#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <polkit/polkit.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE 1
#include <polkitagent/polkitagent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_SECRET 256

struct auth_request {
    GTask *task;
    PolkitAgentSession *session;
    GCancellable *cancellable;
    gulong cancel_handler;
    char *action_id;
    char *message;
    char *identity_text;
    char *info_text;
};

typedef struct _ZeroPolkitAgent ZeroPolkitAgent;
typedef struct _ZeroPolkitAgentClass ZeroPolkitAgentClass;

struct _ZeroPolkitAgent {
    PolkitAgentListener parent_instance;
};

struct _ZeroPolkitAgentClass {
    PolkitAgentListenerClass parent_class;
};

static void zero_polkit_agent_initiate_authentication(PolkitAgentListener *listener,
                                                      const gchar *action_id,
                                                      const gchar *message,
                                                      const gchar *icon_name,
                                                      PolkitDetails *details,
                                                      const gchar *cookie,
                                                      GList *identities,
                                                      GCancellable *cancellable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data);
static gboolean zero_polkit_agent_initiate_authentication_finish(PolkitAgentListener *listener,
                                                                 GAsyncResult *res,
                                                                 GError **error);

G_DEFINE_TYPE(ZeroPolkitAgent, zero_polkit_agent, POLKIT_AGENT_TYPE_LISTENER)

static void secure_clear(char *text)
{
    if (text == NULL) {
        return;
    }
    volatile char *p = text;
    while (*p) {
        *p++ = '\0';
    }
}

static char *prompt_for_response_wayland(const char *message,
                                         const char *identity,
                                         const char *request,
                                         const char *info,
                                         int echo)
{
    const char *prompt_path = getenv("CARDPUTER_ZERO_POLKIT_WAYLAND_PROMPT");
    int pipefd[2];
    pid_t pid;
    char output[MAX_SECRET + 8];
    size_t used = 0;
    int status = 0;

    if (prompt_path == NULL || prompt_path[0] == '\0') {
        prompt_path = "/usr/local/bin/zero-polkit-prompt-wayland";
    }

    if (getenv("WAYLAND_DISPLAY") == NULL || access(prompt_path, X_OK) != 0) {
        return NULL;
    }

    if (pipe(pipefd) != 0) {
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        int null_fd;
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        if (pipefd[1] > STDERR_FILENO) {
            close(pipefd[1]);
        }

        null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            if (null_fd > STDERR_FILENO) {
                close(null_fd);
            }
        }

        execl(prompt_path,
              prompt_path,
              "--message", message != NULL ? message : "Authentication is required",
              "--identity", identity != NULL ? identity : "",
              "--request", request != NULL ? request : "Password:",
              "--info", info != NULL ? info : "",
              echo ? "--echo" : "--secret",
              (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    memset(output, 0, sizeof(output));
    for (;;) {
        ssize_t n = read(pipefd[0], output + used, sizeof(output) - used - 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
        if (used + 1 >= sizeof(output)) {
            break;
        }
    }
    close(pipefd[0]);

    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        secure_clear(output);
        return NULL;
    }

    output[used] = '\0';
    while (used > 0 && (output[used - 1] == '\n' || output[used - 1] == '\r')) {
        output[--used] = '\0';
    }

    char *response = strdup(output);
    secure_clear(output);
    return response;
}

static void auth_request_free(struct auth_request *request)
{
    if (request == NULL) {
        return;
    }
    if (request->cancellable != NULL && request->cancel_handler != 0) {
        g_cancellable_disconnect(request->cancellable, request->cancel_handler);
    }
    g_clear_object(&request->cancellable);
    g_clear_object(&request->session);
    g_clear_object(&request->task);
    g_free(request->action_id);
    g_free(request->message);
    g_free(request->identity_text);
    g_free(request->info_text);
    g_free(request);
}

GType polkit_unix_session_get_type(void);

static void on_auth_cancelled(GCancellable *cancellable, gpointer user_data)
{
    (void)cancellable;
    struct auth_request *auth = user_data;
    if (auth->session != NULL) {
        polkit_agent_session_cancel(auth->session);
    }
}

static PolkitIdentity *choose_identity(GList *identities)
{
    PolkitIdentity *current = polkit_unix_user_new((gint)getuid());

    for (GList *item = identities; item != NULL; item = item->next) {
        PolkitIdentity *identity = POLKIT_IDENTITY(item->data);
        if (polkit_identity_equal(identity, current)) {
            g_object_unref(current);
            return g_object_ref(identity);
        }
    }

    g_object_unref(current);

    if (identities != NULL) {
        return g_object_ref(POLKIT_IDENTITY(identities->data));
    }

    return NULL;
}

static char *identity_label(PolkitIdentity *identity)
{
    char *raw = polkit_identity_to_string(identity);
    char *label = NULL;

    if (g_str_has_prefix(raw, "unix-user:")) {
        label = g_strdup_printf("User: %s", raw + strlen("unix-user:"));
    } else if (g_str_has_prefix(raw, "unix-group:")) {
        label = g_strdup_printf("Group: %s", raw + strlen("unix-group:"));
    } else {
        label = g_strdup(raw);
    }

    g_free(raw);
    return label;
}

static void on_session_request(PolkitAgentSession *session,
                               char *request,
                               gboolean echo_on,
                               gpointer user_data)
{
    struct auth_request *auth = user_data;
    if (auth->cancellable != NULL && g_cancellable_is_cancelled(auth->cancellable)) {
        polkit_agent_session_cancel(session);
        return;
    }

    char *response = prompt_for_response_wayland(auth->message,
                                                 auth->identity_text,
                                                 request != NULL ? request : "Password:",
                                                 auth->info_text,
                                                 echo_on ? 1 : 0);
    if (response == NULL) {
        g_printerr("zero-polkit-agent: Wayland authentication prompt cancelled or unavailable\n");
        polkit_agent_session_cancel(session);
        return;
    }

    polkit_agent_session_response(session, response);
    secure_clear(response);
    free(response);
}

static void on_session_show_error(PolkitAgentSession *session, char *text, gpointer user_data)
{
    (void)session;
    struct auth_request *auth = user_data;
    g_free(auth->info_text);
    auth->info_text = g_strdup(text != NULL ? text : "Authentication error");
}

static void on_session_show_info(PolkitAgentSession *session, char *text, gpointer user_data)
{
    (void)session;
    struct auth_request *auth = user_data;
    g_free(auth->info_text);
    auth->info_text = g_strdup(text != NULL ? text : "");
}

static void on_session_completed(PolkitAgentSession *session,
                                 gboolean gained_authorization,
                                 gpointer user_data)
{
    (void)session;
    struct auth_request *auth = user_data;

    if (gained_authorization) {
        g_print("zero-polkit-agent: authorization granted for %s\n", auth->action_id);
        g_task_return_boolean(auth->task, TRUE);
    } else {
        g_print("zero-polkit-agent: authorization denied for %s\n", auth->action_id);
        g_task_return_new_error(auth->task,
                                G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED,
                                "Authorization failed or was cancelled");
    }

    auth_request_free(auth);
}

static void zero_polkit_agent_initiate_authentication(PolkitAgentListener *listener,
                                                      const gchar *action_id,
                                                      const gchar *message,
                                                      const gchar *icon_name,
                                                      PolkitDetails *details,
                                                      const gchar *cookie,
                                                      GList *identities,
                                                      GCancellable *cancellable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data)
{
    (void)icon_name;
    (void)details;

    GTask *task = g_task_new(listener, cancellable, callback, user_data);
    PolkitIdentity *identity = choose_identity(identities);
    if (identity == NULL) {
        g_task_return_new_error(task,
                                G_IO_ERROR,
                                G_IO_ERROR_FAILED,
                                "No polkit identity was offered");
        g_object_unref(task);
        return;
    }

    struct auth_request *auth = g_new0(struct auth_request, 1);
    auth->task = task;
    auth->action_id = g_strdup(action_id != NULL ? action_id : "");
    auth->message = g_strdup(message != NULL ? message : "Authentication is required");
    auth->identity_text = identity_label(identity);
    auth->session = polkit_agent_session_new(identity, cookie);
    g_print("zero-polkit-agent: authentication requested for %s\n", auth->action_id);
    if (cancellable != NULL) {
        auth->cancellable = g_object_ref(cancellable);
        auth->cancel_handler = g_cancellable_connect(cancellable,
                                                     G_CALLBACK(on_auth_cancelled),
                                                     auth,
                                                     NULL);
    }

    g_signal_connect(auth->session, "request", G_CALLBACK(on_session_request), auth);
    g_signal_connect(auth->session, "show-error", G_CALLBACK(on_session_show_error), auth);
    g_signal_connect(auth->session, "show-info", G_CALLBACK(on_session_show_info), auth);
    g_signal_connect(auth->session, "completed", G_CALLBACK(on_session_completed), auth);

    g_object_unref(identity);
    polkit_agent_session_initiate(auth->session);
}

static gboolean zero_polkit_agent_initiate_authentication_finish(PolkitAgentListener *listener,
                                                                 GAsyncResult *res,
                                                                 GError **error)
{
    g_return_val_if_fail(g_task_is_valid(res, listener), FALSE);
    return g_task_propagate_boolean(G_TASK(res), error);
}

static void zero_polkit_agent_class_init(ZeroPolkitAgentClass *klass)
{
    PolkitAgentListenerClass *listener_class = POLKIT_AGENT_LISTENER_CLASS(klass);
    listener_class->initiate_authentication = zero_polkit_agent_initiate_authentication;
    listener_class->initiate_authentication_finish = zero_polkit_agent_initiate_authentication_finish;
}

static void zero_polkit_agent_init(ZeroPolkitAgent *agent)
{
    (void)agent;
}

static PolkitSubject *current_session_subject(GError **error)
{
    const char *session_id = g_getenv("XDG_SESSION_ID");
    if (session_id != NULL && session_id[0] != '\0') {
        return POLKIT_SUBJECT(polkit_unix_session_new(session_id));
    }

    return POLKIT_SUBJECT(polkit_unix_session_new_for_process_sync((gint)getpid(), NULL, error));
}

int main(void)
{
    GError *error = NULL;
    GMainLoop *loop = NULL;
    PolkitSubject *subject = NULL;
    PolkitAgentListener *listener = NULL;
    gpointer registration = NULL;
    int rc = 1;

    if (g_strcmp0(g_getenv("ZERO_POLKIT_AGENT_VERSION"), "1") == 0) {
        g_print("zero-polkit-agent 0.2 wayland-only\n");
        return 0;
    }

    if (g_getenv("WAYLAND_DISPLAY") == NULL) {
        g_printerr("zero-polkit-agent: WAYLAND_DISPLAY is required\n");
        return 1;
    }

    subject = current_session_subject(&error);
    if (subject == NULL) {
        g_printerr("zero-polkit-agent: cannot determine session: %s\n",
                   error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        goto out;
    }

    listener = g_object_new(zero_polkit_agent_get_type(), NULL);
    registration = polkit_agent_listener_register(listener,
                                                  POLKIT_AGENT_REGISTER_FLAGS_NONE,
                                                  subject,
                                                  NULL,
                                                  NULL,
                                                  &error);
    if (registration == NULL) {
        g_printerr("zero-polkit-agent: cannot register agent: %s\n",
                   error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        goto out;
    }

    g_print("zero-polkit-agent: registered for current Wayland user session\n");
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    rc = 0;

out:
    if (registration != NULL) {
        polkit_agent_listener_unregister(registration);
    }
    if (loop != NULL) {
        g_main_loop_unref(loop);
    }
    g_clear_object(&listener);
    g_clear_object(&subject);
    return rc;
}

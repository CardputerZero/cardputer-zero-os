#include "pam_auth.h"

#include <errno.h>
#include <grp.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct conv_data {
    const char *username;
    const char *password;
};

static void free_passwd_copy(struct passwd *pw);

static int conversation(int num_msg,
                        const struct pam_message **msg,
                        struct pam_response **resp,
                        void *appdata_ptr)
{
    struct conv_data *data = appdata_ptr;
    struct pam_response *responses = NULL;

    if (num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG) {
        return PAM_CONV_ERR;
    }

    responses = calloc((size_t)num_msg, sizeof(*responses));
    if (responses == NULL) {
        return PAM_BUF_ERR;
    }

    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
            responses[i].resp = strdup(data->password != NULL ? data->password : "");
            break;
        case PAM_PROMPT_ECHO_ON:
            responses[i].resp = strdup(data->username != NULL ? data->username : "");
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
            responses[i].resp = NULL;
            break;
        default:
            for (int j = 0; j < i; j++) {
                free(responses[j].resp);
            }
            free(responses);
            return PAM_CONV_ERR;
        }

        if ((msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
             msg[i]->msg_style == PAM_PROMPT_ECHO_ON) &&
            responses[i].resp == NULL) {
            for (int j = 0; j < i; j++) {
                free(responses[j].resp);
            }
            free(responses);
            return PAM_BUF_ERR;
        }
    }

    *resp = responses;
    return PAM_SUCCESS;
}

static int copy_passwd(const char *username, struct passwd *out)
{
    struct passwd pwd;
    struct passwd *result = NULL;
    long buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);

    if (buf_size < 0) {
        buf_size = 16384;
    }

    char *buf = calloc(1, (size_t)buf_size);
    if (buf == NULL) {
        return ENOMEM;
    }

    int rc = getpwnam_r(username, &pwd, buf, (size_t)buf_size, &result);
    if (rc != 0 || result == NULL) {
        free(buf);
        return rc != 0 ? rc : ENOENT;
    }

    memset(out, 0, sizeof(*out));
    out->pw_uid = pwd.pw_uid;
    out->pw_gid = pwd.pw_gid;
    out->pw_name = strdup(pwd.pw_name);
    out->pw_passwd = strdup(pwd.pw_passwd != NULL ? pwd.pw_passwd : "");
    out->pw_gecos = strdup(pwd.pw_gecos != NULL ? pwd.pw_gecos : "");
    out->pw_dir = strdup(pwd.pw_dir != NULL ? pwd.pw_dir : "");
    out->pw_shell = strdup(pwd.pw_shell != NULL ? pwd.pw_shell : "");
    free(buf);

    if (out->pw_name == NULL || out->pw_passwd == NULL || out->pw_gecos == NULL ||
        out->pw_dir == NULL || out->pw_shell == NULL) {
        free_passwd_copy(out);
        return ENOMEM;
    }

    return 0;
}

static void free_passwd_copy(struct passwd *pw)
{
    free(pw->pw_name);
    free(pw->pw_passwd);
    free(pw->pw_gecos);
    free(pw->pw_dir);
    free(pw->pw_shell);
    memset(pw, 0, sizeof(*pw));
}

int zero_pam_start_session(const char *username,
                           const char *password,
                           struct zero_pam_session *session)
{
    struct conv_data data = {
        .username = username,
        .password = password,
    };
    const struct pam_conv conv = {
        .conv = conversation,
        .appdata_ptr = &data,
    };
    int pam_status;

    memset(session, 0, sizeof(*session));
    snprintf(session->username, sizeof(session->username), "%s", username);

    int pw_status = copy_passwd(username, &session->pw);
    if (pw_status != 0) {
        return PAM_USER_UNKNOWN;
    }

    pam_status = pam_start("zero-greeter", username, &conv, &session->pamh);
    if (pam_status != PAM_SUCCESS) {
        free_passwd_copy(&session->pw);
        return pam_status;
    }

    pam_status = pam_authenticate(session->pamh, 0);
    if (pam_status != PAM_SUCCESS) {
        zero_pam_end_session(session, pam_status);
        return pam_status;
    }

    pam_status = pam_acct_mgmt(session->pamh, 0);
    if (pam_status != PAM_SUCCESS) {
        zero_pam_end_session(session, pam_status);
        return pam_status;
    }

    pam_status = pam_setcred(session->pamh, PAM_ESTABLISH_CRED);
    if (pam_status != PAM_SUCCESS) {
        zero_pam_end_session(session, pam_status);
        return pam_status;
    }
    session->cred_established = 1;

    pam_status = pam_open_session(session->pamh, 0);
    if (pam_status != PAM_SUCCESS) {
        zero_pam_end_session(session, pam_status);
        return pam_status;
    }
    session->session_open = 1;

    return PAM_SUCCESS;
}

void zero_pam_end_session(struct zero_pam_session *session, int pam_status)
{
    if (session == NULL) {
        return;
    }

    if (session->pamh != NULL) {
        if (session->session_open) {
            pam_close_session(session->pamh, 0);
        }
        if (session->cred_established) {
            pam_setcred(session->pamh, PAM_DELETE_CRED);
        }
        pam_end(session->pamh, pam_status);
        session->pamh = NULL;
    }

    free_passwd_copy(&session->pw);
    memset(session->username, 0, sizeof(session->username));
}

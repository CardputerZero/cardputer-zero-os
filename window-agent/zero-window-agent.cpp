#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

namespace {

constexpr int kMaxLine = 4096;

bool g_running = true;

class WindowAgent;

struct Task {
    WindowAgent *owner = nullptr;
    int id = 0;
    zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
    std::string app_id;
    std::string title;
    bool activated = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    bool closed = false;
};

struct Client {
    int fd = -1;
    bool subscribed = false;
    std::string input;
    std::string output;
};

std::string runtime_dir()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime) {
        return runtime;
    }
    return {};
}

std::string socket_path()
{
    const char *path = std::getenv("ZERO_WINDOW_AGENT_SOCKET");
    if (path && *path) {
        return path;
    }
    std::string runtime = runtime_dir();
    if (runtime.empty()) {
        return {};
    }
    return runtime + "/cardputer-zero/window-agent.sock";
}

bool set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void signal_handler(int)
{
    g_running = false;
}

bool ensure_parent_dir(const std::string &path)
{
    auto slash = path.rfind('/');
    if (slash == std::string::npos) {
        return true;
    }
    std::string dir = path.substr(0, slash);
    if (dir.empty()) {
        return true;
    }
    if (mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST) {
        chmod(dir.c_str(), 0700);
        return true;
    }
    return false;
}

std::string encode(const std::string &value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7F || ch == '%' || ch == '\t') {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0F]);
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string decode(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int hi = hex_value(value[i + 1]);
            int lo = hex_value(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string &line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= line.size()) {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(decode(line.substr(start)));
            break;
        }
        fields.push_back(decode(line.substr(start, tab - start)));
        start = tab + 1;
    }
    return fields;
}

std::string state_string(const Task &task)
{
    std::string state;
    auto add = [&](const char *name) {
        if (!state.empty()) {
            state.push_back(',');
        }
        state += name;
    };
    if (task.activated) {
        add("activated");
    }
    if (task.minimized) {
        add("minimized");
    }
    if (task.maximized) {
        add("maximized");
    }
    if (task.fullscreen) {
        add("fullscreen");
    }
    return state;
}

bool is_shell_task(const Task &task)
{
    return task.app_id == "cardputer-zero-shell" || task.title == "Cardputer Zero Shell";
}

class WindowAgent {
public:
    bool init_wayland();
    bool init_socket();
    int run();
    int once();

    static void registry_global(void *data, wl_registry *registry, uint32_t name,
                                const char *interface, uint32_t version);
    static void registry_remove(void *, wl_registry *, uint32_t) {}
    static void seat_capabilities(void *, wl_seat *, uint32_t) {}
    static void seat_name(void *, wl_seat *, const char *) {}
    static void manager_toplevel(void *data, zwlr_foreign_toplevel_manager_v1 *,
                                 zwlr_foreign_toplevel_handle_v1 *handle);
    static void manager_finished(void *data, zwlr_foreign_toplevel_manager_v1 *);
    static void handle_title(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *title);
    static void handle_app_id(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *app_id);
    static void handle_output_enter(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
    static void handle_output_leave(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
    static void handle_state(void *data, zwlr_foreign_toplevel_handle_v1 *, wl_array *state);
    static void handle_done(void *data, zwlr_foreign_toplevel_handle_v1 *);
    static void handle_closed(void *data, zwlr_foreign_toplevel_handle_v1 *);
    static void handle_parent(void *, zwlr_foreign_toplevel_handle_v1 *,
                              zwlr_foreign_toplevel_handle_v1 *) {}

private:
    Task *find_task(const std::string &id);
    Task *active_task(bool include_shell);
    Task *shell_task();
    void mark_changed();
    void prune_closed();
    void send(Client &client, const std::string &line);
    void send_error(Client &client, const std::string &code, const std::string &message);
    void send_ok(Client &client, const std::string &request);
    void send_snapshot(Client &client);
    void write_snapshot(std::ostream &out);
    void broadcast_snapshot();
    void handle_command(Client &client, const std::string &line);
    void accept_client();
    bool read_client(size_t index);
    bool flush_client(size_t index);
    void close_client(size_t index);
    bool activate(Task &task);

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
    zwlr_foreign_toplevel_manager_v1 *manager_ = nullptr;
    int listen_fd_ = -1;
    int next_id_ = 1;
    bool changed_ = false;
    std::vector<std::unique_ptr<Task>> tasks_;
    std::vector<Client> clients_;
    std::string sock_path_;
};

const wl_registry_listener kRegistryListener{WindowAgent::registry_global, WindowAgent::registry_remove};
const wl_seat_listener kSeatListener{WindowAgent::seat_capabilities, WindowAgent::seat_name};
const zwlr_foreign_toplevel_manager_v1_listener kManagerListener{
    WindowAgent::manager_toplevel,
    WindowAgent::manager_finished,
};
const zwlr_foreign_toplevel_handle_v1_listener kHandleListener{
    WindowAgent::handle_title,
    WindowAgent::handle_app_id,
    WindowAgent::handle_output_enter,
    WindowAgent::handle_output_leave,
    WindowAgent::handle_state,
    WindowAgent::handle_done,
    WindowAgent::handle_closed,
    WindowAgent::handle_parent,
};

bool WindowAgent::init_wayland()
{
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        std::cerr << "zero-window-agent: cannot connect to Wayland display\n";
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!manager_) {
        std::cerr << "zero-window-agent: compositor does not expose "
                     "zwlr_foreign_toplevel_manager_v1\n";
        return false;
    }
    return true;
}

bool WindowAgent::init_socket()
{
    sock_path_ = socket_path();
    if (sock_path_.empty()) {
        std::cerr << "zero-window-agent: XDG_RUNTIME_DIR is required for the task socket\n";
        return false;
    }
    if (!ensure_parent_dir(sock_path_)) {
        std::cerr << "zero-window-agent: cannot create socket parent directory: "
                  << sock_path_ << "\n";
        return false;
    }

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        std::perror("zero-window-agent: socket");
        return false;
    }
    set_nonblock(listen_fd_);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sock_path_.size() >= sizeof(addr.sun_path)) {
        std::cerr << "zero-window-agent: socket path too long\n";
        return false;
    }
    std::strncpy(addr.sun_path, sock_path_.c_str(), sizeof(addr.sun_path) - 1);

    unlink(sock_path_.c_str());
    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("zero-window-agent: bind");
        return false;
    }
    chmod(sock_path_.c_str(), 0600);
    if (listen(listen_fd_, 8) < 0) {
        std::perror("zero-window-agent: listen");
        return false;
    }
    return true;
}

void WindowAgent::registry_global(void *data, wl_registry *registry, uint32_t name,
                                  const char *interface, uint32_t version)
{
    auto *self = static_cast<WindowAgent *>(data);
    if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
        wl_seat_add_listener(self->seat_, &kSeatListener, self);
    } else if (std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        self->manager_ = static_cast<zwlr_foreign_toplevel_manager_v1 *>(
            wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
                             std::min(version, 3u)));
        zwlr_foreign_toplevel_manager_v1_add_listener(self->manager_, &kManagerListener, self);
    }
}

void WindowAgent::manager_toplevel(void *data, zwlr_foreign_toplevel_manager_v1 *,
                                   zwlr_foreign_toplevel_handle_v1 *handle)
{
    auto *self = static_cast<WindowAgent *>(data);
    auto task = std::make_unique<Task>();
    task->owner = self;
    task->id = self->next_id_++;
    task->handle = handle;
    Task *task_ptr = task.get();
    self->tasks_.push_back(std::move(task));
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, task_ptr);
    self->mark_changed();
}

void WindowAgent::manager_finished(void *data, zwlr_foreign_toplevel_manager_v1 *)
{
    auto *self = static_cast<WindowAgent *>(data);
    g_running = false;
    self->mark_changed();
}

void WindowAgent::handle_title(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *title)
{
    auto *task = static_cast<Task *>(data);
    task->title = title ? title : "";
    task->owner->mark_changed();
}

void WindowAgent::handle_app_id(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *app_id)
{
    auto *task = static_cast<Task *>(data);
    task->app_id = app_id ? app_id : "";
    task->owner->mark_changed();
}

void WindowAgent::handle_state(void *data, zwlr_foreign_toplevel_handle_v1 *, wl_array *state)
{
    auto *task = static_cast<Task *>(data);
    task->maximized = false;
    task->minimized = false;
    task->activated = false;
    task->fullscreen = false;

    for (uint32_t *entry = static_cast<uint32_t *>(state->data);
         reinterpret_cast<char *>(entry) < static_cast<char *>(state->data) + state->size;
         ++entry) {
        switch (*entry) {
        case 0:
            task->maximized = true;
            break;
        case 1:
            task->minimized = true;
            break;
        case 2:
            task->activated = true;
            break;
        case 3:
            task->fullscreen = true;
            break;
        default:
            break;
        }
    }
    task->owner->mark_changed();
}

void WindowAgent::handle_done(void *data, zwlr_foreign_toplevel_handle_v1 *)
{
    auto *task = static_cast<Task *>(data);
    task->closed = false;
    task->owner->mark_changed();
}

void WindowAgent::handle_closed(void *data, zwlr_foreign_toplevel_handle_v1 *)
{
    auto *task = static_cast<Task *>(data);
    task->closed = true;
    task->owner->mark_changed();
}

Task *WindowAgent::find_task(const std::string &id)
{
    if (id.size() < 2 || id[0] != 't') {
        return nullptr;
    }
    char *end = nullptr;
    long numeric = std::strtol(id.c_str() + 1, &end, 10);
    if (!end || *end || numeric <= 0) {
        return nullptr;
    }
    for (auto &task : tasks_) {
        if (!task->closed && task->id == numeric) {
            return task.get();
        }
    }
    return nullptr;
}

Task *WindowAgent::active_task(bool include_shell)
{
    for (auto &task : tasks_) {
        if (!task->closed && task->activated && (include_shell || !is_shell_task(*task))) {
            return task.get();
        }
    }
    return nullptr;
}

Task *WindowAgent::shell_task()
{
    for (auto &task : tasks_) {
        if (!task->closed && is_shell_task(*task)) {
            return task.get();
        }
    }
    return nullptr;
}

void WindowAgent::mark_changed()
{
    changed_ = true;
}

void WindowAgent::prune_closed()
{
    bool removed = false;
    auto it = tasks_.begin();
    while (it != tasks_.end()) {
        if ((*it)->closed) {
            if ((*it)->handle) {
                zwlr_foreign_toplevel_handle_v1_destroy((*it)->handle);
            }
            it = tasks_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed) {
        mark_changed();
    }
}

void WindowAgent::send(Client &client, const std::string &line)
{
    client.output += line;
    client.output.push_back('\n');
}

void WindowAgent::send_error(Client &client, const std::string &code, const std::string &message)
{
    send(client, "error\t" + encode(code) + "\t" + encode(message));
}

void WindowAgent::send_ok(Client &client, const std::string &request)
{
    send(client, "ok\t" + encode(request));
}

void WindowAgent::send_snapshot(Client &client)
{
    send(client, "snapshot-begin");
    for (const auto &task : tasks_) {
        if (task->closed) {
            continue;
        }
        send(client, "task\tt" + std::to_string(task->id) + "\t" +
                         encode(task->app_id) + "\t" + encode(task->title) + "\t" +
                         encode(state_string(*task)));
    }
    send(client, "snapshot-end");
}

void WindowAgent::write_snapshot(std::ostream &out)
{
    out << "snapshot-begin\n";
    for (const auto &task : tasks_) {
        if (task->closed) {
            continue;
        }
        out << "task\tt" << task->id << '\t'
            << encode(task->app_id) << '\t'
            << encode(task->title) << '\t'
            << encode(state_string(*task)) << '\n';
    }
    out << "snapshot-end\n";
}

void WindowAgent::broadcast_snapshot()
{
    for (auto &client : clients_) {
        if (client.subscribed) {
            send_snapshot(client);
        }
    }
}

bool WindowAgent::activate(Task &task)
{
    if (!seat_) {
        return false;
    }
    zwlr_foreign_toplevel_handle_v1_activate(task.handle, seat_);
    return true;
}

void WindowAgent::handle_command(Client &client, const std::string &line)
{
    auto fields = split_tabs(line);
    if (fields.empty() || fields[0].empty()) {
        return;
    }
    const std::string &command = fields[0];

    if (command == "hello") {
        send(client, "hello\tZWA1\tzero-window-agent");
    } else if (command == "list") {
        send_snapshot(client);
    } else if (command == "subscribe") {
        client.subscribed = true;
        send_snapshot(client);
    } else if (command == "activate" || command == "minimize" || command == "restore" ||
               command == "close") {
        if (fields.size() < 2) {
            send_error(client, "bad-request", "missing task id");
            return;
        }
        Task *task = find_task(fields[1]);
        if (!task) {
            send_error(client, "not-found", "task not found");
            return;
        }
        if (command == "activate") {
            if (!activate(*task)) {
                send_error(client, "unsupported", "no Wayland seat");
                return;
            }
        } else if (command == "minimize") {
            zwlr_foreign_toplevel_handle_v1_set_minimized(task->handle);
        } else if (command == "restore") {
            zwlr_foreign_toplevel_handle_v1_unset_minimized(task->handle);
            activate(*task);
        } else if (command == "close") {
            zwlr_foreign_toplevel_handle_v1_close(task->handle);
        }
        wl_display_flush(display_);
        send_ok(client, command);
    } else if (command == "focus-shell") {
        Task *task = shell_task();
        if (!task) {
            send_error(client, "not-found", "shell task not found");
            return;
        }
        if (!activate(*task)) {
            send_error(client, "unsupported", "no Wayland seat");
            return;
        }
        wl_display_flush(display_);
        send_ok(client, command);
    } else if (command == "active-is-shell") {
        Task *task = active_task(true);
        if (task && is_shell_task(*task)) {
            send_ok(client, command);
        } else {
            send_error(client, "not-active", "shell is not active");
        }
    } else if (command == "minimize-active" || command == "close-active") {
        Task *task = active_task(false);
        if (!task) {
            send_error(client, "not-found", "no active non-shell task");
            return;
        }
        if (command == "minimize-active") {
            zwlr_foreign_toplevel_handle_v1_set_minimized(task->handle);
        } else {
            zwlr_foreign_toplevel_handle_v1_close(task->handle);
        }
        wl_display_flush(display_);
        send_ok(client, command);
    } else {
        send_error(client, "unknown-command", command);
    }
}

void WindowAgent::accept_client()
{
    while (true) {
        int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno != EINTR) {
                std::perror("zero-window-agent: accept");
            }
            return;
        }
        clients_.push_back(Client{fd, false, {}, {}});
    }
}

bool WindowAgent::read_client(size_t index)
{
    Client &client = clients_[index];
    char buffer[512];
    while (true) {
        ssize_t count = read(client.fd, buffer, sizeof(buffer));
        if (count > 0) {
            client.input.append(buffer, static_cast<size_t>(count));
            if (client.input.size() > kMaxLine) {
                return false;
            }
            size_t newline = std::string::npos;
            while ((newline = client.input.find('\n')) != std::string::npos) {
                std::string line = client.input.substr(0, newline);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                client.input.erase(0, newline + 1);
                handle_command(client, line);
            }
        } else if (count == 0) {
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
    }
}

bool WindowAgent::flush_client(size_t index)
{
    Client &client = clients_[index];
    while (!client.output.empty()) {
        ssize_t count = write(client.fd, client.output.data(), client.output.size());
        if (count > 0) {
            client.output.erase(0, static_cast<size_t>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}

void WindowAgent::close_client(size_t index)
{
    close(clients_[index].fd);
    clients_.erase(clients_.begin() + static_cast<long>(index));
}

int WindowAgent::once()
{
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);
    write_snapshot(std::cout);
    return 0;
}

int WindowAgent::run()
{
    while (g_running && wl_display_get_error(display_) == 0) {
        wl_display_dispatch_pending(display_);
        prune_closed();
        if (changed_) {
            broadcast_snapshot();
            changed_ = false;
        }

        std::vector<pollfd> fds;
        fds.push_back(pollfd{wl_display_get_fd(display_), POLLIN, 0});
        fds.push_back(pollfd{listen_fd_, POLLIN, 0});
        for (const auto &client : clients_) {
            short events = POLLIN;
            if (!client.output.empty()) {
                events |= POLLOUT;
            }
            fds.push_back(pollfd{client.fd, events, 0});
        }

        wl_display_flush(display_);
        int rc = poll(fds.data(), fds.size(), 1000);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("zero-window-agent: poll");
            return 1;
        }
        if (rc == 0) {
            continue;
        }
        if (fds[0].revents & POLLIN) {
            if (wl_display_dispatch(display_) < 0) {
                break;
            }
        }
        if (fds[1].revents & POLLIN) {
            accept_client();
        }

        for (size_t i = 0; i < clients_.size();) {
            size_t fd_index = i + 2;
            bool keep = true;
            if (fd_index < fds.size() && (fds[fd_index].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                keep = false;
            }
            if (keep && fd_index < fds.size() && (fds[fd_index].revents & POLLIN)) {
                keep = read_client(i);
            }
            if (keep && fd_index < fds.size() && (fds[fd_index].revents & POLLOUT)) {
                keep = flush_client(i);
            }
            if (!keep) {
                close_client(i);
            } else {
                ++i;
            }
        }
    }
    unlink(sock_path_.c_str());
    return wl_display_get_error(display_) == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    bool once = argc > 1 && std::strcmp(argv[1], "--once") == 0;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    WindowAgent agent;
    if (!agent.init_wayland()) {
        return 1;
    }
    if (once) {
        return agent.once();
    }
    if (!agent.init_socket()) {
        return 1;
    }
    return agent.run();
}

#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <pwd.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 170;
constexpr int kStride = kWidth * 4;
constexpr int kBufferSize = kStride * kHeight;

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct UserEntry {
    std::string name;
    uid_t uid = 0;
};

enum class UiMode {
    Login,
    UserMenu,
    PowerMenu,
    Authenticating,
    StartingSession,
};

struct Glyph {
    char ch;
    uint8_t rows[7];
};

constexpr Color kZeroBg{0xE9, 0xE4, 0xD5};
constexpr Color kPanel{0xF4, 0xF0, 0xE6};
constexpr Color kTaskButton{0xEF, 0xE8, 0xD9};
constexpr Color kIconWell{0xF8, 0xF4, 0xEA};
constexpr Color kInk{0x17, 0x17, 0x17};
constexpr Color kLine{0x2A, 0x2A, 0x2A};
constexpr Color kMuted{0x6E, 0x6A, 0x61};
constexpr Color kSoftLine{0xBB, 0xB1, 0x9E};
constexpr Color kGridDot{0xC9, 0xC1, 0xAE};
constexpr Color kAccent{0xE6, 0x6A, 0x2C};
constexpr Color kOk{0x3A, 0x7D, 0x44};
constexpr Color kWarn{0xB9, 0x4A, 0x2C};
constexpr Color kShadow{0xBD, 0xB5, 0xA4};

constexpr Glyph kGlyphs[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'!', {0x04,0x04,0x04,0x04,0x00,0x04,0x00}},
    {'%', {0x11,0x02,0x04,0x08,0x11,0x00,0x00}},
    {'-', {0x00,0x00,0x00,0x0E,0x00,0x00,0x00}},
    {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x04,0x00}},
    {'/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
    {':', {0x00,0x04,0x00,0x00,0x04,0x00,0x00}},
    {'<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02}},
    {'>', {0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J', {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'a', {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}},
    {'b', {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}},
    {'c', {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}},
    {'d', {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}},
    {'e', {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}},
    {'f', {0x06,0x08,0x08,0x1E,0x08,0x08,0x08}},
    {'g', {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}},
    {'h', {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}},
    {'i', {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}},
    {'j', {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}},
    {'k', {0x10,0x10,0x12,0x14,0x18,0x14,0x12}},
    {'l', {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'m', {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}},
    {'n', {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}},
    {'o', {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}},
    {'p', {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}},
    {'q', {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}},
    {'r', {0x00,0x00,0x16,0x19,0x10,0x10,0x10}},
    {'s', {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}},
    {'t', {0x08,0x08,0x1E,0x08,0x08,0x09,0x06}},
    {'u', {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}},
    {'v', {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}},
    {'w', {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}},
    {'x', {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}},
    {'y', {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}},
    {'z', {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}},
};

constexpr uint8_t kQuestionRows[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};

const uint8_t *glyph_rows(char ch)
{
    for (const auto &glyph : kGlyphs) {
        if (glyph.ch == ch) {
            return glyph.rows;
        }
    }
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (upper != ch) {
        for (const auto &glyph : kGlyphs) {
            if (glyph.ch == upper) {
                return glyph.rows;
            }
        }
    }
    return kQuestionRows;
}

uint32_t pixel(Color color)
{
    return 0xFF000000u |
           (static_cast<uint32_t>(color.r) << 16) |
           (static_cast<uint32_t>(color.g) << 8) |
           static_cast<uint32_t>(color.b);
}

std::string display_ascii(const std::string &text)
{
    std::string out;
    bool last_space = false;
    for (unsigned char ch : text) {
        if (ch >= 0x80) {
            continue;
        }
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::toupper(ch)));
            last_space = false;
        } else if ((ch == ' ' || ch == '-' || ch == '_') && !out.empty() && !last_space) {
            out.push_back(' ');
            last_space = true;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string fit_text(std::string text, size_t max_chars)
{
    text = display_ascii(text);
    if (text.size() > max_chars) {
        text.resize(max_chars > 1 ? max_chars - 1 : max_chars);
        if (max_chars > 1) {
            text.push_back('.');
        }
    }
    return text;
}

std::string display_identifier(const std::string &text)
{
    std::string out;
    for (unsigned char ch : text) {
        if (ch >= 0x80) {
            continue;
        }
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::string fit_identifier(std::string text, size_t max_chars)
{
    text = display_identifier(text);
    if (text.size() > max_chars) {
        text.resize(max_chars > 1 ? max_chars - 1 : max_chars);
        if (max_chars > 1) {
            text.push_back('.');
        }
    }
    return text.empty() ? "?" : text;
}

std::string current_time_label()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buf[8] = {};
    std::strftime(buf, sizeof(buf), "%H:%M", &local);
    return buf;
}

bool shell_is_login_capable(const char *shell)
{
    if (shell == nullptr || shell[0] == '\0') {
        return true;
    }
    return std::strstr(shell, "nologin") == nullptr && std::strstr(shell, "false") == nullptr;
}

std::vector<UserEntry> load_users()
{
    std::vector<UserEntry> users;
    setpwent();
    while (passwd *pw = getpwent()) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000) {
            continue;
        }
        if (pw->pw_dir == nullptr || std::strncmp(pw->pw_dir, "/home/", 6) != 0) {
            continue;
        }
        if (!shell_is_login_capable(pw->pw_shell)) {
            continue;
        }
        users.push_back({pw->pw_name, pw->pw_uid});
    }
    endpwent();
    return users;
}

int create_anonymous_file(size_t size)
{
    int fd = -1;
#ifdef SYS_memfd_create
    fd = static_cast<int>(syscall(SYS_memfd_create, "zero-greeter-wayland", MFD_CLOEXEC));
    if (fd >= 0) {
        if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
            return fd;
        }
        close(fd);
    }
#endif

    char name[] = "/zero-greeter-wayland-XXXXXX";
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return -1;
    }
    shm_unlink(name);
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool write_all(int fd, const std::string &data)
{
    const char *cursor = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        cursor += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

std::string trim_line(std::string text)
{
    while (!text.empty()) {
        char ch = text.back();
        if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t') {
            break;
        }
        text.pop_back();
    }
    size_t first = 0;
    while (first < text.size()) {
        char ch = text[first];
        if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t') {
            break;
        }
        ++first;
    }
    if (first > 0) {
        text.erase(0, first);
    }
    return text;
}

bool authenticate_and_start_session(const std::string &username, const std::string &password, std::string &message)
{
    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
        if (input_pipe[0] >= 0) close(input_pipe[0]);
        if (input_pipe[1] >= 0) close(input_pipe[1]);
        if (output_pipe[0] >= 0) close(output_pipe[0]);
        if (output_pipe[1] >= 0) close(output_pipe[1]);
        message = "AUTH HELPER FAILED";
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        message = "AUTH HELPER FAILED";
        return false;
    }
    if (pid == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execl("/usr/local/libexec/cardputer-zero/zero-greeter-auth",
              "zero-greeter-auth", static_cast<char *>(nullptr));
        _exit(127);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);

    std::string request = username + "\n" + password + "\n";
    bool sent = write_all(input_pipe[1], request);
    close(input_pipe[1]);

    std::string response;
    char buffer[128];
    for (;;) {
        ssize_t count = read(output_pipe[0], buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (count == 0) {
            break;
        }
        response.append(buffer, buffer + count);
        if (response.size() > 512) {
            response.resize(512);
            break;
        }
    }
    close(output_pipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            message = "AUTH HELPER FAILED";
            return false;
        }
    }

    response = trim_line(response);
    if (!sent || !WIFEXITED(status)) {
        message = "AUTH HELPER FAILED";
        return false;
    }
    if (WEXITSTATUS(status) == 0) {
        message = "SESSION STARTING";
        return true;
    }
    if (response == "AUTH FAILED" || response == "PAM ERROR" || response == "SESSION FAILED" ||
        response == "USER INVALID") {
        message = response;
        return false;
    }
    if (WEXITSTATUS(status) == 10) {
        message = "AUTH FAILED";
        return false;
    }
    if (WEXITSTATUS(status) == 20) {
        message = "SESSION FAILED";
        return false;
    }
    message = "AUTH HELPER FAILED";
    return false;
}

class GreeterWindow {
public:
    struct Buffer {
        wl_buffer *buffer = nullptr;
        uint32_t *pixels = nullptr;
        bool busy = false;
    };

    bool init();
    int run();

    static void registry_global(void *data, wl_registry *registry, uint32_t name,
                                const char *interface, uint32_t version);
    static void registry_remove(void *, wl_registry *, uint32_t) {}
    static void wm_ping(void *, xdg_wm_base *wm, uint32_t serial);
    static void surface_configure(void *data, xdg_surface *surface, uint32_t serial);
    static void toplevel_configure(void *, xdg_toplevel *, int32_t, int32_t, wl_array *) {}
    static void toplevel_close(void *data, xdg_toplevel *);
    static void seat_capabilities(void *data, wl_seat *, uint32_t capabilities);
    static void seat_name(void *, wl_seat *, const char *) {}
    static void keyboard_keymap(void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size);
    static void keyboard_enter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {}
    static void keyboard_leave(void *, wl_keyboard *, uint32_t, wl_surface *) {}
    static void keyboard_key(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t key, uint32_t state);
    static void keyboard_modifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed,
                                   uint32_t latched, uint32_t locked, uint32_t group);
    static void keyboard_repeat(void *, wl_keyboard *, int32_t, int32_t) {}
    static void buffer_release(void *data, wl_buffer *);

private:
    bool create_buffer(Buffer &buffer);
    Buffer *next_buffer();
    void reload_users();
    void render();
    void attempt_login();
    bool handle_keysym(xkb_keysym_t sym);
    void handle_text(const char *utf8);
    void run_power_action();
    void clamp_selection();
    void clear(uint32_t *pixels, Color color);
    void fill_rect(uint32_t *pixels, int x, int y, int w, int h, Color color);
    void draw_rect(uint32_t *pixels, int x, int y, int w, int h, Color color);
    void draw_text(uint32_t *pixels, int x, int y, const std::string &text, Color color, int scale = 1);
    void draw_text_centered(uint32_t *pixels, int cx, int y, const std::string &text, Color color, int scale = 1);
    void draw_text_right(uint32_t *pixels, int right, int y, const std::string &text, Color color, int scale = 1);
    void draw_user_icon(uint32_t *pixels, int x, int y, Color color);
    void draw_power_icon(uint32_t *pixels, int x, int y, Color color);
    void draw_battery(uint32_t *pixels, int x, int y);
    void draw_password(uint32_t *pixels, int x, int y);
    void draw_frame(uint32_t *pixels);
    void draw_user_menu(uint32_t *pixels);
    void draw_power_menu(uint32_t *pixels);

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_compositor *compositor_ = nullptr;
    wl_shm *shm_ = nullptr;
    wl_seat *seat_ = nullptr;
    wl_keyboard *keyboard_ = nullptr;
    wl_surface *surface_ = nullptr;
    xdg_wm_base *wm_ = nullptr;
    xdg_surface *xdg_surface_ = nullptr;
    xdg_toplevel *toplevel_ = nullptr;
    xkb_context *xkb_context_ = nullptr;
    xkb_keymap *xkb_keymap_ = nullptr;
    xkb_state *xkb_state_ = nullptr;
    Buffer buffers_[2]{};
    bool configured_ = false;
    bool running_ = true;
    bool session_started_ = false;
    bool dirty_ = true;
    UiMode mode_ = UiMode::Login;
    std::vector<UserEntry> users_;
    size_t selected_ = 0;
    size_t menu_selected_ = 0;
    int power_selection_ = 2;
    std::string password_;
    std::string message_ = "PAM AUTHENTICATION";
    bool error_ = false;
};

const wl_registry_listener kRegistryListener{GreeterWindow::registry_global, GreeterWindow::registry_remove};
const xdg_wm_base_listener kWmListener{GreeterWindow::wm_ping};
const xdg_surface_listener kSurfaceListener{GreeterWindow::surface_configure};
const xdg_toplevel_listener kToplevelListener{
    GreeterWindow::toplevel_configure,
    GreeterWindow::toplevel_close,
    nullptr,
    nullptr,
};
const wl_seat_listener kSeatListener{GreeterWindow::seat_capabilities, GreeterWindow::seat_name};
const wl_keyboard_listener kKeyboardListener{
    GreeterWindow::keyboard_keymap,
    GreeterWindow::keyboard_enter,
    GreeterWindow::keyboard_leave,
    GreeterWindow::keyboard_key,
    GreeterWindow::keyboard_modifiers,
    GreeterWindow::keyboard_repeat,
};
const wl_buffer_listener kBufferListener{GreeterWindow::buffer_release};

void GreeterWindow::registry_global(void *data, wl_registry *registry, uint32_t name,
                                    const char *interface, uint32_t version)
{
    auto *self = static_cast<GreeterWindow *>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor *>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm_ = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
        wl_seat_add_listener(self->seat_, &kSeatListener, self);
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        self->wm_ = static_cast<xdg_wm_base *>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(self->wm_, &kWmListener, self);
    }
}

void GreeterWindow::wm_ping(void *, xdg_wm_base *wm, uint32_t serial)
{
    xdg_wm_base_pong(wm, serial);
}

void GreeterWindow::surface_configure(void *data, xdg_surface *surface, uint32_t serial)
{
    auto *self = static_cast<GreeterWindow *>(data);
    xdg_surface_ack_configure(surface, serial);
    self->configured_ = true;
    self->dirty_ = true;
}

void GreeterWindow::toplevel_close(void *data, xdg_toplevel *)
{
    static_cast<GreeterWindow *>(data)->running_ = false;
}

void GreeterWindow::seat_capabilities(void *data, wl_seat *, uint32_t capabilities)
{
    auto *self = static_cast<GreeterWindow *>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && self->keyboard_ == nullptr) {
        self->keyboard_ = wl_seat_get_keyboard(self->seat_);
        wl_keyboard_add_listener(self->keyboard_, &kKeyboardListener, self);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && self->keyboard_ != nullptr) {
        wl_keyboard_destroy(self->keyboard_);
        self->keyboard_ = nullptr;
    }
}

void GreeterWindow::keyboard_keymap(void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size)
{
    auto *self = static_cast<GreeterWindow *>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }

    if (self->xkb_context_ == nullptr) {
        self->xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }
    if (self->xkb_keymap_ != nullptr) {
        xkb_keymap_unref(self->xkb_keymap_);
    }
    if (self->xkb_state_ != nullptr) {
        xkb_state_unref(self->xkb_state_);
    }
    self->xkb_keymap_ = xkb_keymap_new_from_string(
        self->xkb_context_, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    self->xkb_state_ = self->xkb_keymap_ != nullptr ? xkb_state_new(self->xkb_keymap_) : nullptr;

    munmap(map, size);
    close(fd);
}

void GreeterWindow::keyboard_key(void *data, wl_keyboard *, uint32_t, uint32_t,
                                 uint32_t key, uint32_t state)
{
    auto *self = static_cast<GreeterWindow *>(data);
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || self->xkb_state_ == nullptr) {
        return;
    }

    xkb_keycode_t keycode = static_cast<xkb_keycode_t>(key + 8);
    xkb_keysym_t sym = xkb_state_key_get_one_sym(self->xkb_state_, keycode);
    bool handled = self->handle_keysym(sym);
    if (!self->running_ || handled) {
        return;
    }

    char utf8[16] = {};
    int len = xkb_state_key_get_utf8(self->xkb_state_, keycode, utf8, sizeof(utf8));
    if (len > 0) {
        self->handle_text(utf8);
    }
}

void GreeterWindow::keyboard_modifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed,
                                       uint32_t latched, uint32_t locked, uint32_t group)
{
    auto *self = static_cast<GreeterWindow *>(data);
    if (self->xkb_state_ != nullptr) {
        xkb_state_update_mask(self->xkb_state_, depressed, latched, locked, 0, 0, group);
    }
}

void GreeterWindow::buffer_release(void *data, wl_buffer *)
{
    static_cast<Buffer *>(data)->busy = false;
}

bool GreeterWindow::create_buffer(Buffer &buffer)
{
    int fd = create_anonymous_file(kBufferSize);
    if (fd < 0) {
        std::cerr << "zero-greeter-wayland: cannot create shm file: " << std::strerror(errno) << "\n";
        return false;
    }
    void *data = mmap(nullptr, kBufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        std::cerr << "zero-greeter-wayland: cannot mmap shm buffer: " << std::strerror(errno) << "\n";
        close(fd);
        return false;
    }

    wl_shm_pool *pool = wl_shm_create_pool(shm_, fd, kBufferSize);
    buffer.buffer = wl_shm_pool_create_buffer(pool, 0, kWidth, kHeight, kStride, WL_SHM_FORMAT_XRGB8888);
    buffer.pixels = static_cast<uint32_t *>(data);
    wl_buffer_add_listener(buffer.buffer, &kBufferListener, &buffer);
    wl_shm_pool_destroy(pool);
    close(fd);
    return true;
}

GreeterWindow::Buffer *GreeterWindow::next_buffer()
{
    for (auto &buffer : buffers_) {
        if (!buffer.busy) {
            return &buffer;
        }
    }
    return nullptr;
}

bool GreeterWindow::init()
{
    reload_users();
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        std::cerr << "zero-greeter-wayland: cannot connect to Wayland display\n";
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!compositor_ || !shm_ || !wm_) {
        std::cerr << "zero-greeter-wayland: missing compositor/shm/xdg-shell globals\n";
        return false;
    }

    surface_ = wl_compositor_create_surface(compositor_);
    xdg_surface_ = xdg_wm_base_get_xdg_surface(wm_, surface_);
    xdg_surface_add_listener(xdg_surface_, &kSurfaceListener, this);
    toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
    xdg_toplevel_set_title(toplevel_, "Cardputer Zero Greeter");
    xdg_toplevel_set_app_id(toplevel_, "cardputer-zero-greeter");
    xdg_toplevel_set_min_size(toplevel_, kWidth, kHeight);
    xdg_toplevel_set_max_size(toplevel_, kWidth, kHeight);
    xdg_toplevel_set_fullscreen(toplevel_, nullptr);

    if (!create_buffer(buffers_[0]) || !create_buffer(buffers_[1])) {
        return false;
    }

    wl_surface_commit(surface_);
    wl_display_flush(display_);
    return true;
}

void GreeterWindow::reload_users()
{
    users_ = load_users();
    clamp_selection();
}

void GreeterWindow::clamp_selection()
{
    if (users_.empty()) {
        selected_ = 0;
        menu_selected_ = 0;
        return;
    }
    selected_ %= users_.size();
    menu_selected_ %= users_.size();
}

void GreeterWindow::attempt_login()
{
    if (users_.empty()) {
        message_ = "CREATE A USER FIRST";
        error_ = true;
        dirty_ = true;
        return;
    }

    mode_ = UiMode::Authenticating;
    message_ = "AUTHENTICATING";
    error_ = false;
    dirty_ = true;
    render();
    wl_display_flush(display_);

    std::string login_message;
    UserEntry user = users_[selected_];
    bool ok = authenticate_and_start_session(user.name, password_, login_message);
    std::fill(password_.begin(), password_.end(), '\0');
    password_.clear();
    message_ = login_message;
    error_ = !ok;
    if (ok) {
        mode_ = UiMode::StartingSession;
        session_started_ = true;
        running_ = false;
        return;
    }
    mode_ = UiMode::Login;
    dirty_ = true;
}

void GreeterWindow::run_power_action()
{
    const char *action = power_selection_ == 0 ? "shutdown" : power_selection_ == 1 ? "reboot" : "";
    if (action[0] == '\0') {
        mode_ = UiMode::Login;
        dirty_ = true;
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/local/sbin/zero-helper", "zero-helper", action, static_cast<char *>(nullptr));
        _exit(127);
    }
    mode_ = UiMode::Login;
    dirty_ = true;
}

bool GreeterWindow::handle_keysym(xkb_keysym_t sym)
{
    if (mode_ == UiMode::UserMenu) {
        switch (sym) {
            case XKB_KEY_Escape:
                mode_ = UiMode::Login;
                dirty_ = true;
                return true;
            case XKB_KEY_Tab:
            case XKB_KEY_Down:
            case XKB_KEY_Right:
                if (!users_.empty()) {
                    menu_selected_ = (menu_selected_ + 1) % users_.size();
                }
                dirty_ = true;
                return true;
            case XKB_KEY_Up:
            case XKB_KEY_Left:
                if (!users_.empty()) {
                    menu_selected_ = (menu_selected_ + users_.size() - 1) % users_.size();
                }
                dirty_ = true;
                return true;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                if (!users_.empty()) {
                    selected_ = menu_selected_;
                    password_.clear();
                    message_ = "PAM AUTHENTICATION";
                    error_ = false;
                    mode_ = UiMode::Login;
                    dirty_ = true;
                }
                return true;
            default:
                return true;
        }
    }

    if (mode_ == UiMode::PowerMenu) {
        switch (sym) {
            case XKB_KEY_Escape:
                mode_ = UiMode::Login;
                dirty_ = true;
                return true;
            case XKB_KEY_Tab:
            case XKB_KEY_Down:
            case XKB_KEY_Right:
                power_selection_ = (power_selection_ + 1) % 3;
                dirty_ = true;
                return true;
            case XKB_KEY_Up:
            case XKB_KEY_Left:
                power_selection_ = (power_selection_ + 2) % 3;
                dirty_ = true;
                return true;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                run_power_action();
                return true;
            default:
                return true;
        }
    }

    if (mode_ == UiMode::Authenticating || mode_ == UiMode::StartingSession) {
        return true;
    }

    switch (sym) {
        case XKB_KEY_Tab:
            reload_users();
            menu_selected_ = selected_;
            mode_ = UiMode::UserMenu;
            message_.clear();
            error_ = false;
            dirty_ = true;
            return true;
        case XKB_KEY_Escape:
            power_selection_ = 2;
            mode_ = UiMode::PowerMenu;
            message_.clear();
            error_ = false;
            dirty_ = true;
            return true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            attempt_login();
            return true;
        case XKB_KEY_BackSpace:
        case XKB_KEY_Delete:
            if (!password_.empty()) {
                password_.pop_back();
            }
            message_ = "PAM AUTHENTICATION";
            error_ = false;
            dirty_ = true;
            return true;
        default:
            return false;
    }
}

void GreeterWindow::handle_text(const char *utf8)
{
    if (mode_ != UiMode::Login || utf8 == nullptr || utf8[0] == '\0') {
        return;
    }
    unsigned char ch = static_cast<unsigned char>(utf8[0]);
    if (utf8[1] == '\0' && std::isprint(ch) && password_.size() < 128) {
        password_.push_back(static_cast<char>(ch));
        message_ = "PAM AUTHENTICATION";
        error_ = false;
        dirty_ = true;
    }
}

void GreeterWindow::clear(uint32_t *pixels, Color color)
{
    std::fill(pixels, pixels + (kWidth * kHeight), pixel(color));
}

void GreeterWindow::fill_rect(uint32_t *pixels, int x, int y, int w, int h, Color color)
{
    int x0 = std::clamp(x, 0, kWidth);
    int y0 = std::clamp(y, 0, kHeight);
    int x1 = std::clamp(x + w, 0, kWidth);
    int y1 = std::clamp(y + h, 0, kHeight);
    uint32_t value = pixel(color);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            pixels[yy * kWidth + xx] = value;
        }
    }
}

void GreeterWindow::draw_rect(uint32_t *pixels, int x, int y, int w, int h, Color color)
{
    fill_rect(pixels, x, y, w, 1, color);
    fill_rect(pixels, x, y + h - 1, w, 1, color);
    fill_rect(pixels, x, y, 1, h, color);
    fill_rect(pixels, x + w - 1, y, 1, h, color);
}

void GreeterWindow::draw_text(uint32_t *pixels, int x, int y, const std::string &text, Color color, int scale)
{
    scale = std::max(1, scale);
    int cursor = x;
    for (char ch : text) {
        const uint8_t *rows = glyph_rows(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] >> (4 - col)) & 1u) {
                    fill_rect(pixels, cursor + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        cursor += 6 * scale;
    }
}

void GreeterWindow::draw_text_centered(uint32_t *pixels, int cx, int y, const std::string &text, Color color, int scale)
{
    int width = static_cast<int>(text.size()) * 6 * std::max(1, scale);
    draw_text(pixels, cx - width / 2, y, text, color, scale);
}

void GreeterWindow::draw_text_right(uint32_t *pixels, int right, int y, const std::string &text, Color color, int scale)
{
    int width = static_cast<int>(text.size()) * 6 * std::max(1, scale);
    draw_text(pixels, right - width, y, text, color, scale);
}

void GreeterWindow::draw_user_icon(uint32_t *pixels, int x, int y, Color color)
{
    fill_rect(pixels, x + 3, y, 4, 1, color);
    fill_rect(pixels, x + 2, y + 1, 1, 3, color);
    fill_rect(pixels, x + 7, y + 1, 1, 3, color);
    fill_rect(pixels, x + 3, y + 4, 4, 1, color);
    fill_rect(pixels, x + 1, y + 7, 8, 1, color);
    fill_rect(pixels, x, y + 8, 1, 3, color);
    fill_rect(pixels, x + 9, y + 8, 1, 3, color);
}

void GreeterWindow::draw_power_icon(uint32_t *pixels, int x, int y, Color color)
{
    fill_rect(pixels, x + 4, y, 2, 5, color);
    fill_rect(pixels, x + 2, y + 3, 1, 2, color);
    fill_rect(pixels, x + 7, y + 3, 1, 2, color);
    fill_rect(pixels, x + 1, y + 5, 1, 3, color);
    fill_rect(pixels, x + 8, y + 5, 1, 3, color);
    fill_rect(pixels, x + 2, y + 8, 6, 1, color);
}

void GreeterWindow::draw_battery(uint32_t *pixels, int x, int y)
{
    draw_rect(pixels, x, y, 22, 9, kLine);
    fill_rect(pixels, x + 22, y + 3, 2, 3, kLine);
    fill_rect(pixels, x + 2, y + 2, 12, 5, kOk);
}

void GreeterWindow::draw_password(uint32_t *pixels, int x, int y)
{
    size_t dots = std::min<size_t>(password_.size(), 12);
    for (size_t i = 0; i < dots; ++i) {
        fill_rect(pixels, x + static_cast<int>(i) * 7, y + 5, 4, 4, kInk);
    }
}

void GreeterWindow::draw_frame(uint32_t *pixels)
{
    clear(pixels, kZeroBg);
    for (int y = 28; y < 138; y += 8) {
        for (int x = 8; x < kWidth; x += 8) {
            fill_rect(pixels, x, y, 1, 1, kGridDot);
        }
    }

    fill_rect(pixels, 0, 0, kWidth, 20, kPanel);
    draw_rect(pixels, 0, 0, kWidth, 20, kLine);
    draw_text(pixels, 6, 5, current_time_label(), kInk);
    draw_text(pixels, 222, 5, "LOGIN", kMuted);
    draw_text_right(pixels, 286, 5, "--%", kInk);
    draw_battery(pixels, 292, 5);
    fill_rect(pixels, 112, 21, 96, 3, kAccent);

    const int x = 60;
    const int y = 34;
    const int w = 200;
    const int h = 94;
    const int field_x = 116;
    const int field_w = 126;

    fill_rect(pixels, x + 4, y + 4, w, h, kShadow);
    fill_rect(pixels, x, y, w, h, kPanel);
    draw_rect(pixels, x, y, w, h, kLine);
    draw_text(pixels, x + 16, y + 14, "CARDPUTER ZERO", kInk);
    draw_text(pixels, x + 16, y + 27, mode_ == UiMode::UserMenu ? "SELECT USER" : "GUI LOGIN", kMuted);

    draw_text(pixels, x + 16, y + 47, "USER", kMuted);
    fill_rect(pixels, field_x, y + 40, field_w, 18, kIconWell);
    draw_rect(pixels, field_x, y + 40, field_w, 18, kLine);
    draw_user_icon(pixels, field_x + 6, y + 45, kInk);
    if (!users_.empty()) {
        draw_text(pixels, field_x + 22, y + 46, fit_identifier(users_[selected_].name, 16), kInk);
    } else {
        draw_text(pixels, field_x + 22, y + 46, "NO USERS", kWarn);
    }

    draw_text(pixels, x + 16, y + 72, "PASS", kMuted);
    fill_rect(pixels, field_x, y + 65, field_w, 18, kIconWell);
    draw_rect(pixels, field_x, y + 65, field_w, 18, error_ ? kWarn : kAccent);
    draw_password(pixels, field_x + 10, y + 69);
    if (mode_ == UiMode::Login) {
        int cursor_x = field_x + 10 + static_cast<int>(std::min<size_t>(password_.size(), 12)) * 7;
        fill_rect(pixels, cursor_x + 1, y + 69, 2, 10, kAccent);
    }

    std::string status = message_.empty() ? "PAM AUTHENTICATION" : message_;
    draw_text(pixels, x + 16, y + 86, fit_text(status, 26), error_ ? kWarn : kMuted);

    int by = kHeight - 20;
    fill_rect(pixels, 0, by, kWidth, 20, kPanel);
    draw_rect(pixels, 0, by, kWidth, 20, kLine);
    fill_rect(pixels, 0, by, 64, 20, kTaskButton);
    draw_rect(pixels, 0, by, 64, 20, kLine);
    draw_power_icon(pixels, 8, by + 5, kInk);
    draw_text(pixels, 22, by + 6, "POWER", kInk);
    draw_text(pixels, 80, by + 6, "TAB USER", kMuted);
    draw_text(pixels, 166, by + 6, "ENTER LOGIN", kInk);
    draw_text(pixels, 276, by + 6, "ESC", kMuted);

    if (mode_ == UiMode::UserMenu) {
        draw_user_menu(pixels);
    } else if (mode_ == UiMode::PowerMenu) {
        draw_power_menu(pixels);
    }
}

void GreeterWindow::draw_user_menu(uint32_t *pixels)
{
    const int x = 88;
    const int y = 63;
    const int w = 146;
    size_t visible = std::min<size_t>(users_.size(), 3);
    int rows = static_cast<int>(visible == 0 ? 1 : visible);
    int h = 17 + rows * 18 + 2;
    int start = users_.empty() ? 0 : static_cast<int>(menu_selected_) - 1;
    if (start < 0) {
        start = 0;
    }
    if (start + static_cast<int>(visible) > static_cast<int>(users_.size())) {
        start = static_cast<int>(users_.size()) - static_cast<int>(visible);
    }
    if (start < 0) {
        start = 0;
    }

    fill_rect(pixels, x + 3, y + 3, w, h, kShadow);
    fill_rect(pixels, x, y, w, h, kPanel);
    draw_rect(pixels, x, y, w, h, kLine);
    fill_rect(pixels, x, y, w, 17, kInk);
    draw_text(pixels, x + 7, y + 5, "SYSTEM USERS", kPanel);
    if (users_.empty()) {
        draw_text(pixels, x + 8, y + 25, "NO USERS", kMuted);
        return;
    }

    for (size_t row = 0; row < visible; ++row) {
        size_t user_index = static_cast<size_t>(start) + row;
        int row_y = y + 18 + static_cast<int>(row) * 18;
        bool selected = user_index == menu_selected_;
        fill_rect(pixels, x + 4, row_y, w - 8, 16, selected ? kAccent : kPanel);
        draw_text(pixels, x + 10, row_y + 4, fit_identifier(users_[user_index].name, 14), kInk);
        draw_text_right(pixels, x + w - 12, row_y + 4, std::to_string(users_[user_index].uid), selected ? kInk : kMuted);
    }
}

void GreeterWindow::draw_power_menu(uint32_t *pixels)
{
    const int x = 92;
    const int y = 52;
    const int w = 136;
    const int h = 75;
    const char *items[] = {"SHUTDOWN", "REBOOT", "CANCEL"};

    fill_rect(pixels, x + 3, y + 3, w, h, kShadow);
    fill_rect(pixels, x, y, w, h, kPanel);
    draw_rect(pixels, x, y, w, h, kLine);
    fill_rect(pixels, x, y, w, 17, kInk);
    draw_text_centered(pixels, x + w / 2, y + 5, "POWER", kPanel);
    for (int i = 0; i < 3; ++i) {
        int row_y = y + 24 + i * 18;
        bool selected = i == power_selection_;
        fill_rect(pixels, x + 10, row_y, w - 20, 15, selected ? (i == 0 ? kWarn : kAccent) : kPanel);
        draw_rect(pixels, x + 10, row_y, w - 20, 15, selected ? kLine : kSoftLine);
        draw_text_centered(pixels, x + w / 2, row_y + 4, items[i], selected ? kInk : kMuted);
    }
}

void GreeterWindow::render()
{
    if (!configured_) {
        return;
    }
    Buffer *buffer = next_buffer();
    if (buffer == nullptr) {
        return;
    }
    draw_frame(buffer->pixels);
    wl_surface_attach(surface_, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, kWidth, kHeight);
    wl_surface_commit(surface_);
    buffer->busy = true;
    dirty_ = false;
}

int GreeterWindow::run()
{
    while (running_ && wl_display_get_error(display_) == 0) {
        wl_display_dispatch_pending(display_);
        if (dirty_) {
            render();
        }
        wl_display_flush(display_);

        pollfd pfd{};
        pfd.fd = wl_display_get_fd(display_);
        pfd.events = POLLIN;
        int rc = poll(&pfd, 1, 100);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            wl_display_dispatch(display_);
        }
    }
    return session_started_ ? 0 : 1;
}

} // namespace

int main()
{
    GreeterWindow greeter;
    if (!greeter.init()) {
        return 1;
    }
    return greeter.run();
}

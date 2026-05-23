#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
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

constexpr Color kZeroBg{0xE9, 0xE4, 0xD5};
constexpr Color kPanel{0xF4, 0xF0, 0xE6};
constexpr Color kField{0xF8, 0xF4, 0xEA};
constexpr Color kInk{0x17, 0x17, 0x17};
constexpr Color kLine{0x2A, 0x2A, 0x2A};
constexpr Color kMuted{0x6E, 0x6A, 0x61};
constexpr Color kGridDot{0xC9, 0xC1, 0xAE};
constexpr Color kAccent{0xE6, 0x6A, 0x2C};
constexpr Color kWarn{0xB9, 0x4A, 0x2C};
constexpr Color kShadow{0xBD, 0xB5, 0xA4};

struct Glyph {
    char ch;
    uint8_t rows[7];
};

constexpr Glyph kGlyphs[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'!', {0x04,0x04,0x04,0x04,0x00,0x04,0x00}},
    {'"', {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}},
    {'#', {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}},
    {'$', {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
    {'%', {0x11,0x02,0x04,0x08,0x11,0x00,0x00}},
    {'&', {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}},
    {'\'', {0x04,0x04,0x00,0x00,0x00,0x00,0x00}},
    {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
    {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'*', {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}},
    {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {',', {0x00,0x00,0x00,0x00,0x04,0x04,0x08}},
    {'-', {0x00,0x00,0x00,0x0E,0x00,0x00,0x00}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x04,0x00}},
    {'/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
    {':', {0x00,0x04,0x00,0x00,0x04,0x00,0x00}},
    {';', {0x00,0x04,0x00,0x00,0x04,0x04,0x08}},
    {'<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02}},
    {'=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}},
    {'>', {0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
    {'?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
    {'@', {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}},
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
    {'[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}},
    {'\\', {0x10,0x08,0x04,0x02,0x01,0x00,0x00}},
    {']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
};

constexpr uint8_t kQuestionRows[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};

const uint8_t *glyph_rows(char ch)
{
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (const auto &glyph : kGlyphs) {
        if (glyph.ch == upper) {
            return glyph.rows;
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
    out.reserve(text.size());
    bool last_space = false;
    for (unsigned char ch : text) {
        if (ch >= 0x80) {
            continue;
        }
        if (std::isprint(ch)) {
            char c = static_cast<char>(ch);
            if (std::isspace(ch)) {
                if (!out.empty() && !last_space) {
                    out.push_back(' ');
                    last_space = true;
                }
            } else {
                out.push_back(c);
                last_space = false;
            }
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

int create_anonymous_file(size_t size)
{
    int fd = -1;
#ifdef SYS_memfd_create
    fd = static_cast<int>(syscall(SYS_memfd_create, "zero-polkit-prompt", MFD_CLOEXEC));
    if (fd >= 0) {
        if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
            return fd;
        }
        close(fd);
    }
#endif

    char name[] = "/zero-polkit-prompt-XXXXXX";
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

class PromptWindow {
public:
    struct Buffer {
        wl_buffer *buffer = nullptr;
        uint32_t *pixels = nullptr;
        bool busy = false;
    };

    bool init();
    int run();

    std::string message = "Authentication is required";
    std::string identity = "";
    std::string request = "Password:";
    std::string info = "";
    bool echo = false;

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
    void render();
    void submit();
    void cancel();
    void handle_text(const char *utf8);
    void handle_keysym(xkb_keysym_t sym);
    void clear(uint32_t *pixels, Color color);
    void fill_rect(uint32_t *pixels, int x, int y, int w, int h, Color color);
    void draw_rect(uint32_t *pixels, int x, int y, int w, int h, Color color);
    void draw_text(uint32_t *pixels, int x, int y, const std::string &text, Color color, int scale = 1);
    void draw_text_centered(uint32_t *pixels, int cx, int y, const std::string &text, Color color, int scale = 1);
    void draw_password(uint32_t *pixels, int x, int y);

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
    bool accepted_ = false;
    bool dirty_ = true;
    std::string secret_;
};

const wl_registry_listener kRegistryListener{PromptWindow::registry_global, PromptWindow::registry_remove};
const xdg_wm_base_listener kWmListener{PromptWindow::wm_ping};
const xdg_surface_listener kSurfaceListener{PromptWindow::surface_configure};
const xdg_toplevel_listener kToplevelListener{
    PromptWindow::toplevel_configure,
    PromptWindow::toplevel_close,
    nullptr,
    nullptr,
};
const wl_seat_listener kSeatListener{PromptWindow::seat_capabilities, PromptWindow::seat_name};
const wl_keyboard_listener kKeyboardListener{
    PromptWindow::keyboard_keymap,
    PromptWindow::keyboard_enter,
    PromptWindow::keyboard_leave,
    PromptWindow::keyboard_key,
    PromptWindow::keyboard_modifiers,
    PromptWindow::keyboard_repeat,
};
const wl_buffer_listener kBufferListener{PromptWindow::buffer_release};

void PromptWindow::registry_global(void *data, wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    auto *self = static_cast<PromptWindow *>(data);
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

void PromptWindow::wm_ping(void *, xdg_wm_base *wm, uint32_t serial)
{
    xdg_wm_base_pong(wm, serial);
}

void PromptWindow::surface_configure(void *data, xdg_surface *surface, uint32_t serial)
{
    auto *self = static_cast<PromptWindow *>(data);
    xdg_surface_ack_configure(surface, serial);
    self->configured_ = true;
    self->dirty_ = true;
}

void PromptWindow::toplevel_close(void *data, xdg_toplevel *)
{
    static_cast<PromptWindow *>(data)->cancel();
}

void PromptWindow::seat_capabilities(void *data, wl_seat *, uint32_t capabilities)
{
    auto *self = static_cast<PromptWindow *>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && self->keyboard_ == nullptr) {
        self->keyboard_ = wl_seat_get_keyboard(self->seat_);
        wl_keyboard_add_listener(self->keyboard_, &kKeyboardListener, self);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && self->keyboard_ != nullptr) {
        wl_keyboard_destroy(self->keyboard_);
        self->keyboard_ = nullptr;
    }
}

void PromptWindow::keyboard_keymap(void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size)
{
    auto *self = static_cast<PromptWindow *>(data);
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

void PromptWindow::keyboard_key(void *data, wl_keyboard *, uint32_t, uint32_t,
                                uint32_t key, uint32_t state)
{
    auto *self = static_cast<PromptWindow *>(data);
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || self->xkb_state_ == nullptr) {
        return;
    }

    xkb_keycode_t keycode = static_cast<xkb_keycode_t>(key + 8);
    xkb_keysym_t sym = xkb_state_key_get_one_sym(self->xkb_state_, keycode);
    self->handle_keysym(sym);
    if (!self->running_) {
        return;
    }

    char utf8[16] = {};
    int len = xkb_state_key_get_utf8(self->xkb_state_, keycode, utf8, sizeof(utf8));
    if (len > 0) {
        self->handle_text(utf8);
    }
}

void PromptWindow::keyboard_modifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed,
                                      uint32_t latched, uint32_t locked, uint32_t group)
{
    auto *self = static_cast<PromptWindow *>(data);
    if (self->xkb_state_ != nullptr) {
        xkb_state_update_mask(self->xkb_state_, depressed, latched, locked, 0, 0, group);
    }
}

void PromptWindow::buffer_release(void *data, wl_buffer *)
{
    static_cast<Buffer *>(data)->busy = false;
}

bool PromptWindow::create_buffer(Buffer &buffer)
{
    int fd = create_anonymous_file(kBufferSize);
    if (fd < 0) {
        std::cerr << "zero-polkit-prompt-wayland: cannot create shm file: " << std::strerror(errno) << "\n";
        return false;
    }

    void *data = mmap(nullptr, kBufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        std::cerr << "zero-polkit-prompt-wayland: cannot mmap shm buffer: " << std::strerror(errno) << "\n";
        close(fd);
        return false;
    }

    wl_shm_pool *pool = wl_shm_create_pool(shm_, fd, kBufferSize);
    buffer.buffer = wl_shm_pool_create_buffer(pool, 0, kWidth, kHeight, kStride, WL_SHM_FORMAT_XRGB8888);
    buffer.pixels = static_cast<uint32_t *>(data);
    buffer.busy = false;
    wl_buffer_add_listener(buffer.buffer, &kBufferListener, &buffer);
    wl_shm_pool_destroy(pool);
    close(fd);
    return true;
}

PromptWindow::Buffer *PromptWindow::next_buffer()
{
    for (auto &buffer : buffers_) {
        if (!buffer.busy) {
            return &buffer;
        }
    }
    return nullptr;
}

bool PromptWindow::init()
{
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        std::cerr << "zero-polkit-prompt-wayland: cannot connect to Wayland display\n";
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!compositor_ || !shm_ || !wm_) {
        std::cerr << "zero-polkit-prompt-wayland: missing compositor/shm/xdg-shell globals\n";
        return false;
    }

    surface_ = wl_compositor_create_surface(compositor_);
    xdg_surface_ = xdg_wm_base_get_xdg_surface(wm_, surface_);
    xdg_surface_add_listener(xdg_surface_, &kSurfaceListener, this);
    toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
    xdg_toplevel_set_title(toplevel_, "Zero Authorization");
    xdg_toplevel_set_app_id(toplevel_, "cardputer-zero-polkit");
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

void PromptWindow::submit()
{
    accepted_ = true;
    running_ = false;
}

void PromptWindow::cancel()
{
    accepted_ = false;
    running_ = false;
}

void PromptWindow::handle_keysym(xkb_keysym_t sym)
{
    switch (sym) {
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            submit();
            return;
        case XKB_KEY_Escape:
            cancel();
            return;
        case XKB_KEY_BackSpace:
        case XKB_KEY_Delete:
            if (!secret_.empty()) {
                secret_.pop_back();
                dirty_ = true;
            }
            return;
        default:
            return;
    }
}

void PromptWindow::handle_text(const char *utf8)
{
    if (utf8 == nullptr || utf8[0] == '\0') {
        return;
    }
    unsigned char ch = static_cast<unsigned char>(utf8[0]);
    if (utf8[1] == '\0' && std::isprint(ch) && secret_.size() < 128) {
        secret_.push_back(static_cast<char>(ch));
        dirty_ = true;
    }
}

void PromptWindow::clear(uint32_t *pixels, Color color)
{
    std::fill(pixels, pixels + (kWidth * kHeight), pixel(color));
}

void PromptWindow::fill_rect(uint32_t *pixels, int x, int y, int w, int h, Color color)
{
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(kWidth, x + w);
    int y1 = std::min(kHeight, y + h);
    uint32_t value = pixel(color);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            pixels[yy * kWidth + xx] = value;
        }
    }
}

void PromptWindow::draw_rect(uint32_t *pixels, int x, int y, int w, int h, Color color)
{
    fill_rect(pixels, x, y, w, 1, color);
    fill_rect(pixels, x, y + h - 1, w, 1, color);
    fill_rect(pixels, x, y, 1, h, color);
    fill_rect(pixels, x + w - 1, y, 1, h, color);
}

void PromptWindow::draw_text(uint32_t *pixels, int x, int y, const std::string &text, Color color, int scale)
{
    int cursor = x;
    uint32_t value = pixel(color);
    for (char ch : text) {
        const uint8_t *rows = glyph_rows(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] >> (4 - col)) & 1u) {
                    fill_rect(pixels, cursor + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        (void)value;
        cursor += 6 * scale;
    }
}

void PromptWindow::draw_text_centered(uint32_t *pixels, int cx, int y, const std::string &text, Color color, int scale)
{
    int width = static_cast<int>(text.size()) * 6 * scale;
    draw_text(pixels, cx - width / 2, y, text, color, scale);
}

void PromptWindow::draw_password(uint32_t *pixels, int x, int y)
{
    if (echo) {
        draw_text(pixels, x, y, fit_text(secret_, 34), kInk, 1);
        return;
    }

    size_t dots = std::min<size_t>(secret_.size(), 32);
    for (size_t i = 0; i < dots; ++i) {
        fill_rect(pixels, x + static_cast<int>(i) * 7, y + 3, 4, 4, kInk);
    }
}

void PromptWindow::render()
{
    if (!configured_) {
        return;
    }
    Buffer *buffer = next_buffer();
    if (buffer == nullptr) {
        return;
    }
    uint32_t *pixels = buffer->pixels;
    clear(pixels, kZeroBg);
    for (int y = 25; y < 146; y += 10) {
        for (int x = 12; x < 308; x += 10) {
            fill_rect(pixels, x, y, 1, 1, kGridDot);
        }
    }

    fill_rect(pixels, 0, 0, kWidth, 20, kPanel);
    draw_rect(pixels, 0, 0, kWidth, 20, kLine);
    draw_text(pixels, 7, 6, "ZERO AUTH", kInk, 1);
    draw_text(pixels, 240, 6, "POLKIT", kMuted, 1);
    fill_rect(pixels, 112, 21, 96, 3, kAccent);

    int px = 27;
    int py = 32;
    int pw = 266;
    int ph = 102;
    fill_rect(pixels, px + 4, py + 4, pw, ph, kShadow);
    fill_rect(pixels, px, py, pw, ph, kPanel);
    draw_rect(pixels, px, py, pw, ph, kLine);
    fill_rect(pixels, px, py, pw, 18, kInk);
    draw_text_centered(pixels, px + pw / 2, py + 5, "AUTHORIZATION", kPanel, 1);

    draw_text(pixels, px + 12, py + 29, fit_text(message, 38), kInk, 1);
    draw_text(pixels, px + 12, py + 43, fit_text(identity, 38), kMuted, 1);
    draw_text(pixels, px + 12, py + 57, fit_text(request, 38), kMuted, 1);

    fill_rect(pixels, px + 12, py + 70, pw - 24, 18, kField);
    draw_rect(pixels, px + 12, py + 70, pw - 24, 18, kLine);
    draw_password(pixels, px + 18, py + 76);
    int cursor_x = px + 18 + static_cast<int>(std::min<size_t>(secret_.size(), 32)) * 7;
    fill_rect(pixels, cursor_x, py + 74, 2, 10, kAccent);

    if (!info.empty()) {
        draw_text(pixels, px + 12, py + 93, fit_text(info, 38), kWarn, 1);
    } else {
        draw_text(pixels, px + 12, py + 93, "ENTER OK   ESC CANCEL", kMuted, 1);
    }

    fill_rect(pixels, 0, 150, kWidth, 20, kPanel);
    draw_rect(pixels, 0, 150, kWidth, 20, kLine);
    fill_rect(pixels, 0, 150, 86, 20, kField);
    draw_rect(pixels, 0, 150, 86, 20, kLine);
    draw_text(pixels, 13, 157, "ENTER AUTH", kInk, 1);
    draw_text(pixels, 234, 157, "ESC CANCEL", kMuted, 1);

    wl_surface_attach(surface_, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, kWidth, kHeight);
    wl_surface_commit(surface_);
    buffer->busy = true;
    dirty_ = false;
}

int PromptWindow::run()
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

    if (accepted_) {
        std::cout << secret_ << "\n";
        std::cout.flush();
        std::fill(secret_.begin(), secret_.end(), '\0');
        return 0;
    }
    std::fill(secret_.begin(), secret_.end(), '\0');
    return 1;
}

void usage()
{
    std::cerr << "Usage: zero-polkit-prompt-wayland --message TEXT --identity TEXT --request TEXT --info TEXT [--echo|--secret]\n";
}

} // namespace

int main(int argc, char **argv)
{
    PromptWindow prompt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next_value = [&](std::string *out) -> bool {
            if (i + 1 >= argc) {
                return false;
            }
            *out = argv[++i];
            return true;
        };

        if (arg == "--message") {
            if (!next_value(&prompt.message)) {
                usage();
                return 2;
            }
        } else if (arg == "--identity") {
            if (!next_value(&prompt.identity)) {
                usage();
                return 2;
            }
        } else if (arg == "--request") {
            if (!next_value(&prompt.request)) {
                usage();
                return 2;
            }
        } else if (arg == "--info") {
            if (!next_value(&prompt.info)) {
                usage();
                return 2;
            }
        } else if (arg == "--echo") {
            prompt.echo = true;
        } else if (arg == "--secret") {
            prompt.echo = false;
        } else {
            usage();
            return 2;
        }
    }

    if (!prompt.init()) {
        return 1;
    }
    return prompt.run();
}

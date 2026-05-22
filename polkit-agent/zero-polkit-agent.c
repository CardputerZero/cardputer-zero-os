#include "../common/zero_framebuffer.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <linux/input.h>
#include <polkit/polkit.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE 1
#include <polkitagent/polkitagent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#define FB_WIDTH ZERO_FB_LOGICAL_WIDTH
#define FB_HEIGHT ZERO_FB_LOGICAL_HEIGHT
#define MAX_INPUTS 32
#define MAX_SECRET 256
#define MAX_STOPPED_PROCS 64

#define RGB565_CONST(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xF8) << 3) | ((b) >> 3))

struct input_set {
    int fds[MAX_INPUTS];
    int count;
    int shift;
};

struct screen_lease {
    pid_t pids[MAX_STOPPED_PROCS];
    int count;
};

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

static const uint16_t COLOR_BG = RGB565_CONST(0xE9, 0xE4, 0xD5);
static const uint16_t COLOR_PANEL = RGB565_CONST(0xF4, 0xF0, 0xE6);
static const uint16_t COLOR_FIELD = RGB565_CONST(0xF8, 0xF4, 0xEA);
static const uint16_t COLOR_INK = RGB565_CONST(0x17, 0x17, 0x17);
static const uint16_t COLOR_LINE = RGB565_CONST(0x2A, 0x2A, 0x2A);
static const uint16_t COLOR_MUTED = RGB565_CONST(0x6E, 0x6A, 0x61);
static const uint16_t COLOR_ACCENT = RGB565_CONST(0xE6, 0x6A, 0x2C);
static const uint16_t COLOR_WARN = RGB565_CONST(0xB9, 0x4A, 0x2C);
static const uint16_t COLOR_SHADOW = RGB565_CONST(0xBD, 0xB5, 0xA4);
static const uint16_t COLOR_GRID = RGB565_CONST(0xC9, 0xC1, 0xAE);

static const uint8_t font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08},{0x08,0x1C,0x2A,0x08,0x08}
};

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

static int parse_pid_name(const char *name, pid_t *pid)
{
    char *end = NULL;
    long value;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    errno = 0;
    value = strtol(name, &end, 10);
    if (errno != 0 || end == NULL || *end != '\0' || value <= 1) {
        return 0;
    }

    *pid = (pid_t)value;
    return 1;
}

static int process_belongs_to_user(pid_t pid, uid_t uid)
{
    char proc_path[64];
    struct stat st;

    snprintf(proc_path, sizeof(proc_path), "/proc/%ld", (long)pid);
    if (stat(proc_path, &st) != 0) {
        return 0;
    }

    return st.st_uid == uid;
}

static int process_has_framebuffer_open(pid_t pid)
{
    char fd_dir_path[64];
    DIR *fd_dir;
    struct dirent *entry;

    snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%ld/fd", (long)pid);
    fd_dir = opendir(fd_dir_path);
    if (fd_dir == NULL) {
        return 0;
    }

    while ((entry = readdir(fd_dir)) != NULL) {
        char link_path[512];
        char target[256];
        ssize_t n;

        if (entry->d_name[0] == '.') {
            continue;
        }

        snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir_path, entry->d_name);
        n = readlink(link_path, target, sizeof(target) - 1);
        if (n < 0) {
            continue;
        }
        target[n] = '\0';

        if (zero_fb_path_is_internal_device(target)) {
            closedir(fd_dir);
            return 1;
        }
    }

    closedir(fd_dir);
    return 0;
}

static void screen_lease_acquire(struct screen_lease *lease)
{
    DIR *proc_dir;
    struct dirent *entry;
    uid_t uid = getuid();
    pid_t self = getpid();

    memset(lease, 0, sizeof(*lease));
    proc_dir = opendir("/proc");
    if (proc_dir == NULL) {
        return;
    }

    while ((entry = readdir(proc_dir)) != NULL) {
        pid_t pid = 0;

        if (!parse_pid_name(entry->d_name, &pid) || pid == self) {
            continue;
        }
        if (!process_belongs_to_user(pid, uid)) {
            continue;
        }
        if (!process_has_framebuffer_open(pid)) {
            continue;
        }
        if (lease->count >= MAX_STOPPED_PROCS) {
            break;
        }

        if (kill(pid, SIGSTOP) == 0) {
            lease->pids[lease->count++] = pid;
        }
    }

    closedir(proc_dir);
    if (lease->count > 0) {
        g_print("zero-polkit-agent: paused %d framebuffer owner(s)\n", lease->count);
        usleep(50000);
    }
}

static void screen_lease_release(struct screen_lease *lease)
{
    for (int i = lease->count - 1; i >= 0; i--) {
        if (lease->pids[i] > 1) {
            kill(lease->pids[i], SIGCONT);
        }
    }

    if (lease->count > 0) {
        g_print("zero-polkit-agent: resumed %d framebuffer owner(s)\n", lease->count);
    }

    memset(lease, 0, sizeof(*lease));
}

static int fb_open(struct zero_framebuffer *fb)
{
    return zero_fb_open(fb);
}

static void fb_close(struct zero_framebuffer *fb)
{
    zero_fb_close(fb);
}

static void put_pixel(struct zero_framebuffer *fb, int x, int y, uint16_t color)
{
    zero_fb_put_pixel(fb, x, y, color);
}

static void fill_rect(struct zero_framebuffer *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            put_pixel(fb, xx, yy, color);
        }
    }
}

static void stroke_rect(struct zero_framebuffer *fb, int x, int y, int w, int h, uint16_t color)
{
    fill_rect(fb, x, y, w, 1, color);
    fill_rect(fb, x, y + h - 1, w, 1, color);
    fill_rect(fb, x, y, 1, h, color);
    fill_rect(fb, x + w - 1, y, 1, h, color);
}

static int text_width(const char *text, int scale)
{
    return text == NULL ? 0 : (int)strlen(text) * 6 * scale;
}

static void draw_char(struct zero_framebuffer *fb, int x, int y, char ch, uint16_t color, int scale)
{
    unsigned char uch = (unsigned char)ch;
    if (uch < 32 || uch > 127) {
        ch = '?';
        uch = (unsigned char)ch;
    }

    const uint8_t *glyph = font5x7[(int)uch - 32];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (glyph[col] & (1 << row)) {
                fill_rect(fb, x + (col * scale), y + (row * scale), scale, scale, color);
            }
        }
    }
}

static void draw_text(struct zero_framebuffer *fb, int x, int y, const char *text, uint16_t color, int scale)
{
    int cursor = x;
    for (const char *p = text != NULL ? text : ""; *p; p++) {
        draw_char(fb, cursor, y, *p, color, scale);
        cursor += 6 * scale;
    }
}

static void draw_text_clipped(struct zero_framebuffer *fb, int x, int y, int max_w, const char *text, uint16_t color)
{
    char buf[80];
    int max_chars = max_w / 6;
    if (max_chars <= 0) {
        return;
    }
    snprintf(buf, sizeof(buf), "%s", text != NULL ? text : "");
    if ((int)strlen(buf) > max_chars) {
        if (max_chars > 3) {
            buf[max_chars - 3] = '.';
            buf[max_chars - 2] = '.';
            buf[max_chars - 1] = '.';
            buf[max_chars] = '\0';
        } else {
            buf[max_chars] = '\0';
        }
    }
    draw_text(fb, x, y, buf, color, 1);
}

static void draw_text_centered(struct zero_framebuffer *fb, int cx, int y, const char *text, uint16_t color, int scale)
{
    draw_text(fb, cx - text_width(text, scale) / 2, y, text, color, scale);
}

static void draw_background(struct zero_framebuffer *fb)
{
    fill_rect(fb, 0, 0, FB_WIDTH, FB_HEIGHT, COLOR_BG);
    for (int y = 26; y < FB_HEIGHT - 20; y += 14) {
        for (int x = 9; x < FB_WIDTH; x += 18) {
            put_pixel(fb, x, y, COLOR_GRID);
        }
    }
}

static void draw_prompt(struct zero_framebuffer *fb,
                        const char *message,
                        const char *identity,
                        const char *request,
                        const char *info,
                        const char *input,
                        int echo)
{
    int x = 34;
    int y = 31;
    int w = 252;
    int h = 105;
    size_t len = strlen(input != NULL ? input : "");

    draw_background(fb);
    fill_rect(fb, 0, 0, FB_WIDTH, 20, COLOR_PANEL);
    stroke_rect(fb, 0, 0, FB_WIDTH, 20, COLOR_LINE);
    draw_text(fb, 6, 6, "ZERO AUTH", COLOR_INK, 1);
    fill_rect(fb, 112, 21, 96, 3, COLOR_ACCENT);

    fill_rect(fb, x + 4, y + 4, w, h, COLOR_SHADOW);
    fill_rect(fb, x, y, w, h, COLOR_PANEL);
    stroke_rect(fb, x, y, w, h, COLOR_LINE);
    fill_rect(fb, x, y, w, 18, COLOR_INK);
    draw_text_centered(fb, x + w / 2, y + 5, "POLKIT AUTHORIZATION", COLOR_PANEL, 1);

    draw_text_clipped(fb, x + 12, y + 29, w - 24, message, COLOR_INK);
    draw_text_clipped(fb, x + 12, y + 43, w - 24, identity, COLOR_MUTED);
    draw_text_clipped(fb, x + 12, y + 57, w - 24, request, COLOR_MUTED);

    fill_rect(fb, x + 12, y + 70, w - 24, 18, COLOR_FIELD);
    stroke_rect(fb, x + 12, y + 70, w - 24, 18, COLOR_LINE);
    if (echo) {
        draw_text_clipped(fb, x + 18, y + 76, w - 36, input, COLOR_INK);
    } else {
        for (size_t i = 0; i < len && i < 26; i++) {
            fill_rect(fb, x + 18 + (int)i * 7, y + 76, 4, 4, COLOR_INK);
        }
    }
    int cursor_x = x + 18 + (int)(len > 26 ? 26 : len) * 7;
    fill_rect(fb, cursor_x, y + 74, 2, 10, COLOR_ACCENT);

    draw_text_clipped(fb, x + 12, y + 93, w - 24, info != NULL ? info : "ENTER OK   ESC CANCEL", info ? COLOR_WARN : COLOR_MUTED);

    fill_rect(fb, 0, FB_HEIGHT - 20, FB_WIDTH, 20, COLOR_PANEL);
    stroke_rect(fb, 0, FB_HEIGHT - 20, FB_WIDTH, 20, COLOR_LINE);
    draw_text(fb, 12, FB_HEIGHT - 14, "ENTER AUTHORIZE", COLOR_INK, 1);
    draw_text(fb, 232, FB_HEIGHT - 14, "ESC CANCEL", COLOR_MUTED, 1);
}

static void input_close(struct input_set *inputs)
{
    for (int i = 0; i < inputs->count; i++) {
        ioctl(inputs->fds[i], EVIOCGRAB, 0);
        close(inputs->fds[i]);
    }
    memset(inputs, 0, sizeof(*inputs));
}

static int input_open(struct input_set *inputs)
{
    memset(inputs, 0, sizeof(*inputs));
    for (int i = 0; i < 32 && inputs->count < MAX_INPUTS; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        ioctl(fd, EVIOCGRAB, 1);
        inputs->fds[inputs->count++] = fd;
    }

    return inputs->count > 0 ? 0 : -1;
}

static char key_to_char(unsigned short code, int shift)
{
    static const char normal[128] = {
        [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4', [KEY_5] = '5',
        [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9', [KEY_0] = '0',
        [KEY_Q] = 'q', [KEY_W] = 'w', [KEY_E] = 'e', [KEY_R] = 'r', [KEY_T] = 't',
        [KEY_Y] = 'y', [KEY_U] = 'u', [KEY_I] = 'i', [KEY_O] = 'o', [KEY_P] = 'p',
        [KEY_A] = 'a', [KEY_S] = 's', [KEY_D] = 'd', [KEY_F] = 'f', [KEY_G] = 'g',
        [KEY_H] = 'h', [KEY_J] = 'j', [KEY_K] = 'k', [KEY_L] = 'l', [KEY_Z] = 'z',
        [KEY_X] = 'x', [KEY_C] = 'c', [KEY_V] = 'v', [KEY_B] = 'b', [KEY_N] = 'n',
        [KEY_M] = 'm', [KEY_SPACE] = ' ', [KEY_MINUS] = '-', [KEY_EQUAL] = '=',
        [KEY_LEFTBRACE] = '[', [KEY_RIGHTBRACE] = ']', [KEY_BACKSLASH] = '\\',
        [KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'', [KEY_GRAVE] = '`',
        [KEY_COMMA] = ',', [KEY_DOT] = '.', [KEY_SLASH] = '/'
    };
    static const char shifted[128] = {
        [KEY_1] = '!', [KEY_2] = '@', [KEY_3] = '#', [KEY_4] = '$', [KEY_5] = '%',
        [KEY_6] = '^', [KEY_7] = '&', [KEY_8] = '*', [KEY_9] = '(', [KEY_0] = ')',
        [KEY_Q] = 'Q', [KEY_W] = 'W', [KEY_E] = 'E', [KEY_R] = 'R', [KEY_T] = 'T',
        [KEY_Y] = 'Y', [KEY_U] = 'U', [KEY_I] = 'I', [KEY_O] = 'O', [KEY_P] = 'P',
        [KEY_A] = 'A', [KEY_S] = 'S', [KEY_D] = 'D', [KEY_F] = 'F', [KEY_G] = 'G',
        [KEY_H] = 'H', [KEY_J] = 'J', [KEY_K] = 'K', [KEY_L] = 'L', [KEY_Z] = 'Z',
        [KEY_X] = 'X', [KEY_C] = 'C', [KEY_V] = 'V', [KEY_B] = 'B', [KEY_N] = 'N',
        [KEY_M] = 'M', [KEY_SPACE] = ' ', [KEY_MINUS] = '_', [KEY_EQUAL] = '+',
        [KEY_LEFTBRACE] = '{', [KEY_RIGHTBRACE] = '}', [KEY_BACKSLASH] = '|',
        [KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '"', [KEY_GRAVE] = '~',
        [KEY_COMMA] = '<', [KEY_DOT] = '>', [KEY_SLASH] = '?'
    };

    if (code >= 128) {
        return '\0';
    }
    return shift ? shifted[code] : normal[code];
}

static int read_key(struct input_set *inputs, unsigned short *code, int timeout_ms)
{
    for (;;) {
        fd_set rfds;
        int max_fd = -1;
        struct timeval tv;
        struct timeval *tvp = NULL;

        FD_ZERO(&rfds);
        for (int i = 0; i < inputs->count; i++) {
            FD_SET(inputs->fds[i], &rfds);
            if (inputs->fds[i] > max_fd) {
                max_fd = inputs->fds[i];
            }
        }

        if (timeout_ms >= 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            tvp = &tv;
        }

        int ready = select(max_fd + 1, &rfds, NULL, NULL, tvp);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (ready == 0) {
            return 1;
        }

        for (int i = 0; i < inputs->count; i++) {
            if (!FD_ISSET(inputs->fds[i], &rfds)) {
                continue;
            }
            struct input_event ev;
            ssize_t n = read(inputs->fds[i], &ev, sizeof(ev));
            if (n != (ssize_t)sizeof(ev) || ev.type != EV_KEY) {
                continue;
            }
            if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                inputs->shift = ev.value != 0;
                continue;
            }
            if (ev.value == 1) {
                *code = ev.code;
                return 0;
            }
        }
    }
}

static char *prompt_for_response(const char *message,
                                 const char *identity,
                                 const char *request,
                                 const char *info,
                                 int echo)
{
    struct zero_framebuffer fb;
    struct input_set inputs;
    struct screen_lease lease;
    char secret[MAX_SECRET] = "";
    char *out = NULL;

    screen_lease_acquire(&lease);
    if (fb_open(&fb) != 0) {
        g_printerr("zero-polkit-agent: failed to open internal framebuffer: %s\n", strerror(errno));
        screen_lease_release(&lease);
        return NULL;
    }
    if (input_open(&inputs) != 0) {
        g_printerr("zero-polkit-agent: failed to open input devices: %s\n", strerror(errno));
        fb_close(&fb);
        screen_lease_release(&lease);
        return NULL;
    }

    draw_prompt(&fb, message, identity, request, info, secret, echo);

    for (;;) {
        unsigned short code = 0;
        int status = read_key(&inputs, &code, 250);
        if (status < 0) {
            input_close(&inputs);
            fb_close(&fb);
            return NULL;
        }
        if (status > 0) {
            continue;
        }

        if (code == KEY_ENTER || code == KEY_KPENTER) {
            out = strdup(secret);
            secure_clear(secret);
            input_close(&inputs);
            fb_close(&fb);
            screen_lease_release(&lease);
            return out;
        }
        if (code == KEY_ESC) {
            secure_clear(secret);
            input_close(&inputs);
            fb_close(&fb);
            screen_lease_release(&lease);
            return NULL;
        }
        if (code == KEY_BACKSPACE || code == KEY_DELETE) {
            size_t len = strlen(secret);
            if (len > 0) {
                secret[len - 1] = '\0';
            }
            draw_prompt(&fb, message, identity, request, info, secret, echo);
            continue;
        }

        char ch = key_to_char(code, inputs.shift);
        if (ch != '\0') {
            size_t len = strlen(secret);
            if (len + 1 < sizeof(secret)) {
                secret[len] = ch;
                secret[len + 1] = '\0';
            }
            draw_prompt(&fb, message, identity, request, info, secret, echo);
        }
    }
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

    char *response = prompt_for_response(auth->message,
                                         auth->identity_text,
                                         request != NULL ? request : "Password:",
                                         auth->info_text,
                                         echo_on ? 1 : 0);
    if (response == NULL) {
        g_printerr("zero-polkit-agent: authentication prompt cancelled or unavailable\n");
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
        g_print("zero-polkit-agent 0.1\n");
        return 0;
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

    g_print("zero-polkit-agent: registered for current user session\n");
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

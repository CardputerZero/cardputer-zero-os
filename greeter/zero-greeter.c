#include "pam_auth.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/input.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_USERS 64
#define MAX_PASSWORD 256
#define MAX_INPUTS 32
#define FB_DEVICE "/dev/fb0"
#define FB_WIDTH 320
#define FB_HEIGHT 170
#define FB_BYTES_PER_PIXEL 2
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * FB_BYTES_PER_PIXEL)

struct user_entry {
    char name[256];
    uid_t uid;
    gid_t gid;
    char home[512];
    char shell[256];
};

struct framebuffer {
    int fd;
    uint16_t *pixels;
    size_t size;
    int ok;
};

struct input_set {
    int fds[MAX_INPUTS];
    int count;
    int shift;
};

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xF8) << 3) | (b >> 3));
}

static const uint16_t COLOR_PANEL = 0x10A5;
static const uint16_t COLOR_PANEL_2 = 0x18E7;
static const uint16_t COLOR_TEXT = 0xE79C;
static const uint16_t COLOR_MUTED = 0x8C71;
static const uint16_t COLOR_ACCENT = 0x4D7F;
static const uint16_t COLOR_ERROR = 0xF9A6;
static const uint16_t COLOR_FIELD = 0x0000;
static const uint16_t COLOR_BUTTON = 0x3D5F;

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

static int shell_is_login_capable(const char *shell)
{
    if (shell == NULL || shell[0] == '\0') {
        return 1;
    }
    return strstr(shell, "nologin") == NULL && strstr(shell, "false") == NULL;
}

static int home_is_normal(const char *home)
{
    return home != NULL && strncmp(home, "/home/", 6) == 0;
}

static int load_users(struct user_entry users[MAX_USERS])
{
    struct passwd *pw;
    int count = 0;

    setpwent();
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000) {
            continue;
        }
        if (!home_is_normal(pw->pw_dir) || !shell_is_login_capable(pw->pw_shell)) {
            continue;
        }
        if (count >= MAX_USERS) {
            break;
        }

        snprintf(users[count].name, sizeof(users[count].name), "%s", pw->pw_name);
        users[count].uid = pw->pw_uid;
        users[count].gid = pw->pw_gid;
        snprintf(users[count].home, sizeof(users[count].home), "%s", pw->pw_dir);
        snprintf(users[count].shell, sizeof(users[count].shell), "%s", pw->pw_shell);
        count++;
    }
    endpwent();

    return count;
}

static int fb_open(struct framebuffer *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = open(FB_DEVICE, O_RDWR);
    if (fb->fd < 0) {
        return -1;
    }

    fb->size = FB_SIZE;
    fb->pixels = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->pixels == MAP_FAILED) {
        close(fb->fd);
        fb->fd = -1;
        fb->pixels = NULL;
        return -1;
    }

    fb->ok = 1;
    return 0;
}

static void fb_close(struct framebuffer *fb)
{
    if (fb->pixels != NULL) {
        munmap(fb->pixels, fb->size);
    }
    if (fb->fd >= 0) {
        close(fb->fd);
    }
    memset(fb, 0, sizeof(*fb));
}

static void put_pixel(struct framebuffer *fb, int x, int y, uint16_t color)
{
    if (!fb->ok || x < 0 || y < 0 || x >= FB_WIDTH || y >= FB_HEIGHT) {
        return;
    }
    fb->pixels[(y * FB_WIDTH) + x] = color;
}

static void fill_rect(struct framebuffer *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            put_pixel(fb, xx, yy, color);
        }
    }
}

static void stroke_rect(struct framebuffer *fb, int x, int y, int w, int h, uint16_t color)
{
    fill_rect(fb, x, y, w, 1, color);
    fill_rect(fb, x, y + h - 1, w, 1, color);
    fill_rect(fb, x, y, 1, h, color);
    fill_rect(fb, x + w - 1, y, 1, h, color);
}

static void draw_char(struct framebuffer *fb, int x, int y, char ch, uint16_t color, int scale)
{
    unsigned char uch = (unsigned char)ch;
    if (uch < 32 || uch > 127) {
        ch = '?';
        uch = (unsigned char)ch;
    }

    const uint8_t *glyph = font5x7[(int)uch - 32];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if ((glyph[col] >> row) & 1) {
                fill_rect(fb, x + (col * scale), y + (row * scale), scale, scale, color);
            }
        }
    }
}

static void draw_text(struct framebuffer *fb, int x, int y, const char *text, uint16_t color, int scale)
{
    int cursor = x;
    for (const char *p = text; p != NULL && *p != '\0'; p++) {
        draw_char(fb, cursor, y, *p, color, scale);
        cursor += 6 * scale;
    }
}

static void draw_password_dots(struct framebuffer *fb, int x, int y, size_t count)
{
    size_t max_dots = count > 18 ? 18 : count;
    for (size_t i = 0; i < max_dots; i++) {
        fill_rect(fb, x + (int)i * 9, y + 5, 5, 5, COLOR_TEXT);
    }
}

static void draw_background(struct framebuffer *fb)
{
    for (int y = 0; y < FB_HEIGHT; y++) {
        uint16_t line = rgb565((uint8_t)(7 + y / 7), (uint8_t)(12 + y / 9), (uint8_t)(22 + y / 4));
        fill_rect(fb, 0, y, FB_WIDTH, 1, line);
    }
    fill_rect(fb, 0, 0, FB_WIDTH, 4, COLOR_ACCENT);
}

static void draw_frame(struct framebuffer *fb,
                       const struct user_entry *users,
                       int user_count,
                       int selected,
                       const char *password,
                       const char *message,
                       int error)
{
    char line[128];

    draw_background(fb);
    fill_rect(fb, 12, 12, 296, 146, COLOR_PANEL);
    stroke_rect(fb, 12, 12, 296, 146, COLOR_ACCENT);
    fill_rect(fb, 14, 14, 292, 20, COLOR_PANEL_2);

    draw_text(fb, 80, 20, "Cardputer Zero", COLOR_TEXT, 2);

    draw_text(fb, 28, 48, "USER", COLOR_MUTED, 1);
    fill_rect(fb, 80, 42, 208, 22, COLOR_FIELD);
    stroke_rect(fb, 80, 42, 208, 22, COLOR_MUTED);
    if (user_count > 0) {
        snprintf(line, sizeof(line), "%.24s", users[selected].name);
        draw_text(fb, 90, 50, line, COLOR_TEXT, 1);
    } else {
        draw_text(fb, 90, 50, "no normal users", COLOR_ERROR, 1);
    }

    draw_text(fb, 28, 80, "PASS", COLOR_MUTED, 1);
    fill_rect(fb, 80, 74, 208, 22, COLOR_FIELD);
    stroke_rect(fb, 80, 74, 208, 22, COLOR_ACCENT);
    draw_password_dots(fb, 90, 79, strlen(password));

    fill_rect(fb, 104, 108, 112, 22, COLOR_BUTTON);
    stroke_rect(fb, 104, 108, 112, 22, COLOR_ACCENT);
    draw_text(fb, 136, 116, "LOGIN", COLOR_TEXT, 1);

    if (message != NULL && message[0] != '\0') {
        draw_text(fb, 24, 144, message, error ? COLOR_ERROR : COLOR_MUTED, 1);
    } else {
        draw_text(fb, 24, 144, "TAB user  ENTER login  ESC power", COLOR_MUTED, 1);
    }
}

static void input_close(struct input_set *inputs)
{
    for (int i = 0; i < inputs->count; i++) {
        close(inputs->fds[i]);
    }
    memset(inputs, 0, sizeof(*inputs));
}

static int input_open(struct input_set *inputs)
{
    memset(inputs, 0, sizeof(*inputs));

    for (int i = 0; i < MAX_INPUTS; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
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

static int read_key(struct input_set *inputs, unsigned short *code, int *pressed, int timeout_ms)
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
            if (n != (ssize_t)sizeof(ev)) {
                continue;
            }
            if (ev.type != EV_KEY) {
                continue;
            }

            if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                inputs->shift = ev.value != 0;
                continue;
            }

            if (ev.value == 0 || ev.value == 1) {
                *code = ev.code;
                *pressed = ev.value == 1;
                return 0;
            }
        }
    }
}

static void run_helper(const char *arg)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/local/sbin/zero-helper", "zero-helper", arg, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

static void apply_pam_environment(pam_handle_t *pamh)
{
    char **envlist = pam_getenvlist(pamh);
    if (envlist == NULL) {
        return;
    }

    for (char **entry = envlist; *entry != NULL; entry++) {
        putenv(*entry);
    }

    free(envlist);
}

static int launch_session(struct zero_pam_session *session)
{
    pid_t pid = fork();

    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        struct passwd *pw = &session->pw;

        if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
            perror("initgroups");
            _exit(126);
        }
        if (setgid(pw->pw_gid) != 0) {
            perror("setgid");
            _exit(126);
        }
        if (setuid(pw->pw_uid) != 0) {
            perror("setuid");
            _exit(126);
        }

        apply_pam_environment(session->pamh);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("HOME", pw->pw_dir, 1);
        setenv("SHELL", pw->pw_shell != NULL && pw->pw_shell[0] != '\0' ? pw->pw_shell : "/bin/sh", 1);

        if (chdir(pw->pw_dir) != 0 && chdir("/") != 0) {
            perror("chdir");
            _exit(126);
        }

        execl("/usr/local/bin/cardputer-zero-session", "cardputer-zero-session", (char *)NULL);
        perror("exec cardputer-zero-session");
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }

    return 0;
}

static int handle_login(struct user_entry *user, const char *password, char *message, size_t message_size)
{
    struct zero_pam_session session;
    int pam_status = zero_pam_start_session(user->name, password, &session);

    if (pam_status != PAM_SUCCESS) {
        snprintf(message, message_size, "Login failed");
        return 1;
    }

    snprintf(message, message_size, "Session starting");
    launch_session(&session);
    zero_pam_end_session(&session, PAM_SUCCESS);
    snprintf(message, message_size, "Session ended");
    return 0;
}

int main(void)
{
    struct framebuffer fb;
    struct input_set inputs;
    struct user_entry users[MAX_USERS];
    int selected = 0;
    char password[MAX_PASSWORD] = "";
    char message[128] = "";
    int error = 0;

    if (fb_open(&fb) != 0) {
        fprintf(stderr, "zero-greeter: failed to open %s: %s\n", FB_DEVICE, strerror(errno));
        return 1;
    }

    if (input_open(&inputs) != 0) {
        fprintf(stderr, "zero-greeter: failed to open input devices: %s\n", strerror(errno));
        fb_close(&fb);
        return 1;
    }

    for (;;) {
        int user_count = load_users(users);
        if (selected >= user_count) {
            selected = 0;
        }

        draw_frame(&fb, users, user_count, selected, password, message, error);

        unsigned short code;
        int pressed;
        int key_status = read_key(&inputs, &code, &pressed, 1000);
        if (key_status == 1) {
            continue;
        }
        if (key_status != 0 || !pressed) {
            continue;
        }

        if (code == KEY_TAB) {
            if (user_count > 0) {
                selected = (selected + 1) % user_count;
            }
            password[0] = '\0';
            message[0] = '\0';
            error = 0;
            continue;
        }

        if (code == KEY_ESC) {
            snprintf(message, sizeof(message), "R reboot  P poweroff");
            error = 0;
            draw_frame(&fb, users, user_count, selected, password, message, error);
            for (;;) {
                int power_key_status = read_key(&inputs, &code, &pressed, 1000);
                if (power_key_status == 1) {
                    draw_frame(&fb, users, user_count, selected, password, message, error);
                    continue;
                }
                if (power_key_status != 0 || !pressed) {
                    continue;
                }
                if (code == KEY_R) {
                    run_helper("reboot");
                    break;
                }
                if (code == KEY_P) {
                    run_helper("poweroff");
                    break;
                }
                if (code == KEY_ESC || code == KEY_C) {
                    break;
                }
            }
            message[0] = '\0';
            continue;
        }

        if (code == KEY_ENTER || code == KEY_KPENTER) {
            if (user_count == 0) {
                snprintf(message, sizeof(message), "Create a user first");
                error = 1;
                continue;
            }
            error = handle_login(&users[selected], password, message, sizeof(message));
            memset(password, 0, sizeof(password));
            continue;
        }

        if (code == KEY_BACKSPACE || code == KEY_DELETE) {
            size_t len = strlen(password);
            if (len > 0) {
                password[len - 1] = '\0';
            }
            message[0] = '\0';
            continue;
        }

        char ch = key_to_char(code, inputs.shift);
        if (ch != '\0') {
            size_t len = strlen(password);
            if (len + 1 < sizeof(password)) {
                password[len] = ch;
                password[len + 1] = '\0';
            }
            message[0] = '\0';
            error = 0;
        }
    }

    input_close(&inputs);
    fb_close(&fb);
    return 0;
}

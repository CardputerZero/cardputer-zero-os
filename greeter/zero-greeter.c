#include "pam_auth.h"
#include "../common/zero_framebuffer.h"

#include <dirent.h>
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
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_USERS 64
#define MAX_PASSWORD 256
#define MAX_INPUTS 32
#define MAX_JSON 8192
#define FB_WIDTH ZERO_FB_LOGICAL_WIDTH
#define FB_HEIGHT ZERO_FB_LOGICAL_HEIGHT

#define TOP_BAR_H 20
#define BOTTOM_BAR_H 20
#define BOTTOM_BAR_Y (FB_HEIGHT - BOTTOM_BAR_H)

#define RGB565_CONST(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xF8) << 3) | ((b) >> 3))

struct user_entry {
    char name[256];
    uid_t uid;
    gid_t gid;
    char home[512];
    char shell[256];
};

struct input_set {
    int fds[MAX_INPUTS];
    int count;
    int shift;
};

enum ui_mode {
    UI_LOGIN,
    UI_USER_MENU,
    UI_POWER_MENU,
    UI_AUTHENTICATING,
    UI_STARTING_SESSION
};

static const uint16_t COLOR_ZERO_BG = RGB565_CONST(0xE9, 0xE4, 0xD5);
static const uint16_t COLOR_PANEL = RGB565_CONST(0xF4, 0xF0, 0xE6);
static const uint16_t COLOR_TASK_BUTTON = RGB565_CONST(0xEF, 0xE8, 0xD9);
static const uint16_t COLOR_ICON_WELL = RGB565_CONST(0xF8, 0xF4, 0xEA);
static const uint16_t COLOR_INK = RGB565_CONST(0x17, 0x17, 0x17);
static const uint16_t COLOR_LINE = RGB565_CONST(0x2A, 0x2A, 0x2A);
static const uint16_t COLOR_MUTED = RGB565_CONST(0x6E, 0x6A, 0x61);
static const uint16_t COLOR_SOFT_LINE = RGB565_CONST(0xBB, 0xB1, 0x9E);
static const uint16_t COLOR_GRID_DOT = RGB565_CONST(0xC9, 0xC1, 0xAE);
static const uint16_t COLOR_ACCENT = RGB565_CONST(0xE6, 0x6A, 0x2C);
static const uint16_t COLOR_OK = RGB565_CONST(0x3A, 0x7D, 0x44);
static const uint16_t COLOR_WARN = RGB565_CONST(0xB9, 0x4A, 0x2C);
static const uint16_t COLOR_SHADOW = RGB565_CONST(0xBD, 0xB5, 0xA4);

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
            if ((glyph[col] >> row) & 1) {
                fill_rect(fb, x + (col * scale), y + (row * scale), scale, scale, color);
            }
        }
    }
}

static void draw_text(struct zero_framebuffer *fb, int x, int y, const char *text, uint16_t color, int scale)
{
    int cursor = x;
    for (const char *p = text; p != NULL && *p != '\0'; p++) {
        draw_char(fb, cursor, y, *p, color, scale);
        cursor += 6 * scale;
    }
}

static void draw_text_centered(struct zero_framebuffer *fb, int cx, int y, const char *text, uint16_t color, int scale)
{
    draw_text(fb, cx - text_width(text, scale) / 2, y, text, color, scale);
}

static void draw_text_right(struct zero_framebuffer *fb, int right, int y, const char *text, uint16_t color, int scale)
{
    draw_text(fb, right - text_width(text, scale), y, text, color, scale);
}

static void sanitize_ascii_label(const char *input, char *out, size_t out_size)
{
    size_t pos = 0;
    int last_space = 0;

    if (out_size == 0) {
        return;
    }

    for (const unsigned char *p = (const unsigned char *)input;
         p != NULL && *p != '\0' && pos + 1 < out_size;
         p++) {
        unsigned char ch = *p;
        if (ch >= 0x80) {
            continue;
        }
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            out[pos++] = (char)ch;
            last_space = 0;
        } else if ((ch == ' ' || ch == '-' || ch == '_') && pos > 0 && !last_space) {
            out[pos++] = ' ';
            last_space = 1;
        }
    }

    while (pos > 0 && out[pos - 1] == ' ') {
        pos--;
    }
    out[pos] = '\0';
}

static void current_time_text(char out[6])
{
    time_t raw = time(NULL);
    struct tm local_time;

    if (raw == (time_t)-1 || localtime_r(&raw, &local_time) == NULL) {
        snprintf(out, 6, "--:--");
        return;
    }
    snprintf(out, 6, "%02d:%02d", local_time.tm_hour, local_time.tm_min);
}

static int read_battery_percent(void)
{
    DIR *dir = opendir("/sys/class/power_supply");
    if (dir == NULL) {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", entry->d_name);
        FILE *file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }

        int value = -1;
        int ok = fscanf(file, "%d", &value) == 1;
        fclose(file);
        if (ok) {
            closedir(dir);
            if (value < 0) {
                return -1;
            }
            if (value > 100) {
                return 100;
            }
            return value;
        }
    }

    closedir(dir);
    return -1;
}

static void draw_power_icon(struct zero_framebuffer *fb, int x, int y, uint16_t color)
{
    fill_rect(fb, x + 4, y, 2, 5, color);
    fill_rect(fb, x + 2, y + 3, 1, 2, color);
    fill_rect(fb, x + 7, y + 3, 1, 2, color);
    fill_rect(fb, x + 1, y + 5, 1, 3, color);
    fill_rect(fb, x + 8, y + 5, 1, 3, color);
    fill_rect(fb, x + 2, y + 8, 6, 1, color);
}

static void draw_user_icon(struct zero_framebuffer *fb, int x, int y, uint16_t color)
{
    fill_rect(fb, x + 3, y, 4, 1, color);
    fill_rect(fb, x + 2, y + 1, 1, 3, color);
    fill_rect(fb, x + 7, y + 1, 1, 3, color);
    fill_rect(fb, x + 3, y + 4, 4, 1, color);
    fill_rect(fb, x + 1, y + 7, 8, 1, color);
    fill_rect(fb, x, y + 8, 1, 3, color);
    fill_rect(fb, x + 9, y + 8, 1, 3, color);
}

static void draw_battery(struct zero_framebuffer *fb, int x, int y, int percent)
{
    stroke_rect(fb, x, y, 22, 9, COLOR_LINE);
    fill_rect(fb, x + 22, y + 3, 2, 3, COLOR_LINE);
    if (percent >= 0) {
        int fill = (percent * 18) / 100;
        if (fill < 0) {
            fill = 0;
        }
        if (fill > 18) {
            fill = 18;
        }
        fill_rect(fb, x + 2, y + 2, fill, 5, percent >= 25 ? COLOR_OK : COLOR_WARN);
    }
}

static void draw_password_dots(struct zero_framebuffer *fb, int x, int y, size_t count)
{
    size_t max_dots = count > 12 ? 12 : count;
    for (size_t i = 0; i < max_dots; i++) {
        fill_rect(fb, x + (int)i * 7, y + 5, 4, 4, COLOR_INK);
    }
}

static void draw_background(struct zero_framebuffer *fb)
{
    fill_rect(fb, 0, 0, FB_WIDTH, FB_HEIGHT, COLOR_ZERO_BG);

    for (int yy = TOP_BAR_H + 8; yy < BOTTOM_BAR_Y - 4; yy += 8) {
        for (int xx = 8; xx < FB_WIDTH; xx += 8) {
            put_pixel(fb, xx, yy, COLOR_GRID_DOT);
        }
    }
}

static void draw_topbar(struct zero_framebuffer *fb)
{
    char time_text[6];
    char battery_text[8];
    int battery = read_battery_percent();

    current_time_text(time_text);
    if (battery > 100) {
        battery = 100;
    }
    fill_rect(fb, 0, 0, FB_WIDTH, TOP_BAR_H, COLOR_PANEL);
    stroke_rect(fb, 0, 0, FB_WIDTH, TOP_BAR_H, COLOR_LINE);
    draw_text(fb, 6, 5, time_text, COLOR_INK, 1);
    fill_rect(fb, 112, TOP_BAR_H + 1, 96, 3, COLOR_ACCENT);

    draw_text(fb, 222, 5, "LOGIN", COLOR_MUTED, 1);
    if (battery >= 0) {
        snprintf(battery_text, sizeof(battery_text), "%d%%", battery);
    } else {
        snprintf(battery_text, sizeof(battery_text), "--%%");
    }
    draw_text_right(fb, 286, 5, battery_text, COLOR_INK, 1);
    draw_battery(fb, 292, 5, battery);
}

static void draw_bottombar(struct zero_framebuffer *fb)
{
    fill_rect(fb, 0, BOTTOM_BAR_Y, FB_WIDTH, BOTTOM_BAR_H, COLOR_PANEL);
    stroke_rect(fb, 0, BOTTOM_BAR_Y, FB_WIDTH, BOTTOM_BAR_H, COLOR_LINE);

    fill_rect(fb, 0, BOTTOM_BAR_Y, 64, BOTTOM_BAR_H, COLOR_TASK_BUTTON);
    stroke_rect(fb, 0, BOTTOM_BAR_Y, 64, BOTTOM_BAR_H, COLOR_LINE);
    draw_power_icon(fb, 8, BOTTOM_BAR_Y + 5, COLOR_INK);
    draw_text(fb, 22, BOTTOM_BAR_Y + 6, "POWER", COLOR_INK, 1);

    draw_text(fb, 80, BOTTOM_BAR_Y + 6, "TAB USER", COLOR_MUTED, 1);
    draw_text(fb, 166, BOTTOM_BAR_Y + 6, "ENTER LOGIN", COLOR_INK, 1);
    draw_text(fb, 276, BOTTOM_BAR_Y + 6, "ESC", COLOR_MUTED, 1);
}

static const char *status_message(enum ui_mode mode, const char *message, int error)
{
    if (mode == UI_AUTHENTICATING) {
        return "AUTHENTICATING";
    }
    if (mode == UI_STARTING_SESSION) {
        return "SESSION STARTING";
    }
    if (message != NULL && message[0] != '\0') {
        return message;
    }
    return error ? "AUTH FAILED" : "PAM AUTHENTICATION";
}

static void draw_login_panel(struct zero_framebuffer *fb,
                             const struct user_entry *users,
                             int user_count,
                             int selected,
                             const char *password,
                             const char *message,
                             int error,
                             enum ui_mode mode)
{
    char user_label[48];
    const int x = 60;
    const int y = 34;
    const int w = 200;
    const int h = 94;
    const int field_x = 116;
    const int field_w = 126;
    uint16_t pass_border = error ? COLOR_WARN : COLOR_ACCENT;

    fill_rect(fb, x + 4, y + 4, w, h, COLOR_SHADOW);
    fill_rect(fb, x, y, w, h, COLOR_PANEL);
    stroke_rect(fb, x, y, w, h, COLOR_LINE);

    draw_text(fb, x + 16, y + 14, "CARDPUTER ZERO", COLOR_INK, 1);
    draw_text(fb, x + 16, y + 27, mode == UI_USER_MENU ? "SELECT USER" : "GUI LOGIN", COLOR_MUTED, 1);

    draw_text(fb, x + 16, y + 47, "USER", COLOR_MUTED, 1);
    fill_rect(fb, field_x, y + 40, field_w, 18, COLOR_ICON_WELL);
    stroke_rect(fb, field_x, y + 40, field_w, 18, COLOR_LINE);
    draw_user_icon(fb, field_x + 6, y + 45, COLOR_INK);
    if (user_count > 0) {
        sanitize_ascii_label(users[selected].name, user_label, sizeof(user_label));
        if (user_label[0] == '\0') {
            snprintf(user_label, sizeof(user_label), "UID %u", (unsigned)users[selected].uid);
        }
        draw_text(fb, field_x + 22, y + 46, user_label, COLOR_INK, 1);
    } else {
        draw_text(fb, field_x + 22, y + 46, "NO USERS", COLOR_WARN, 1);
    }

    draw_text(fb, x + 16, y + 72, "PASS", COLOR_MUTED, 1);
    fill_rect(fb, field_x, y + 65, field_w, 18, COLOR_ICON_WELL);
    stroke_rect(fb, field_x, y + 65, field_w, 18, pass_border);
    draw_password_dots(fb, field_x + 10, y + 69, strlen(password));
    if (mode != UI_AUTHENTICATING && mode != UI_STARTING_SESSION) {
        int cursor_x = field_x + 10 + (int)(strlen(password) > 12 ? 12 : strlen(password)) * 7;
        fill_rect(fb, cursor_x + 1, y + 69, 2, 10, COLOR_ACCENT);
    }

    draw_text(fb, x + 16, y + 86, status_message(mode, message, error),
              error ? COLOR_WARN : COLOR_MUTED, 1);
}

static void draw_user_menu(struct zero_framebuffer *fb,
                           const struct user_entry *users,
                           int user_count,
                           int menu_selected)
{
    const int x = 88;
    const int y = 63;
    const int w = 146;
    const int visible = user_count < 3 ? user_count : 3;
    const int rows = visible > 0 ? visible : 1;
    const int h = 17 + rows * 18 + 2;
    int start = menu_selected - 1;

    if (start < 0) {
        start = 0;
    }
    if (start + visible > user_count) {
        start = user_count - visible;
    }
    if (start < 0) {
        start = 0;
    }

    fill_rect(fb, x + 3, y + 3, w, h, COLOR_SHADOW);
    fill_rect(fb, x, y, w, h, COLOR_PANEL);
    stroke_rect(fb, x, y, w, h, COLOR_LINE);
    fill_rect(fb, x, y, w, 17, COLOR_INK);
    draw_text(fb, x + 7, y + 5, "SYSTEM USERS", COLOR_PANEL, 1);

    if (user_count <= 0) {
        draw_text(fb, x + 8, y + 25, "NO USERS", COLOR_MUTED, 1);
        return;
    }

    for (int row = 0; row < visible; row++) {
        int user_index = start + row;
        int row_y = y + 18 + row * 18;
        char name[48];
        char uid_text[16];
        int selected = user_index == menu_selected;

        fill_rect(fb, x + 4, row_y, w - 8, 16, selected ? COLOR_ACCENT : COLOR_PANEL);
        sanitize_ascii_label(users[user_index].name, name, sizeof(name));
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "USER");
        }
        snprintf(uid_text, sizeof(uid_text), "%u", (unsigned)users[user_index].uid);
        draw_text(fb, x + 10, row_y + 4, name, selected ? COLOR_INK : COLOR_INK, 1);
        draw_text_right(fb, x + w - 12, row_y + 4, uid_text, selected ? COLOR_INK : COLOR_MUTED, 1);
    }
}

static void draw_power_menu(struct zero_framebuffer *fb, int power_selection)
{
    const int x = 92;
    const int y = 52;
    const int w = 136;
    const int h = 75;
    const char *items[] = {"SHUTDOWN", "REBOOT", "CANCEL"};

    fill_rect(fb, x + 3, y + 3, w, h, COLOR_SHADOW);
    fill_rect(fb, x, y, w, h, COLOR_PANEL);
    stroke_rect(fb, x, y, w, h, COLOR_LINE);
    fill_rect(fb, x, y, w, 17, COLOR_INK);
    draw_text_centered(fb, x + w / 2, y + 5, "POWER", COLOR_PANEL, 1);

    for (int i = 0; i < 3; i++) {
        int row_y = y + 24 + i * 18;
        int selected = i == power_selection;
        uint16_t fill = selected ? (i == 0 ? COLOR_WARN : COLOR_ACCENT) : COLOR_PANEL;
        fill_rect(fb, x + 10, row_y, w - 20, 15, fill);
        stroke_rect(fb, x + 10, row_y, w - 20, 15, selected ? COLOR_LINE : COLOR_SOFT_LINE);
        draw_text_centered(fb, x + w / 2, row_y + 4, items[i], selected ? COLOR_INK : COLOR_MUTED, 1);
    }
}

static void draw_frame(struct zero_framebuffer *fb,
                       const struct user_entry *users,
                       int user_count,
                       int selected,
                       int menu_selected,
                       int power_selection,
                       const char *password,
                       const char *message,
                       int error,
                       enum ui_mode mode)
{
    draw_background(fb);
    draw_topbar(fb);
    draw_login_panel(fb, users, user_count, selected, password, message, error, mode);
    draw_bottombar(fb);

    if (mode == UI_USER_MENU) {
        draw_user_menu(fb, users, user_count, menu_selected);
    } else if (mode == UI_POWER_MENU) {
        draw_power_menu(fb, power_selection);
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

static int greetd_is_active(void)
{
    const char *sock = getenv("GREETD_SOCK");
    return sock != NULL && sock[0] != '\0';
}

static void json_escape(const char *input, char *out, size_t out_size)
{
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    for (const unsigned char *p = (const unsigned char *)input;
         p != NULL && *p != '\0' && pos + 1 < out_size;
         p++) {
        unsigned char ch = *p;
        if ((ch == '"' || ch == '\\') && pos + 2 < out_size) {
            out[pos++] = '\\';
            out[pos++] = (char)ch;
        } else if (ch == '\n' && pos + 2 < out_size) {
            out[pos++] = '\\';
            out[pos++] = 'n';
        } else if (ch == '\r' && pos + 2 < out_size) {
            out[pos++] = '\\';
            out[pos++] = 'r';
        } else if (ch == '\t' && pos + 2 < out_size) {
            out[pos++] = '\\';
            out[pos++] = 't';
        } else if (ch >= 0x20 && ch < 0x80) {
            out[pos++] = (char)ch;
        }
    }
    out[pos] = '\0';
}

static int write_full(int fd, const void *buf, size_t len)
{
    const char *cursor = buf;
    while (len > 0) {
        ssize_t n = write(fd, cursor, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += n;
        len -= (size_t)n;
    }
    return 0;
}

static int read_full(int fd, void *buf, size_t len)
{
    char *cursor = buf;
    while (len > 0) {
        ssize_t n = read(fd, cursor, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        cursor += n;
        len -= (size_t)n;
    }
    return 0;
}

static int greetd_connect(void)
{
    const char *sock_path = getenv("GREETD_SOCK");
    struct sockaddr_un addr;
    int fd;

    if (sock_path == NULL || sock_path[0] == '\0') {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int greetd_request(int fd, const char *request, char *reply, size_t reply_size)
{
    uint32_t len = (uint32_t)strlen(request);
    uint32_t reply_len = 0;

    if (write_full(fd, &len, sizeof(len)) != 0 ||
        write_full(fd, request, len) != 0) {
        return -1;
    }

    if (read_full(fd, &reply_len, sizeof(reply_len)) != 0) {
        return -1;
    }

    if (reply_len >= reply_size || reply_len >= MAX_JSON) {
        return -1;
    }

    if (read_full(fd, reply, reply_len) != 0) {
        return -1;
    }
    reply[reply_len] = '\0';
    return 0;
}

static int json_has_type(const char *json, const char *type)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"type\":\"%s\"", type);
    if (strstr(json, pattern) != NULL) {
        return 1;
    }
    snprintf(pattern, sizeof(pattern), "\"type\": \"%s\"", type);
    return strstr(json, pattern) != NULL;
}

static int json_has_auth_message_type(const char *json, const char *type)
{
    char pattern[160];
    snprintf(pattern, sizeof(pattern), "\"auth_message_type\":\"%s\"", type);
    if (strstr(json, pattern) != NULL) {
        return 1;
    }
    snprintf(pattern, sizeof(pattern), "\"auth_message_type\": \"%s\"", type);
    return strstr(json, pattern) != NULL;
}

static int greetd_cancel(int fd)
{
    char reply[MAX_JSON];
    return greetd_request(fd, "{\"type\":\"cancel_session\"}", reply, sizeof(reply));
}

static int greetd_start_session(int fd)
{
    char reply[MAX_JSON];
    const char *session = getenv("CARDPUTER_ZERO_GREETD_SESSION");
    const char *request;
    char custom_request[MAX_JSON];
    char escaped_session[2048];

    if (session != NULL && session[0] != '\0') {
        json_escape(session, escaped_session, sizeof(escaped_session));
        snprintf(custom_request, sizeof(custom_request),
                 "{\"type\":\"start_session\","
                 "\"cmd\":[\"/bin/sh\",\"-lc\",\"%s\"],"
                 "\"env\":[\"CARDPUTER_ZERO_SESSION=1\","
                 "\"XDG_CURRENT_DESKTOP=CardputerZero\","
                 "\"XDG_SESSION_DESKTOP=CardputerZero\"]}",
                 escaped_session);
        request = custom_request;
    } else {
        request = "{\"type\":\"start_session\","
                  "\"cmd\":[\"/usr/local/bin/cardputer-zero-session\"],"
                  "\"env\":[\"CARDPUTER_ZERO_SESSION=1\","
                  "\"XDG_CURRENT_DESKTOP=CardputerZero\","
                  "\"XDG_SESSION_DESKTOP=CardputerZero\"]}";
    }

    if (greetd_request(fd, request, reply, sizeof(reply)) != 0) {
        return -1;
    }
    return json_has_type(reply, "success") ? 0 : -1;
}

static int greetd_login(const char *username,
                        const char *password,
                        char *message,
                        size_t message_size)
{
    int fd = greetd_connect();
    char request[MAX_JSON];
    char reply[MAX_JSON];
    char escaped_user[512];
    char escaped_password[MAX_PASSWORD * 2];
    int status = 1;

    if (fd < 0) {
        snprintf(message, message_size, "GREETD SOCKET FAILED");
        return 1;
    }

    json_escape(username, escaped_user, sizeof(escaped_user));
    snprintf(request, sizeof(request),
             "{\"type\":\"create_session\",\"username\":\"%s\"}",
             escaped_user);

    if (greetd_request(fd, request, reply, sizeof(reply)) != 0) {
        snprintf(message, message_size, "GREETD FAILED");
        close(fd);
        return 1;
    }

    for (;;) {
        if (json_has_type(reply, "success")) {
            snprintf(message, message_size, "SESSION STARTING");
            status = greetd_start_session(fd) == 0 ? 0 : 1;
            if (status != 0) {
                snprintf(message, message_size, "SESSION FAILED");
                greetd_cancel(fd);
            }
            break;
        }

        if (json_has_type(reply, "error")) {
            snprintf(message, message_size, strstr(reply, "auth_error") != NULL ?
                     "AUTH FAILED" : "GREETD ERROR");
            greetd_cancel(fd);
            break;
        }

        if (!json_has_type(reply, "auth_message")) {
            snprintf(message, message_size, "GREETD ERROR");
            greetd_cancel(fd);
            break;
        }

        if (json_has_auth_message_type(reply, "secret") ||
            json_has_auth_message_type(reply, "visible")) {
            json_escape(password, escaped_password, sizeof(escaped_password));
            snprintf(request, sizeof(request),
                     "{\"type\":\"post_auth_message_response\",\"response\":\"%s\"}",
                     escaped_password);
        } else {
            snprintf(request, sizeof(request),
                     "{\"type\":\"post_auth_message_response\"}");
        }

        if (greetd_request(fd, request, reply, sizeof(reply)) != 0) {
            snprintf(message, message_size, "GREETD FAILED");
            greetd_cancel(fd);
            break;
        }
    }

    close(fd);
    return status;
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

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        fprintf(stderr, "zero-greeter: session for %s exited with status %d\n",
                session->pw.pw_name, code);
        return code;
    }

    if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        fprintf(stderr, "zero-greeter: session for %s terminated by signal %d\n",
                session->pw.pw_name, signal);
        return 128 + signal;
    }

    fprintf(stderr, "zero-greeter: session for %s ended with unhandled wait status %d\n",
            session->pw.pw_name, status);
    return -1;
}

static int handle_login(struct user_entry *user, const char *password, char *message, size_t message_size)
{
    if (greetd_is_active()) {
        return greetd_login(user->name, password, message, message_size);
    }

    struct zero_pam_session session;
    int pam_status = zero_pam_start_session(user->name, password, &session);

    if (pam_status != PAM_SUCCESS) {
        snprintf(message, message_size, "AUTH FAILED");
        return 1;
    }

    snprintf(message, message_size, "SESSION STARTING");
    int session_status = launch_session(&session);
    zero_pam_end_session(&session, PAM_SUCCESS);
    if (session_status == 0) {
        snprintf(message, message_size, "SESSION ENDED");
    } else {
        snprintf(message, message_size, "SESSION FAILED");
    }
    return session_status == 0 ? 0 : 1;
}

static void clamp_selection(int *value, int count)
{
    if (count <= 0) {
        *value = 0;
        return;
    }
    if (*value < 0) {
        *value = count - 1;
    } else if (*value >= count) {
        *value = 0;
    }
}

int main(void)
{
    struct zero_framebuffer fb;
    struct input_set inputs;
    struct user_entry users[MAX_USERS];
    int selected = 0;
    int menu_selected = 0;
    int power_selection = 2;
    char password[MAX_PASSWORD] = "";
    char message[128] = "";
    int error = 0;
    enum ui_mode mode = UI_LOGIN;

    if (fb_open(&fb) != 0) {
        fprintf(stderr, "zero-greeter: failed to open internal framebuffer: %s\n", strerror(errno));
        return 1;
    }

    if (input_open(&inputs) != 0) {
        fprintf(stderr, "zero-greeter: failed to open input devices: %s\n", strerror(errno));
        fb_close(&fb);
        return 1;
    }

    for (;;) {
        int user_count = load_users(users);
        clamp_selection(&selected, user_count);
        clamp_selection(&menu_selected, user_count);

        draw_frame(&fb, users, user_count, selected, menu_selected, power_selection,
                   password, message, error, mode);

        unsigned short code;
        int pressed;
        int key_status = read_key(&inputs, &code, &pressed, 1000);
        if (key_status == 1) {
            continue;
        }
        if (key_status != 0 || !pressed) {
            continue;
        }

        if (mode == UI_USER_MENU) {
            if (code == KEY_ESC) {
                mode = UI_LOGIN;
                continue;
            }
            if (code == KEY_TAB || code == KEY_DOWN || code == KEY_RIGHT) {
                menu_selected++;
                clamp_selection(&menu_selected, user_count);
                continue;
            }
            if (code == KEY_UP || code == KEY_LEFT) {
                menu_selected--;
                clamp_selection(&menu_selected, user_count);
                continue;
            }
            if ((code == KEY_ENTER || code == KEY_KPENTER) && user_count > 0) {
                selected = menu_selected;
                password[0] = '\0';
                message[0] = '\0';
                error = 0;
                mode = UI_LOGIN;
                continue;
            }
            continue;
        }

        if (mode == UI_POWER_MENU) {
            if (code == KEY_ESC) {
                mode = UI_LOGIN;
                continue;
            }
            if (code == KEY_DOWN || code == KEY_RIGHT || code == KEY_TAB) {
                power_selection = (power_selection + 1) % 3;
                continue;
            }
            if (code == KEY_UP || code == KEY_LEFT) {
                power_selection = (power_selection + 2) % 3;
                continue;
            }
            if (code == KEY_ENTER || code == KEY_KPENTER) {
                if (power_selection == 0) {
                    run_helper("shutdown");
                } else if (power_selection == 1) {
                    run_helper("reboot");
                } else {
                    mode = UI_LOGIN;
                }
                continue;
            }
            continue;
        }

        if (code == KEY_TAB) {
            if (user_count > 0) {
                menu_selected = selected;
            }
            mode = UI_USER_MENU;
            message[0] = '\0';
            error = 0;
            continue;
        }

        if (code == KEY_ESC || code == KEY_POWER) {
            power_selection = 2;
            mode = UI_POWER_MENU;
            message[0] = '\0';
            error = 0;
            continue;
        }

        if (code == KEY_ENTER || code == KEY_KPENTER) {
            if (user_count == 0) {
                snprintf(message, sizeof(message), "CREATE A USER FIRST");
                error = 1;
                continue;
            }

            mode = UI_AUTHENTICATING;
            snprintf(message, sizeof(message), "AUTHENTICATING");
            draw_frame(&fb, users, user_count, selected, menu_selected, power_selection,
                       password, message, error, mode);

            mode = UI_STARTING_SESSION;
            error = handle_login(&users[selected], password, message, sizeof(message));
            memset(password, 0, sizeof(password));
            if (!error && greetd_is_active()) {
                input_close(&inputs);
                fb_close(&fb);
                return 0;
            }
            mode = UI_LOGIN;
            continue;
        }

        if (code == KEY_BACKSPACE || code == KEY_DELETE) {
            size_t len = strlen(password);
            if (len > 0) {
                password[len - 1] = '\0';
            }
            message[0] = '\0';
            error = 0;
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

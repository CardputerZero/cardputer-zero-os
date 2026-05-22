#ifndef CARDPUTER_ZERO_FRAMEBUFFER_H
#define CARDPUTER_ZERO_FRAMEBUFFER_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ZERO_FB_LOGICAL_WIDTH 320
#define ZERO_FB_LOGICAL_HEIGHT 170
#define ZERO_FB_BYTES_PER_PIXEL 2
#define ZERO_FB_FALLBACK_DEVICE "/dev/fb0"

#if defined(__GNUC__)
#define ZERO_FB_MAYBE_UNUSED __attribute__((unused))
#else
#define ZERO_FB_MAYBE_UNUSED
#endif

struct zero_framebuffer {
    int fd;
    uint16_t *pixels;
    size_t size;
    int ok;
    char device[512];
    int physical_width;
    int physical_height;
    int stride_pixels;
    int rotation;
};

static int zero_fb_name_is_internal(const char *name)
{
    return name != NULL &&
           (strstr(name, "fb_st7789") != NULL ||
            strstr(name, "st7789") != NULL ||
            strstr(name, "panel-mipi-dbi") != NULL ||
            strstr(name, "panel-mipi-dbid") != NULL);
}

static int zero_fb_read_text_file(const char *path, char *out, size_t out_size)
{
    FILE *file;
    size_t len;

    if (out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    if (fgets(out, (int)out_size, file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);

    len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
        out[--len] = '\0';
    }

    return 0;
}

static int zero_fb_resolve_device(char *out, size_t out_size)
{
    const char *env_fb = getenv("CARDPUTER_ZERO_FB");
    DIR *dir;
    struct dirent *entry;

    if (out_size == 0) {
        errno = EINVAL;
        return -1;
    }
    out[0] = '\0';

    if (env_fb != NULL && env_fb[0] != '\0') {
        snprintf(out, out_size, "%s", env_fb);
        return 0;
    }

    dir = opendir("/sys/class/graphics");
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            char name_path[512];
            char fb_name[128];

            if (strncmp(entry->d_name, "fb", 2) != 0 || strcmp(entry->d_name, "fbcon") == 0) {
                continue;
            }

            snprintf(name_path, sizeof(name_path), "/sys/class/graphics/%s/name", entry->d_name);
            if (zero_fb_read_text_file(name_path, fb_name, sizeof(fb_name)) != 0) {
                continue;
            }
            if (!zero_fb_name_is_internal(fb_name)) {
                continue;
            }

            snprintf(out, out_size, "/dev/%s", entry->d_name);
            closedir(dir);
            return 0;
        }
        closedir(dir);
    }

    snprintf(out, out_size, "%s", ZERO_FB_FALLBACK_DEVICE);
    return 0;
}

static int zero_fb_parse_rotation_env(int fallback)
{
    const char *value = getenv("CARDPUTER_ZERO_FB_ROTATION");

    if (value == NULL || value[0] == '\0' || strcmp(value, "auto") == 0) {
        return fallback;
    }
    if (strcmp(value, "0") == 0) {
        return 0;
    }
    if (strcmp(value, "90") == 0) {
        return 90;
    }
    if (strcmp(value, "270") == 0) {
        return 270;
    }

    return fallback;
}

static int zero_fb_open(struct zero_framebuffer *fb)
{
    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    size_t map_size;
    int fallback_rotation;

    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    if (zero_fb_resolve_device(fb->device, sizeof(fb->device)) != 0) {
        return -1;
    }

    fb->fd = open(fb->device, O_RDWR | O_CLOEXEC);
    if (fb->fd < 0) {
        return -1;
    }

    memset(&var_info, 0, sizeof(var_info));
    memset(&fix_info, 0, sizeof(fix_info));
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &var_info) != 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix_info) != 0) {
        fb->physical_width = ZERO_FB_LOGICAL_WIDTH;
        fb->physical_height = ZERO_FB_LOGICAL_HEIGHT;
        fb->stride_pixels = ZERO_FB_LOGICAL_WIDTH;
        map_size = ZERO_FB_LOGICAL_WIDTH * ZERO_FB_LOGICAL_HEIGHT * ZERO_FB_BYTES_PER_PIXEL;
    } else {
        if (var_info.bits_per_pixel != 16 || fix_info.line_length == 0) {
            close(fb->fd);
            fb->fd = -1;
            errno = EINVAL;
            return -1;
        }

        fb->physical_width = (int)var_info.xres;
        fb->physical_height = (int)var_info.yres;
        fb->stride_pixels = (int)fix_info.line_length / ZERO_FB_BYTES_PER_PIXEL;
        map_size = (size_t)fix_info.line_length *
                   (size_t)(var_info.yres_virtual > 0 ? var_info.yres_virtual : var_info.yres);
    }

    if (fb->physical_width == ZERO_FB_LOGICAL_HEIGHT &&
        fb->physical_height == ZERO_FB_LOGICAL_WIDTH) {
        fallback_rotation = 270;
    } else {
        fallback_rotation = 0;
    }
    fb->rotation = zero_fb_parse_rotation_env(fallback_rotation);

    fb->size = map_size;
    fb->pixels = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->pixels == MAP_FAILED) {
        fb->pixels = NULL;
        close(fb->fd);
        fb->fd = -1;
        return -1;
    }

    fb->ok = 1;
    return 0;
}

static void zero_fb_close(struct zero_framebuffer *fb)
{
    if (fb->pixels != NULL) {
        munmap(fb->pixels, fb->size);
    }
    if (fb->fd >= 0) {
        close(fb->fd);
    }
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
}

static void zero_fb_put_pixel(struct zero_framebuffer *fb, int x, int y, uint16_t color)
{
    int px = x;
    int py = y;

    if (!fb->ok || x < 0 || y < 0 ||
        x >= ZERO_FB_LOGICAL_WIDTH || y >= ZERO_FB_LOGICAL_HEIGHT) {
        return;
    }

    if (fb->rotation == 90) {
        px = ZERO_FB_LOGICAL_HEIGHT - 1 - y;
        py = x;
    } else if (fb->rotation == 270) {
        px = y;
        py = ZERO_FB_LOGICAL_WIDTH - 1 - x;
    }

    if (px < 0 || py < 0 ||
        px >= fb->physical_width || py >= fb->physical_height ||
        px >= fb->stride_pixels) {
        return;
    }

    fb->pixels[(py * fb->stride_pixels) + px] = color;
}

static int zero_fb_path_matches_device(const char *target, const char *device)
{
    size_t len;

    if (target == NULL || device == NULL || device[0] == '\0') {
        return 0;
    }

    len = strlen(device);
    return strncmp(target, device, len) == 0 &&
           (target[len] == '\0' || target[len] == ' ');
}

static int ZERO_FB_MAYBE_UNUSED zero_fb_path_is_internal_device(const char *target)
{
    DIR *dir;
    struct dirent *entry;

    if (target == NULL || target[0] == '\0') {
        return 0;
    }

    dir = opendir("/sys/class/graphics");
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            char name_path[512];
            char fb_name[128];
            char device[512];

            if (strncmp(entry->d_name, "fb", 2) != 0 || strcmp(entry->d_name, "fbcon") == 0) {
                continue;
            }

            snprintf(name_path, sizeof(name_path), "/sys/class/graphics/%s/name", entry->d_name);
            if (zero_fb_read_text_file(name_path, fb_name, sizeof(fb_name)) != 0) {
                continue;
            }
            if (!zero_fb_name_is_internal(fb_name)) {
                continue;
            }

            snprintf(device, sizeof(device), "/dev/%s", entry->d_name);
            if (zero_fb_path_matches_device(target, device)) {
                closedir(dir);
                return 1;
            }
        }
        closedir(dir);
    }

    return zero_fb_path_matches_device(target, ZERO_FB_FALLBACK_DEVICE);
}

#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "grok_ui.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define GROK_UI_DEFAULT_W 2560
#define GROK_UI_DEFAULT_H 1440
#define GROK_UI_TESSERACT_LANG "eng"

static int parse_int_env(const char *name, int def) {
    const char *v = getenv(name);
    if (!v || !v[0]) return def;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (!end || *end) return def;
    return (int)n;
}

int grok_ui_slot_rect(int slot, grok_ui_rect_t *rect) {
    if (!rect) return -1;
    char env_name[64];
    snprintf(env_name, sizeof(env_name), "GROK_UI_SLOT_%d", slot);
    const char *spec = getenv(env_name);

    rect->x = 0;
    rect->y = 0;
    rect->w = parse_int_env("GROK_UI_SCREEN_W", GROK_UI_DEFAULT_W);
    rect->h = parse_int_env("GROK_UI_SCREEN_H", GROK_UI_DEFAULT_H);

    if (spec && spec[0]) {
        int x = 0, y = 0, w = rect->w, h = rect->h;
        if (sscanf(spec, "%d,%d,%d,%d", &x, &y, &w, &h) == 4) {
            rect->x = x;
            rect->y = y;
            rect->w = w;
            rect->h = h;
        }
    }

    if (slot >= 7 && slot <= 9) {
        /* Slots are workspaces, not physical offsets by default. */
        return 0;
    }
    return 0;
}

static int ensure_ydotoold(void) {
    struct stat st;
    const char *socket_path = getenv("YDOTOOL_SOCKET");
    if (!socket_path || !socket_path[0]) {
        socket_path = "/run/user/1000/.ydotool_socket";
    }
    if (stat(socket_path, &st) == 0) return 0;

    /* uinput is available; try to start the daemon for this session. */
    int rc = system("ydotoold >/tmp/conv_forest_ydotoold.log 2>&1 &");
    (void)rc;
    for (int i = 0; i < 40; i++) {
        if (stat(socket_path, &st) == 0) return 0;
        usleep(100000);
    }
    return -1;
}

static int shell_command(const char *fmt, ...) {
    char *cmd = NULL;
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(&cmd, fmt, ap);
    va_end(ap);
    if (n < 0 || !cmd) return -1;
    int rc = system(cmd);
    free(cmd);
    return rc == 0 ? 0 : -1;
}

static char *shell_quote(const char *text) {
    size_t n = strlen(text);
    size_t cap = n * 2 + 3;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t pos = 0;
    out[pos++] = '\'';
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '\'') {
            if (pos + 4 >= cap) {
                cap *= 2;
                char *next = realloc(out, cap);
                if (!next) { free(out); return NULL; }
                out = next;
            }
            out[pos++] = '\'';
            out[pos++] = '\\';
            out[pos++] = '\'';
            out[pos++] = '\'';
        } else {
            if (pos + 2 >= cap) {
                cap *= 2;
                char *next = realloc(out, cap);
                if (!next) { free(out); return NULL; }
                out = next;
            }
            out[pos++] = text[i];
        }
    }
    out[pos++] = '\'';
    out[pos] = '\0';
    return out;
}

int grok_ui_capture_slot(int slot, const char *png_path) {
    grok_ui_rect_t rect;
    if (grok_ui_slot_rect(slot, &rect) != 0) return -1;
    return shell_command("grim -g '%d,%d %dx%d' '%s'",
                         rect.x, rect.y, rect.w, rect.h, png_path);
}

int grok_ui_ocr_text(const char *png_path, char **text_out) {
    if (!png_path || !text_out) return -1;
    *text_out = NULL;

    char *cmd = NULL;
    if (asprintf(&cmd, "tesseract '%s' stdout -l %s 2>/dev/null",
                 png_path, GROK_UI_TESSERACT_LANG) < 0) {
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    free(cmd);
    if (!fp) return -1;

    size_t cap = 4096;
    size_t len = 0;
    char *text = malloc(cap);
    if (!text) { pclose(fp); return -1; }
    text[0] = '\0';

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (len + n + 1 > cap) {
            while (cap < len + n + 1) cap *= 2;
            char *next = realloc(text, cap);
            if (!next) { free(text); pclose(fp); return -1; }
            text = next;
        }
        memcpy(text + len, buf, n);
        len += n;
        text[len] = '\0';
    }
    int rc = pclose(fp);
    if (rc != 0) {
        free(text);
        return -1;
    }

    /* Trim trailing whitespace. */
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == ' ' ||
                       text[len - 1] == '\t' || text[len - 1] == '\r')) {
        text[--len] = '\0';
    }

    *text_out = text;
    return 0;
}

int grok_ui_click_slot(int slot, int x, int y) {
    grok_ui_rect_t rect;
    if (grok_ui_slot_rect(slot, &rect) != 0) return -1;
    if (ensure_ydotoold() != 0) return -1;
    return shell_command(
        "ydotool mousemove -- -x %d -y %d >/dev/null 2>&1 && ydotool click 0x00 >/dev/null 2>&1",
        rect.x + x, rect.y + y);
}

int grok_ui_type_text(const char *text) {
    if (!text) return -1;
    if (ensure_ydotoold() != 0) return -1;
    char *quoted = shell_quote(text);
    if (!quoted) return -1;
    int rc = shell_command("ydotool type --delay 12 %s >/dev/null 2>&1", quoted);
    free(quoted);
    return rc;
}

int grok_ui_key(const char *key) {
    if (!key || !key[0]) return -1;
    if (ensure_ydotoold() != 0) return -1;
    return shell_command("ydotool key %s >/dev/null 2>&1", key);
}

void grok_ui_free_text(char *text) {
    free(text);
}

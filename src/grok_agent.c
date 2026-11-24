#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "grok_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
            "usage:\n"
            "  %s capture SLOT PNG\n"
            "  %s ocr PNG\n"
            "  %s probe SLOT PNG\n"
            "  %s click SLOT X Y\n"
            "  %s type TEXT\n"
            "  %s key KEY\n",
            prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "capture") == 0 && argc == 4) {
        int slot = atoi(argv[2]);
        return grok_ui_capture_slot(slot, argv[3]) == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "ocr") == 0 && argc == 3) {
        char *text = NULL;
        if (grok_ui_ocr_text(argv[2], &text) != 0) return 1;
        puts(text ? text : "");
        grok_ui_free_text(text);
        return 0;
    }

    if (strcmp(cmd, "probe") == 0 && argc == 4) {
        int slot = atoi(argv[2]);
        const char *png = argv[3];
        if (grok_ui_capture_slot(slot, png) != 0) {
            fprintf(stderr, "capture failed\n");
            return 1;
        }
        char *text = NULL;
        if (grok_ui_ocr_text(png, &text) != 0) {
            fprintf(stderr, "ocr failed\n");
            return 1;
        }
        puts(text ? text : "");
        grok_ui_free_text(text);
        return 0;
    }

    if (strcmp(cmd, "click") == 0 && argc == 5) {
        int slot = atoi(argv[2]);
        int x = atoi(argv[3]);
        int y = atoi(argv[4]);
        return grok_ui_click_slot(slot, x, y) == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "type") == 0 && argc == 3) {
        return grok_ui_type_text(argv[2]) == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "key") == 0 && argc == 3) {
        return grok_ui_key(argv[2]) == 0 ? 0 : 1;
    }

    usage(argv[0]);
    return 2;
}

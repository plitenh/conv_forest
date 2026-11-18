#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>

#include "auth.h"
#include "capture.h"
#include "http.h"

#define DEFAULT_OUT "./v2"
#define DEFAULT_COUNT 0

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : DEFAULT_OUT;
    size_t count = argc > 2 ? (size_t)strtoul(argv[2], NULL, 10) : DEFAULT_COUNT;

    dpsk_auth_t auth;
    if (dpsk_auth_load(&auth) != 0) {
        fprintf(stderr, "auth load failed\n");
        return 2;
    }
    if (dpsk_http_init() != 0) {
        fprintf(stderr, "curl init failed\n");
        dpsk_auth_free(&auth);
        return 2;
    }

    int rc = dpsk_capture_sessions("https://chat.deepseek.com", &auth, out, count);
    dpsk_http_cleanup();
    dpsk_auth_free(&auth);
    return rc == 0 ? 0 : 1;
}

#ifndef DPSK_HTTP_H
#define DPSK_HTTP_H

#include <stddef.h>

#include "auth.h"

typedef struct dpsk_http_buf {
    char *data;
    size_t len;
} dpsk_http_buf_t;

int dpsk_http_init(void);
void dpsk_http_cleanup(void);
int dpsk_http_get(const char *url,
                  const dpsk_auth_t *auth,
                  dpsk_http_buf_t *out);
void dpsk_http_buf_free(dpsk_http_buf_t *buf);

#endif

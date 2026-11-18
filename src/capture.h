#ifndef DPSK_CAPTURE_H
#define DPSK_CAPTURE_H

#include <stddef.h>

#include "auth.h"

int dpsk_capture_forest(const char *base_url,
                        const dpsk_auth_t *auth,
                        const char *out_root);

#endif

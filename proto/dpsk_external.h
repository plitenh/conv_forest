/*
 * dpsk_external.h
 *
 * Out-of-process helpers that must not pollute the forest data model.
 * OCR is intentionally deferred. PoW/uinput is a separate executable
 * boundary and only returns a solved answer or an error.
 */

#ifndef DPSK_EXTERNAL_H
#define DPSK_EXTERNAL_H

#include <stddef.h>

typedef enum {
    DPSK_EXT_OK = 0,
    DPSK_EXT_CHALLENGE_NEEDED,
    DPSK_EXT_TIMEOUT,
    DPSK_EXT_IO_ERROR
} dpsk_ext_status_t;

/* Solve a PoW-like challenge out-of-process. OCR is deferred; the current
 * prototype may use uinput automation and return a fixed answer for local
 * synthetic fixtures. */
int dpsk_ext_pow_solve(const char *challenge,
                       char *answer,
                       size_t answer_len);

/* Submit the solved answer through uinput/synthetic input. */
int dpsk_ext_uinput_submit(const char *answer);

#endif /* DPSK_EXTERNAL_H */

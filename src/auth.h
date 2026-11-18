#ifndef DPSK_AUTH_H
#define DPSK_AUTH_H

typedef struct dpsk_auth {
    char *token;
    char *cookie;
} dpsk_auth_t;

int dpsk_auth_load(dpsk_auth_t *auth);
void dpsk_auth_free(dpsk_auth_t *auth);

#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "auth.h"

#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#define DEFAULT_PROFILE_ROOT \
    "/home/kernel_dev/.var/app/org.mozilla.firefox/config/mozilla/firefox/bas7n4yc.default-release"

static const char *profile_root(void) {
    const char *env = getenv("DPSK_PROFILE_ROOT");
    return (env && env[0]) ? env : DEFAULT_PROFILE_ROOT;
}

typedef struct strbuf {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

static int strbuf_reserve(strbuf_t *b, size_t extra) {
    if (extra > (size_t)-1 - b->len) {
        return -1;
    }
    size_t need = b->len + extra + 1;
    if (need <= b->cap) {
        return 0;
    }
    size_t cap = b->cap ? b->cap : 128;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    char *next = realloc(b->data, cap);
    if (!next) {
        return -1;
    }
    b->data = next;
    b->cap = cap;
    return 0;
}

static int strbuf_append(strbuf_t *b, const char *s, size_t n) {
    if (!s) {
        s = "";
        n = 0;
    }
    if (strbuf_reserve(b, n) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int strbuf_append_cstr(strbuf_t *b, const char *s) {
    return strbuf_append(b, s, s ? strlen(s) : 0);
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    int ok = ferror(in) || ferror(out) ? -1 : 0;
    fclose(in);
    fclose(out);
    return ok;
}

static char *read_token(void) {
    char src[PATH_MAX];
    snprintf(src, sizeof(src),
             "%s/storage/default/https+++chat.deepseek.com/ls/data.sqlite",
             profile_root());

    char tmpl[] = "/tmp/dpsk_token_XXXXXX";
    char *tmpdir = mkdtemp(tmpl);
    if (!tmpdir) {
        fprintf(stderr, "mkdtemp failed: %s\n", strerror(errno));
        return NULL;
    }

    char dst[PATH_MAX];
    snprintf(dst, sizeof(dst), "%s/data.sqlite", tmpdir);
    if (copy_file(src, dst) != 0) {
        fprintf(stderr, "copy token db failed\n");
        rmdir(tmpdir);
        return NULL;
    }

    char src_wal[PATH_MAX + 8], dst_wal[PATH_MAX + 8];
    snprintf(src_wal, sizeof(src_wal), "%s-wal", src);
    snprintf(dst_wal, sizeof(dst_wal), "%s-wal", dst);
    copy_file(src_wal, dst_wal);

    char src_shm[PATH_MAX + 8], dst_shm[PATH_MAX + 8];
    snprintf(src_shm, sizeof(src_shm), "%s-shm", src);
    snprintf(dst_shm, sizeof(dst_shm), "%s-shm", dst);
    copy_file(src_shm, dst_shm);

    sqlite3 *db = NULL;
    if (sqlite3_open(dst, &db) != SQLITE_OK) {
        fprintf(stderr, "open token db failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        unlink(dst);
        unlink(dst_wal);
        unlink(dst_shm);
        rmdir(tmpdir);
        return NULL;
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
                                "select value from data where key='userToken'",
                                -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "prepare token query failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        unlink(dst);
        unlink(dst_wal);
        unlink(dst_shm);
        rmdir(tmpdir);
        return NULL;
    }

    char *token = NULL;
    if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *blob = sqlite3_column_text(stmt, 0);
        int len = sqlite3_column_bytes(stmt, 0);
        if (blob && len > 0) {
            cJSON *obj = cJSON_ParseWithLength((const char *)blob, (size_t)len);
            if (!obj) {
                fprintf(stderr, "token value is not valid JSON\n");
            } else {
                cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, "value");
                if (cJSON_IsString(value) && value->valuestring &&
                    value->valuestring[0]) {
                    token = strdup(value->valuestring);
                }
                cJSON_Delete(obj);
            }
        }
    } else if (rc != SQLITE_DONE) {
        fprintf(stderr, "token query failed: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    unlink(dst);
    unlink(dst_wal);
    unlink(dst_shm);
    rmdir(tmpdir);
    return token;
}

static char *read_cookies(void) {
    char src[PATH_MAX];
    snprintf(src, sizeof(src), "%s/cookies.sqlite", profile_root());

    char tmpl[] = "/tmp/dpsk_cookies_XXXXXX";
    char *tmpdir = mkdtemp(tmpl);
    if (!tmpdir) {
        fprintf(stderr, "mkdtemp failed: %s\n", strerror(errno));
        return NULL;
    }

    char dst[PATH_MAX];
    snprintf(dst, sizeof(dst), "%s/cookies.sqlite", tmpdir);
    if (copy_file(src, dst) != 0) {
        fprintf(stderr, "copy cookies.sqlite failed\n");
        rmdir(tmpdir);
        return NULL;
    }

    char src_wal[PATH_MAX + 8], dst_wal[PATH_MAX + 8];
    snprintf(src_wal, sizeof(src_wal), "%s-wal", src);
    snprintf(dst_wal, sizeof(dst_wal), "%s-wal", dst);
    copy_file(src_wal, dst_wal);

    char src_shm[PATH_MAX + 8], dst_shm[PATH_MAX + 8];
    snprintf(src_shm, sizeof(src_shm), "%s-shm", src);
    snprintf(dst_shm, sizeof(dst_shm), "%s-shm", dst);
    copy_file(src_shm, dst_shm);

    sqlite3 *db = NULL;
    if (sqlite3_open(dst, &db) != SQLITE_OK) {
        fprintf(stderr, "open cookies db failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        unlink(dst);
        unlink(dst_wal);
        unlink(dst_shm);
        rmdir(tmpdir);
        return NULL;
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(
        db,
        "select name,value from moz_cookies where host like '%deepseek%'",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "prepare cookie query failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        unlink(dst);
        unlink(dst_wal);
        unlink(dst_shm);
        rmdir(tmpdir);
        return NULL;
    }

    strbuf_t buf = {0};
    int first = 1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        const unsigned char *value = sqlite3_column_text(stmt, 1);
        if (!name || !value) {
            continue;
        }
        if (!first && strbuf_append_cstr(&buf, "; ") != 0) {
            break;
        }
        first = 0;
        if (strbuf_append_cstr(&buf, (const char *)name) != 0 ||
            strbuf_append_cstr(&buf, "=") != 0 ||
            strbuf_append_cstr(&buf, (const char *)value) != 0) {
            break;
        }
    }
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "cookie query failed: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    unlink(dst);
    unlink(dst_wal);
    unlink(dst_shm);
    rmdir(tmpdir);

    if (!buf.data) {
        strbuf_append_cstr(&buf, "");
    }
    return buf.data ? buf.data : strdup("");
}

int dpsk_auth_load(dpsk_auth_t *auth) {
    if (!auth) {
        return -1;
    }
    memset(auth, 0, sizeof(*auth));
    auth->token = read_token();
    if (!auth->token) {
        return -1;
    }
    auth->cookie = read_cookies();
    if (!auth->cookie) {
        dpsk_auth_free(auth);
        return -1;
    }
    return 0;
}

void dpsk_auth_free(dpsk_auth_t *auth) {
    if (!auth) {
        return;
    }
    free(auth->token);
    free(auth->cookie);
    memset(auth, 0, sizeof(*auth));
}

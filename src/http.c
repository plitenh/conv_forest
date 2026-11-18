#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct write_ctx {
    char *data;
    size_t len;
    size_t cap;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct write_ctx *ctx = userdata;
    size_t incoming = size * nmemb;
    if (incoming == 0) {
        return 0;
    }
    size_t need = ctx->len + incoming + 1;
    if (need > ctx->cap) {
        size_t cap = ctx->cap ? ctx->cap : 32768;
        while (cap < need) {
            cap *= 2;
        }
        char *next = realloc(ctx->data, cap);
        if (!next) {
            return 0;
        }
        ctx->data = next;
        ctx->cap = cap;
    }
    memcpy(ctx->data + ctx->len, ptr, incoming);
    ctx->len += incoming;
    ctx->data[ctx->len] = '\0';
    return incoming;
}

int dpsk_http_init(void) {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void dpsk_http_cleanup(void) {
    curl_global_cleanup();
}

void dpsk_http_buf_free(dpsk_http_buf_t *buf) {
    if (!buf) {
        return;
    }
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

int dpsk_http_get(const char *url,
                  const dpsk_auth_t *auth,
                  dpsk_http_buf_t *out) {
    if (!url || !auth || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    struct write_ctx ctx = {0};
    struct curl_slist *headers = NULL;
    char authorization[8192];
    char cookie_header[65536];

    snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s",
             auth->token ? auth->token : "");
    snprintf(cookie_header, sizeof(cookie_header), "Cookie: %s",
             auth->cookie ? auth->cookie : "");

    headers = curl_slist_append(headers, authorization);
    headers = curl_slist_append(headers, cookie_header);
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0");
    headers = curl_slist_append(headers,
                                "Accept: application/json, text/plain, */*");
    headers = curl_slist_append(headers, "Origin: https://chat.deepseek.com");
    headers = curl_slist_append(headers, "Referer: https://chat.deepseek.com/");

    CURLcode rc = CURLE_OK;
    rc = curl_easy_setopt(curl, CURLOPT_URL, url);
    if (rc == CURLE_OK) rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (rc == CURLE_OK) rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    if (rc == CURLE_OK) rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    if (rc == CURLE_OK) rc = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (rc == CURLE_OK) rc = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    if (rc == CURLE_OK) rc = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);

    if (rc != CURLE_OK) {
        fprintf(stderr, "curl setup failed: %s\n", curl_easy_strerror(rc));
    } else {
        rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            fprintf(stderr, "curl get failed: %s\n", curl_easy_strerror(rc));
        } else {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            if (status < 200 || status >= 300) {
                fprintf(stderr, "http status %ld for %s\n", status, url);
                rc = CURLE_HTTP_RETURNED_ERROR;
            }
        }
    }

    if (rc == CURLE_OK && ctx.data) {
        out->data = ctx.data;
        out->len = ctx.len;
    } else {
        free(ctx.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK ? 0 : -1;
}

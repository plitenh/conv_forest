#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "capture.h"

#include <cjson/cJSON.h>
#include <errno.h>
#include <limits.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "http.h"

#define DPSK_BASE "https://chat.deepseek.com"

typedef struct strbuf {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

typedef struct capture_msg {
    int64_t message_id;
    int64_t parent_id;
    char *role;
    char *text;
    cJSON *raw;
} capture_msg_t;

typedef struct capture_session {
    char *root_key;
    char *uri;
    char *session_id;
    char *title;
    capture_msg_t *msgs;
    size_t msg_count;
    int **children;
    size_t *child_count;
    size_t *child_cap;
    int *roots;
    size_t root_count;
    size_t leaf_count;
    size_t branch_count;
    size_t path_count;
    int **paths;
    size_t *path_lens;
} capture_session_t;

static int strbuf_reserve(strbuf_t *b, size_t extra) {
    if (extra > (size_t)-1 - b->len) return -1;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) { cap = need; break; }
        cap *= 2;
    }
    char *next = realloc(b->data, cap);
    if (!next) return -1;
    b->data = next;
    b->cap = cap;
    return 0;
}

static int strbuf_append(strbuf_t *b, const char *s, size_t n) {
    if (!s) { s = ""; n = 0; }
    if (strbuf_reserve(b, n) != 0) return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int strbuf_append_cstr(strbuf_t *b, const char *s) {
    return strbuf_append(b, s, s ? strlen(s) : 0);
}

static void strbuf_free(strbuf_t *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static char *make_url(const char *base, const char *path, const char *query) {
    size_t base_len = strlen(base);
    size_t path_len = strlen(path);
    size_t query_len = query ? strlen(query) : 0;
    size_t total = base_len + path_len + (query_len ? query_len + 1 : 0) + 1;
    char *url = malloc(total);
    if (!url) return NULL;
    if (query_len) {
        snprintf(url, total, "%s%s?%s", base, path, query);
    } else {
        snprintf(url, total, "%s%s", base, path);
    }
    return url;
}

static cJSON *http_get_json(const char *url, const dpsk_auth_t *auth) {
    dpsk_http_buf_t body = {0};
    if (dpsk_http_get(url, auth, &body) != 0) {
        fprintf(stderr, "GET failed: %s\n", url);
        dpsk_http_buf_free(&body);
        return NULL;
    }
    cJSON *json = cJSON_Parse(body.data ? body.data : "");
    if (!json) {
        fprintf(stderr, "JSON parse failed for %s\n", url);
    }
    dpsk_http_buf_free(&body);
    return json;
}

static void sha256_hex(const char *text, char out[65]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)text, strlen(text), digest);
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    out[64] = '\0';
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return -1;
    memcpy(tmp, path, n + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static double json_number_default(cJSON *obj, const char *key, double def) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : def;
}

static int json_bool_default(cJSON *obj, const char *key, int def) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return def;
}

static const char *json_string_default(cJSON *obj, const char *key,
                                       const char *def) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : def;
}

static int64_t json_int_default(cJSON *obj, const char *key, int64_t def) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (int64_t)item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring) return strtoll(item->valuestring, NULL, 10);
    return def;
}

static char *extract_text(cJSON *raw) {
    strbuf_t buf = {0};
    cJSON *fragments = cJSON_GetObjectItemCaseSensitive(raw, "fragments");
    int pieces = 0;
    if (cJSON_IsArray(fragments)) {
        int n = cJSON_GetArraySize(fragments);
        for (int i = 0; i < n; i++) {
            cJSON *frag = cJSON_GetArrayItem(fragments, i);
            const char *type = json_string_default(frag, "type", "");
            strbuf_t piece = {0};
            if (strcmp(type, "FILE") == 0) {
                cJSON *files = cJSON_GetObjectItemCaseSensitive(frag, "files");
                strbuf_append_cstr(&piece, "[files: ");
                if (cJSON_IsArray(files)) {
                    int fn = cJSON_GetArraySize(files);
                    for (int j = 0; j < fn; j++) {
                        cJSON *file = cJSON_GetArrayItem(files, j);
                        const char *name = json_string_default(file, "file_name", "");
                        if (j) strbuf_append_cstr(&piece, ", ");
                        strbuf_append_cstr(&piece, name);
                    }
                }
                strbuf_append_cstr(&piece, "]");
            } else {
                const char *content = json_string_default(frag, "content", "");
                if (content[0]) strbuf_append_cstr(&piece, content);
            }
            if (piece.len > 0) {
                if (pieces++) strbuf_append_cstr(&buf, "\n\n");
                strbuf_append_cstr(&buf, piece.data);
            }
            strbuf_free(&piece);
        }
    }
    if (!buf.data) strbuf_append_cstr(&buf, "");
    return buf.data ? buf.data : strdup("");
}

static int find_index_by_id(capture_session_t *s, int64_t message_id) {
    for (size_t i = 0; i < s->msg_count; i++) {
        if (s->msgs[i].message_id == message_id) return (int)i;
    }
    return -1;
}

static int append_child(capture_session_t *s, int parent_idx, int child_idx) {
    size_t n = s->child_count[parent_idx];
    if (n == s->child_cap[parent_idx]) {
        size_t cap = s->child_cap[parent_idx] ? s->child_cap[parent_idx] * 2 : 4;
        int *next = realloc(s->children[parent_idx], cap * sizeof(int));
        if (!next) return -1;
        s->children[parent_idx] = next;
        s->child_cap[parent_idx] = cap;
    }
    s->children[parent_idx][n] = child_idx;
    s->child_count[parent_idx] = n + 1;
    return 0;
}

static int build_tree(capture_session_t *s) {
    s->children = calloc(s->msg_count, sizeof(int *));
    s->child_count = calloc(s->msg_count, sizeof(size_t));
    s->child_cap = calloc(s->msg_count, sizeof(size_t));
    if (!s->children || !s->child_count || !s->child_cap) return -1;

    s->roots = calloc(s->msg_count ? s->msg_count : 1, sizeof(int));
    if (!s->roots) return -1;
    s->root_count = 0;

    for (size_t i = 0; i < s->msg_count; i++) {
        if (s->msgs[i].parent_id < 0) {
            s->roots[s->root_count++] = (int)i;
        } else {
            int parent = find_index_by_id(s, s->msgs[i].parent_id);
            if (parent < 0) {
                /* Treat dangling parents as extra roots; the first three
                 * verified sessions are connected, but this keeps export
                 * lossless if the API ever returns a dangling edge. */
                s->roots[s->root_count++] = (int)i;
            } else if (append_child(s, parent, (int)i) != 0) {
                return -1;
            }
        }
    }

    s->leaf_count = 0;
    s->branch_count = 0;
    for (size_t i = 0; i < s->msg_count; i++) {
        if (s->child_count[i] == 0) s->leaf_count++;
        if (s->child_count[i] > 1) s->branch_count++;
    }
    return 0;
}

static void collect_path_dfs(capture_session_t *s, int idx, int *path,
                             size_t depth) {
    path[depth++] = idx;
    if (s->child_count[idx] == 0) {
        int **next_paths = realloc(s->paths, (s->path_count + 1) * sizeof(int *));
        size_t *next_lens = realloc(s->path_lens, (s->path_count + 1) * sizeof(size_t));
        if (!next_paths || !next_lens) return;
        s->paths = next_paths;
        s->path_lens = next_lens;
        s->paths[s->path_count] = malloc(depth * sizeof(int));
        if (!s->paths[s->path_count]) return;
        memcpy(s->paths[s->path_count], path, depth * sizeof(int));
        s->path_lens[s->path_count] = depth;
        s->path_count++;
        return;
    }
    for (size_t i = 0; i < s->child_count[idx]; i++) {
        collect_path_dfs(s, s->children[idx][i], path, depth);
    }
}

static int collect_paths(capture_session_t *s) {
    if (s->msg_count == 0) return 0;
    int *path = malloc(s->msg_count * sizeof(int));
    if (!path) return -1;
    for (size_t r = 0; r < s->root_count; r++) {
        collect_path_dfs(s, s->roots[r], path, 0);
    }
    free(path);
    return 0;
}

static int export_messages(capture_session_t *s, const char *dir) {
    if (mkdir_p(dir) != 0) return -1;
    char *path = NULL;
    if (asprintf(&path, "%s/messages.jsonl", dir) < 0) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        free(path);
        return -1;
    }

    for (size_t i = 0; i < s->msg_count; i++) {
        capture_msg_t *m = &s->msgs[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj) break;

        cJSON_AddNumberToObject(obj, "message_id", (double)m->message_id);
        if (m->parent_id < 0) cJSON_AddNullToObject(obj, "parent_id");
        else cJSON_AddNumberToObject(obj, "parent_id", (double)m->parent_id);

        cJSON_AddStringToObject(obj, "role", m->role ? m->role : "");
        cJSON_AddStringToObject(obj, "model", json_string_default(m->raw, "model", ""));
        cJSON_AddBoolToObject(obj, "thinking_enabled", json_bool_default(m->raw, "thinking_enabled", 0));
        cJSON_AddStringToObject(obj, "status", json_string_default(m->raw, "status", ""));
        cJSON_AddNumberToObject(obj, "inserted_at", json_number_default(m->raw, "inserted_at", 0));
        cJSON_AddNumberToObject(obj, "accumulated_token_usage",
                                json_number_default(m->raw, "accumulated_token_usage", 0));
        cJSON_AddBoolToObject(obj, "search_enabled", json_bool_default(m->raw, "search_enabled", 0));
        cJSON_AddStringToObject(obj, "text", m->text ? m->text : "");

        cJSON *fragments = cJSON_GetObjectItemCaseSensitive(m->raw, "fragments");
        if (cJSON_IsArray(fragments)) {
            cJSON_AddItemToObject(obj, "fragments", cJSON_Duplicate(fragments, 1));
        } else {
            cJSON_AddItemToObject(obj, "fragments", cJSON_CreateArray());
        }

        char *line = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (!line) continue;
        fputs(line, fp);
        fputc('\n', fp);
        free(line);
    }

    int ok = fclose(fp) == 0 ? 0 : -1;
    free(path);
    return ok;
}

static cJSON *nodes_json(capture_session_t *s) {
    cJSON *nodes = cJSON_CreateObject();
    if (!nodes) return NULL;
    for (size_t i = 0; i < s->msg_count; i++) {
        char key[64];
        snprintf(key, sizeof(key), "%lld", (long long)s->msgs[i].message_id);
        cJSON *node = cJSON_CreateObject();
        cJSON_AddNumberToObject(node, "message_id", (double)s->msgs[i].message_id);
        if (s->msgs[i].parent_id < 0) cJSON_AddNullToObject(node, "parent_id");
        else cJSON_AddNumberToObject(node, "parent_id", (double)s->msgs[i].parent_id);
        cJSON_AddStringToObject(node, "role", s->msgs[i].role ? s->msgs[i].role : "");
        cJSON_AddNumberToObject(node, "text_len", (double)(s->msgs[i].text ? strlen(s->msgs[i].text) : 0));
        cJSON_AddItemToObject(nodes, key, node);
    }
    return nodes;
}

static cJSON *edges_json(capture_session_t *s) {
    cJSON *edges = cJSON_CreateArray();
    if (!edges) return NULL;
    for (size_t p = 0; p < s->msg_count; p++) {
        for (size_t c = 0; c < s->child_count[p]; c++) {
            int child = s->children[p][c];
            cJSON *edge = cJSON_CreateObject();
            cJSON_AddNumberToObject(edge, "parent", (double)s->msgs[p].message_id);
            cJSON_AddNumberToObject(edge, "child", (double)s->msgs[child].message_id);
            cJSON_AddItemToArray(edges, edge);
        }
    }
    return edges;
}

static cJSON *id_list_json(capture_session_t *s, const char *kind) {
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;
    if (strcmp(kind, "roots") == 0) {
        for (size_t i = 0; i < s->root_count; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)s->msgs[s->roots[i]].message_id));
        }
    } else if (strcmp(kind, "leaves") == 0) {
        for (size_t i = 0; i < s->msg_count; i++) {
            if (s->child_count[i] == 0) {
                cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)s->msgs[i].message_id));
            }
        }
    } else if (strcmp(kind, "branch_points") == 0) {
        for (size_t i = 0; i < s->msg_count; i++) {
            if (s->child_count[i] > 1) {
                cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)s->msgs[i].message_id));
            }
        }
    }
    return arr;
}

static cJSON *paths_json(capture_session_t *s) {
    cJSON *paths = cJSON_CreateArray();
    if (!paths) return NULL;
    for (size_t p = 0; p < s->path_count; p++) {
        cJSON *path = cJSON_CreateArray();
        for (size_t d = 0; d < s->path_lens[p]; d++) {
            cJSON_AddItemToArray(path,
                                 cJSON_CreateNumber((double)s->msgs[s->paths[p][d]].message_id));
        }
        cJSON_AddItemToArray(paths, path);
    }
    return paths;
}

static int export_tree(capture_session_t *s, const char *dir) {
    if (mkdir_p(dir) != 0) return -1;
    char *path = NULL;
    if (asprintf(&path, "%s/tree.json", dir) < 0) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        free(path);
        return -1;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) { fclose(fp); free(path); return -1; }
    cJSON_AddStringToObject(root, "root_key", s->root_key ? s->root_key : "");
    cJSON_AddStringToObject(root, "uri", s->uri ? s->uri : "");
    cJSON_AddStringToObject(root, "session_id", s->session_id ? s->session_id : "");
    cJSON_AddStringToObject(root, "title", s->title ? s->title : "");
    cJSON_AddItemToObject(root, "roots", id_list_json(s, "roots"));
    cJSON_AddItemToObject(root, "nodes", nodes_json(s));
    cJSON_AddItemToObject(root, "edges", edges_json(s));
    cJSON_AddItemToObject(root, "leaves", id_list_json(s, "leaves"));
    cJSON_AddItemToObject(root, "branch_points", id_list_json(s, "branch_points"));
    cJSON_AddItemToObject(root, "paths", paths_json(s));
    cJSON_AddNumberToObject(root, "message_count", (double)s->msg_count);
    cJSON_AddNumberToObject(root, "leaf_count", (double)s->leaf_count);
    cJSON_AddNumberToObject(root, "branch_point_count", (double)s->branch_count);
    cJSON_AddNumberToObject(root, "path_count", (double)s->path_count);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) { fclose(fp); free(path); return -1; }
    fputs(json, fp);
    fputc('\n', fp);
    free(json);
    int ok = fclose(fp) == 0 ? 0 : -1;
    free(path);
    return ok;
}

static void free_session(capture_session_t *s) {
    free(s->root_key);
    free(s->uri);
    free(s->session_id);
    free(s->title);
    for (size_t i = 0; i < s->msg_count; i++) {
        free(s->msgs[i].role);
        free(s->msgs[i].text);
        cJSON_Delete(s->msgs[i].raw);
    }
    free(s->msgs);
    for (size_t i = 0; i < s->msg_count; i++) free(s->children[i]);
    free(s->children);
    free(s->child_count);
    free(s->child_cap);
    free(s->roots);
    for (size_t i = 0; i < s->path_count; i++) free(s->paths[i]);
    free(s->paths);
    free(s->path_lens);
    memset(s, 0, sizeof(*s));
}

static int parse_history(cJSON *root, const char *uri,
                         const char *session_id, capture_session_t *s) {
    memset(s, 0, sizeof(*s));
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *biz = data ? cJSON_GetObjectItemCaseSensitive(data, "biz_data") : NULL;
    if (!biz) return -1;

    cJSON *messages = cJSON_GetObjectItemCaseSensitive(biz, "chat_messages");
    cJSON *chat_session = cJSON_GetObjectItemCaseSensitive(biz, "chat_session");
    if (!cJSON_IsArray(messages)) return -1;

    s->uri = strdup(uri);
    s->session_id = strdup(session_id);
    s->title = strdup(json_string_default(chat_session, "title", ""));

    char digest[65];
    sha256_hex(uri, digest);
    size_t root_len = strlen(digest) + 1 + strlen(uri) + 1;
    s->root_key = malloc(root_len);
    if (!s->root_key) return -1;
    snprintf(s->root_key, root_len, "%s:%s", digest, uri);

    int n = cJSON_GetArraySize(messages);
    s->msgs = calloc(n ? (size_t)n : 1, sizeof(capture_msg_t));
    if (!s->msgs) return -1;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(messages, i);
        capture_msg_t *m = &s->msgs[s->msg_count];
        m->raw = cJSON_Duplicate(item, 1);
        m->message_id = json_int_default(item, "message_id", -1);
        cJSON *pid = cJSON_GetObjectItemCaseSensitive(item, "parent_id");
        if (cJSON_IsNull(pid) || !pid) m->parent_id = -1;
        else if (cJSON_IsNumber(pid)) m->parent_id = (int64_t)pid->valuedouble;
        else if (cJSON_IsString(pid) && pid->valuestring) m->parent_id = strtoll(pid->valuestring, NULL, 10);
        else m->parent_id = -1;
        const char *role = json_string_default(item, "role", "");
        m->role = strdup(role ? role : "");
        m->text = extract_text(item);
        s->msg_count++;
    }

    if (build_tree(s) != 0) return -1;
    if (collect_paths(s) != 0) return -1;
    return 0;
}

static cJSON *fetch_all_sessions(const dpsk_auth_t *auth, size_t *count) {
    cJSON *all = cJSON_CreateArray();
    if (!all) return NULL;

    char cursor[128];
    cursor[0] = '\0';

    for (int page = 0; page < 1000; page++) {
        strbuf_t query = {0};
        strbuf_append_cstr(&query, "lte_cursor.pinned=false&count=100");
        if (cursor[0]) {
            strbuf_append_cstr(&query, "&lte_cursor.updated_at=");
            strbuf_append_cstr(&query, cursor);
        }
        char *url = make_url(DPSK_BASE, "/api/v0/chat_session/fetch_page", query.data);
        strbuf_free(&query);
        if (!url) {
            cJSON_Delete(all);
            return NULL;
        }

        cJSON *root = http_get_json(url, auth);
        free(url);
        if (!root) {
            cJSON_Delete(all);
            return NULL;
        }

        cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
        cJSON *biz = data ? cJSON_GetObjectItemCaseSensitive(data, "biz_data") : NULL;
        cJSON *sessions = biz ? cJSON_GetObjectItemCaseSensitive(biz, "chat_sessions") : NULL;
        int has_more = biz ? json_bool_default(biz, "has_more", 0) : 0;

        if (!cJSON_IsArray(sessions)) {
            cJSON_Delete(root);
            cJSON_Delete(all);
            return NULL;
        }

        int n = cJSON_GetArraySize(sessions);
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(sessions, i);
            cJSON_AddItemToArray(all, cJSON_Duplicate(item, 1));
        }

        if (!has_more || n == 0) {
            cJSON_Delete(root);
            break;
        }

        cJSON *last = cJSON_GetArrayItem(sessions, n - 1);
        double updated_at = json_number_default(last, "updated_at", 0);
        snprintf(cursor, sizeof(cursor), "%.17g", updated_at);
        cJSON_Delete(root);
    }

    *count = (size_t)cJSON_GetArraySize(all);
    if (*count == 0) {
        cJSON_Delete(all);
        return NULL;
    }
    return all;
}

static cJSON *fetch_history(const char *session_id, const dpsk_auth_t *auth) {
    size_t qlen = strlen("chat_session_id=") + strlen(session_id) + 1;
    char *query = malloc(qlen);
    if (!query) return NULL;
    snprintf(query, qlen, "chat_session_id=%s", session_id);
    char *url = make_url(DPSK_BASE, "/api/v0/chat/history_messages", query);
    free(query);
    if (!url) return NULL;
    cJSON *root = http_get_json(url, auth);
    free(url);
    return root;
}

static int seen_has(char **seen, size_t seen_count, const char *id) {
    for (size_t i = 0; i < seen_count; i++) {
        if (strcmp(seen[i], id) == 0) return 1;
    }
    return 0;
}

static int seen_add(char ***seen_ptr, size_t *seen_count, size_t *seen_cap,
                    const char *id) {
    char **seen = *seen_ptr;
    if (*seen_count == *seen_cap) {
        size_t cap = *seen_cap ? *seen_cap * 2 : 64;
        char **next = realloc(seen, cap * sizeof(char *));
        if (!next) return -1;
        seen = next;
        *seen_ptr = seen;
        *seen_cap = cap;
    }
    seen[*seen_count] = strdup(id);
    if (!seen[*seen_count]) return -1;
    (*seen_count)++;
    return 0;
}

int dpsk_capture_forest(const char *base_url,
                       const dpsk_auth_t *auth,
                       const char *out_root) {
    (void)base_url;
    if (mkdir_p(out_root) != 0) return -1;

    size_t total = 0;
    cJSON *sessions = fetch_all_sessions(auth, &total);
    if (!sessions) return -1;
    if (total == 0) {
        cJSON_Delete(sessions);
        return -1;
    }

    char **seen = NULL;
    size_t seen_count = 0;
    size_t seen_cap = 0;

    cJSON *report = cJSON_CreateArray();
    cJSON *forest = cJSON_CreateObject();
    cJSON *forest_sessions = cJSON_CreateArray();
    cJSON *forest_paths = cJSON_CreateArray();
    size_t captured_sessions = 0;
    size_t total_messages = 0;
    size_t total_paths = 0;

    for (size_t i = 0; i < total; i++) {
        cJSON *sess = cJSON_GetArrayItem(sessions, (int)i);
        const char *sid = json_string_default(sess, "id", "");
        const char *list_title = json_string_default(sess, "title", "");
        if (!sid[0]) continue;
        if (seen_has(seen, seen_count, sid)) continue;
        if (seen_add(&seen, &seen_count, &seen_cap, sid) != 0) {
            for (size_t j = 0; j < seen_count; j++) free(seen[j]);
            free(seen);
            cJSON_Delete(sessions);
            return -1;
        }

        char uri[512];
        snprintf(uri, sizeof(uri), "%s/a/chat/s/%s", DPSK_BASE, sid);
        cJSON *history = fetch_history(sid, auth);
        if (!history) {
            fprintf(stderr, "history fetch failed for %s\n", sid);
            continue;
        }

        capture_session_t cap;
        if (parse_history(history, uri, sid, &cap) != 0) {
            fprintf(stderr, "history parse failed for %s\n", sid);
            cJSON_Delete(history);
            continue;
        }

        char digest[65];
        sha256_hex(uri, digest);
        char dir[PATH_MAX + 32];
        snprintf(dir, sizeof(dir), "%s/%s", out_root, digest);
        int export_ok = export_messages(&cap, dir) == 0 && export_tree(&cap, dir) == 0;

        cJSON *session_entry = cJSON_CreateObject();
        cJSON_AddStringToObject(session_entry, "session_id", sid);
        cJSON_AddStringToObject(session_entry, "title", cap.title ? cap.title : list_title);
        cJSON_AddStringToObject(session_entry, "root_key", cap.root_key);
        cJSON_AddStringToObject(session_entry, "uri", cap.uri);
        cJSON_AddNumberToObject(session_entry, "message_count", (double)cap.msg_count);
        cJSON_AddNumberToObject(session_entry, "root_count", (double)cap.root_count);
        cJSON_AddNumberToObject(session_entry, "leaf_count", (double)cap.leaf_count);
        cJSON_AddNumberToObject(session_entry, "branch_point_count", (double)cap.branch_count);
        cJSON_AddNumberToObject(session_entry, "path_count", (double)cap.path_count);
        cJSON_AddItemToObject(session_entry, "paths", paths_json(&cap));
        cJSON_AddItemToArray(forest_sessions, session_entry);

        for (size_t p = 0; p < cap.path_count; p++) {
            cJSON *path = cJSON_CreateObject();
            cJSON_AddNumberToObject(path, "path_id", (double)total_paths);
            cJSON_AddStringToObject(path, "session_id", sid);
            cJSON_AddStringToObject(path, "root_key", cap.root_key);
            cJSON_AddNumberToObject(path, "local_path_no", (double)p);
            cJSON *ids = cJSON_CreateArray();
            for (size_t d = 0; d < cap.path_lens[p]; d++) {
                cJSON_AddItemToArray(ids, cJSON_CreateNumber((double)cap.msgs[cap.paths[p][d]].message_id));
            }
            cJSON_AddItemToObject(path, "message_ids", ids);
            cJSON_AddItemToArray(forest_paths, path);
            total_paths++;
        }

        captured_sessions++;
        total_messages += cap.msg_count;

        cJSON *sum = cJSON_CreateObject();
        cJSON_AddStringToObject(sum, "session_id", sid);
        cJSON_AddStringToObject(sum, "title", cap.title ? cap.title : list_title);
        cJSON_AddNumberToObject(sum, "message_count", (double)cap.msg_count);
        cJSON_AddNumberToObject(sum, "root_count", (double)cap.root_count);
        cJSON_AddNumberToObject(sum, "leaf_count", (double)cap.leaf_count);
        cJSON_AddNumberToObject(sum, "branch_point_count", (double)cap.branch_count);
        cJSON_AddNumberToObject(sum, "path_count", (double)cap.path_count);
        cJSON_AddStringToObject(sum, "root_key", cap.root_key);
        cJSON_AddBoolToObject(sum, "export_ok", export_ok);
        cJSON_AddItemToArray(report, sum);

        char *line = cJSON_PrintUnformatted(sum);
        if (line) { puts(line); free(line); }
        cJSON_Delete(history);
        free_session(&cap);
    }

    char report_path[PATH_MAX];
    snprintf(report_path, sizeof(report_path), "%s/capture_report.json", out_root);
    FILE *fp = fopen(report_path, "wb");
    if (fp) {
        char *json = cJSON_Print(report);
        if (json) {
            fputs(json, fp);
            fputc('\n', fp);
            free(json);
        }
        fclose(fp);
    }

    cJSON_AddStringToObject(forest, "backup_root", out_root);
    cJSON_AddNumberToObject(forest, "session_count", (double)captured_sessions);
    cJSON_AddNumberToObject(forest, "total_messages", (double)total_messages);
    cJSON_AddNumberToObject(forest, "total_paths", (double)total_paths);
    cJSON_AddItemToObject(forest, "sessions", forest_sessions);
    cJSON_AddItemToObject(forest, "paths", forest_paths);

    char forest_path[PATH_MAX + 32];
    snprintf(forest_path, sizeof(forest_path), "%s/forest.json", out_root);
    FILE *ffp = fopen(forest_path, "wb");
    if (ffp) {
        char *json = cJSON_Print(forest);
        if (json) {
            fputs(json, ffp);
            fputc('\n', ffp);
            free(json);
        }
        fclose(ffp);
    }

    for (size_t j = 0; j < seen_count; j++) free(seen[j]);
    free(seen);
    cJSON_Delete(forest);
    cJSON_Delete(report);
    cJSON_Delete(sessions);
    return 0;
}

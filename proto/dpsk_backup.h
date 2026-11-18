/*
 * dpsk_backup.h
 *
 * DeepSeek Web backup data model.
 *
 * Forest model:
 *   forest -> session roots -> session static branch tree -> path leaves.
 * The forest is built from the already-acquired session list (loop1), not
 * from a global tree. Each root is indexed by:
 *
 *   root_key = sha256_hex(uri) + ":" + uri
 *
 * Inside a session, branch alternatives are represented as a static message
 * forest derived from API history data: parent_id links form one or more
 * roots, and a node with multiple children is a branch point. DFS enumerates
 * each root-to-leaf path as one unique conversation. DOM arrow clicking is
 * not part of this model. PoW-like protection is handled out-of-process
 * with uinput + OCR.
 */

#ifndef DPSK_BACKUP_H
#define DPSK_BACKUP_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    DPSK_ROLE_UNKNOWN = 0,
    DPSK_ROLE_USER,
    DPSK_ROLE_ASSISTANT,
    DPSK_ROLE_SYSTEM
} dpsk_role_t;

/* One rendered message node from the virtual list. */
typedef struct dpsk_msg dpsk_msg_t;
struct dpsk_msg {
    int64_t message_id;          /* API message_id */
    int64_t parent_id;           /* API parent_id; null = root */
    dpsk_role_t role;
    char *text;                  /* innerText or markdown source */
    size_t text_len;
    dpsk_msg_t *next;            /* flat pool list */
    dpsk_msg_t *first_child;     /* message tree */
    dpsk_msg_t *next_sibling;    /* message tree */
    size_t child_count;
};

/* One node on a root-to-leaf traversal path. */
typedef struct dpsk_path_node dpsk_path_node_t;
struct dpsk_path_node {
    dpsk_msg_t *message;
    dpsk_path_node_t *parent;
    dpsk_path_node_t *first_child;
    dpsk_path_node_t *next_sibling;
};

/* Static message tree for one session. */
typedef struct dpsk_static_tree dpsk_static_tree_t;
struct dpsk_static_tree {
    dpsk_msg_t **roots;
    size_t root_count;
    size_t message_count;
    dpsk_path_node_t *root;      /* synthetic root; children are branch paths */
    size_t leaf_count;
};

/* One session = one root in the forest. */
typedef struct dpsk_session_root dpsk_session_root_t;
struct dpsk_session_root {
    char *root_key;              /* sha256_hex(uri) + ":" + uri */
    char *uri;                   /* full /a/chat/s/<id> URI */
    char *session_id;            /* UUID parsed from uri, used for API calls */
    char *title;
    dpsk_msg_t *messages;        /* flat message pool, ordered by key */
    size_t message_count;
    dpsk_static_tree_t tree;
    dpsk_session_root_t *next;
};

/* Forest built from the acquired session list. */
typedef struct dpsk_forest dpsk_forest_t;
struct dpsk_forest {
    dpsk_session_root_t *roots;
    size_t session_count;
    char *backup_root;
};

/* Storage helpers keep the C model and SQLite index in sync. */
int dpsk_forest_init(dpsk_forest_t *forest, const char *backup_root);
void dpsk_forest_free(dpsk_forest_t *forest);

int dpsk_root_key_from_uri(const char *uri, char *out, size_t out_len);

int dpsk_session_add(dpsk_forest_t *forest,
                     const char *uri,
                     const char *title,
                     dpsk_session_root_t **session_out);

int dpsk_msg_append(dpsk_session_root_t *session,
                    int64_t message_id,
                    int64_t parent_id,
                    dpsk_role_t role,
                    const char *text,
                    size_t text_len);

int dpsk_tree_build(dpsk_session_root_t *session);

int dpsk_tree_walk(dpsk_static_tree_t *tree,
                   int (*on_leaf)(const dpsk_session_root_t *session,
                                  const dpsk_path_node_t *leaf,
                                  void *user),
                   void *user);

/* libcurl-backed persistence prototypes. */
int dpsk_export_markdown(const dpsk_session_root_t *session,
                         const dpsk_path_node_t *leaf,
                         const char *out_dir);

int dpsk_storage_open(const char *db_path);
int dpsk_storage_write_session(const dpsk_session_root_t *session);
int dpsk_storage_write_message(const char *root_key, const dpsk_msg_t *msg);
int dpsk_storage_write_path(const char *root_key,
                            int path_no,
                            const dpsk_path_node_t *leaf);

#endif /* DPSK_BACKUP_H */

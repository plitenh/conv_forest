PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

/* Forest roots: one row per acquired session list item. */
CREATE TABLE IF NOT EXISTS sessions (
    root_key    TEXT PRIMARY KEY,  /* sha256_hex(uri) + ':' + uri */
    uri         TEXT NOT NULL UNIQUE,
    session_id  TEXT NOT NULL,     /* UUID parsed from uri, for API calls */
    title       TEXT NOT NULL,
    updated_at  REAL,
    backup_time TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

/* Flat message pool per session. */
CREATE TABLE IF NOT EXISTS messages (
    root_key   TEXT NOT NULL,
    message_id INTEGER NOT NULL,
    parent_id  INTEGER,
    role       TEXT NOT NULL CHECK (role IN ('user','assistant','system','unknown')),
    text       TEXT NOT NULL,
    text_len   INTEGER NOT NULL,
    PRIMARY KEY (root_key, message_id),
    FOREIGN KEY (root_key) REFERENCES sessions(root_key) ON DELETE CASCADE
);

/* Unique branch paths produced by loop3. */
CREATE TABLE IF NOT EXISTS branch_paths (
    root_key   TEXT NOT NULL,
    path_no    INTEGER NOT NULL,
    depth      INTEGER NOT NULL,
    message_id INTEGER NOT NULL,
    parent_id  INTEGER,
    PRIMARY KEY (root_key, path_no, depth),
    FOREIGN KEY (root_key) REFERENCES sessions(root_key) ON DELETE CASCADE,
    FOREIGN KEY (root_key, message_id) REFERENCES messages(root_key, message_id)
);

CREATE INDEX IF NOT EXISTS idx_messages_root_key ON messages(root_key, message_id);
CREATE INDEX IF NOT EXISTS idx_messages_parent ON messages(root_key, parent_id);
CREATE INDEX IF NOT EXISTS idx_branch_paths_root_no ON branch_paths(root_key, path_no);

/* Append-only forest: the built forest and branch paths may only grow. */
CREATE TRIGGER IF NOT EXISTS sessions_no_update
BEFORE UPDATE ON sessions
BEGIN
    SELECT RAISE(ABORT, 'sessions is append-only');
END;

CREATE TRIGGER IF NOT EXISTS sessions_no_delete
BEFORE DELETE ON sessions
BEGIN
    SELECT RAISE(ABORT, 'sessions is append-only');
END;

CREATE TRIGGER IF NOT EXISTS messages_no_update
BEFORE UPDATE ON messages
BEGIN
    SELECT RAISE(ABORT, 'messages is append-only');
END;

CREATE TRIGGER IF NOT EXISTS messages_no_delete
BEFORE DELETE ON messages
BEGIN
    SELECT RAISE(ABORT, 'messages is append-only');
END;

CREATE TRIGGER IF NOT EXISTS branch_paths_no_update
BEFORE UPDATE ON branch_paths
BEGIN
    SELECT RAISE(ABORT, 'branch_paths is append-only');
END;

CREATE TRIGGER IF NOT EXISTS branch_paths_no_delete
BEFORE DELETE ON branch_paths
BEGIN
    SELECT RAISE(ABORT, 'branch_paths is append-only');
END;

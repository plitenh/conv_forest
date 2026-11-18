# conv_forest experiment path

Project language: C. Design files live in `proto/`. `dpsk_backup.h` is the
source of truth for field names and project structure.

## 0. Cookie / auth
- Extract cookie + userToken from the Firefox profile.
- Keep auth handling outside `dpsk_backup.h`.
- OCR deferred.

## 1. Fetch branches into database
- Get session list from curl/API or acquired `root_key` list.
- Visit each `uri` and call `history_messages`.
- Store `sessions`, `messages`, `branch_paths` only.
- Content may be minimal at this stage; structure completeness is the target.
- No benchmark yet; human review required.

## 2. Build forest and branches
- `dpsk_forest_t` from the acquired session list.
- `dpsk_msg_t` tree from `message_id` / `parent_id`.
- `dpsk_tree_build()` creates static message tree.
- `dpsk_tree_walk()` emits root-to-leaf unique conversations.
- Database and in-memory structures are append-only.
- Verify against curl list; human supervises.

## 3. Branch concatenation
- Deferred. Possibly a future path-concatenation format.

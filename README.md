# conv_forest

DeepSeek web conversation backup tool written in C.

It reads the logged-in Firefox profile for DeepSeek credentials, pages through
the full session list, downloads complete message history, reconstructs the
message forest from `message_id` / `parent_id`, enumerates every root-to-leaf
branch path, and writes per-session JSON output.

## Output

Each session is written to:

```text
v2/<sha256(uri)>/tree.json
v2/<sha256(uri)>/messages.jsonl
v2/capture_report.json
```

- `messages.jsonl`: one complete message per line, including original fragments
- `tree.json`: nodes, edges, roots, leaves, branch points, and all branch paths
- `capture_report.json`: one summary entry per captured session

## Build

Uses `libcurl`, `sqlite3`, `cJSON`, and OpenSSL.

```bash
nix-shell -p curl sqlite cjson openssl pkg-config --run 'make clean && make'
```

On non-Nix systems, point `pkg-config` at the four dependencies.

## Run

```bash
nix-shell -p curl sqlite cjson openssl --run './dpsk_capture [OUT_DIR] [COUNT]'
```

- `COUNT=0` (default) captures all sessions
- A positive `COUNT` captures only the first N sessions
- The default output directory is `./v2`

## Firefox profile

By default the profile path is the original local fixture path. Override it
with:

```bash
DPSK_PROFILE_ROOT=/path/to/firefox-profile
```

The profile must contain:

```text
storage/default/https+++chat.deepseek.com/ls/data.sqlite
cookies.sqlite
```

`userToken` is read from `data.sqlite`; DeepSeek cookies are read from
`cookies.sqlite`. Both SQLite files are copied to a temporary directory first
to avoid Firefox write locks.

## Current scope

The current adapter is DeepSeek web. The model and export format are intended
to stay platform-neutral. `proto/dpsk_backup.h` describes the forest and
branch-path data model; `proto/dpsk_external.h` reserves the out-of-process
PoW/uinput/OCR boundary for platforms that need it.

## Prototype

`experiment_connectivity.py` and `capture_first3.py` are the original Python
prototypes used to validate API connectivity and the first three sessions.

# conv_forest

DeepSeek web conversation backup tool written in C.

It reads the logged-in Firefox profile for DeepSeek credentials, pages through
the full session list, downloads complete message history, reconstructs the
message forest from `message_id` / `parent_id`, enumerates every root-to-leaf
branch path, and writes per-session JSON output.

## Output

Each session is written to:

```text
v2/forest.json
v2/<sha256(uri)>/tree.json
v2/<sha256(uri)>/messages.jsonl
v2/capture_report.json
```

- `forest.json`: full-session forest, global path IDs, and per-session path lists
- `messages.jsonl`: one complete message per line, including original fragments
- `tree.json`: nodes, edges, roots, leaves, branch points, and all branch paths
- `capture_report.json`: one summary entry per captured session

## Build

Uses `libcurl`, `sqlite3`, `cJSON`, and OpenSSL.

On Nix:

```bash
nix develop --command sh -c 'make clean && make'
```

Without flakes:

```bash
nix-shell -p curl sqlite cjson openssl pkg-config --run 'make clean && make'
```

On non-Nix systems, point `pkg-config` at the four dependencies.

## Run

```bash
nix-shell -p curl sqlite cjson openssl --run './dpsk_capture [OUT_DIR]'
```

- Export is always the complete session forest; there is no partial-count mode.
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

## Grok UI agent

`grok_ui_agent` is a small OCR/uinput boundary for `grok.com` when a direct
login/API path is blocked by a challenge.

```bash
./grok_ui_agent capture SLOT PNG
./grok_ui_agent ocr PNG
./grok_ui_agent probe SLOT PNG
./grok_ui_agent click SLOT X Y
./grok_ui_agent type TEXT
./grok_ui_agent key KEY
```

Runtime tools: `grim`, `tesseract`, `ydotool`/`ydotoold`.

The default screen is `2560x1440` and slots `7`, `8`, `9` are treated as
workspaces. Override regions with:

```bash
GROK_UI_SCREEN_W=2560
GROK_UI_SCREEN_H=1440
GROK_UI_SLOT_7=0,0,2560,1440
GROK_UI_SLOT_8=0,0,2560,1440
GROK_UI_SLOT_9=0,0,2560,1440
```

## Current scope

The current adapter is DeepSeek web. The model and export format are intended
to stay platform-neutral. `proto/dpsk_backup.h` describes the forest and
branch-path data model; `proto/dpsk_external.h` reserves the out-of-process
PoW/uinput/OCR boundary for platforms that need it.

## Prototype

`experiment_connectivity.py` and `capture_first3.py` are the original Python
prototypes used to validate API connectivity and the first three sessions.

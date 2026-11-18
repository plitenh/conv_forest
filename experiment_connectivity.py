#!/usr/bin/env python3
"""Connectivity and accuracy experiment for the conv_forest model."""

from __future__ import annotations

import hashlib
import json
import shutil
import sqlite3
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

BASE = "https://chat.deepseek.com"
PROFILE_ROOT = Path(
    "/home/kernel_dev/.var/app/org.mozilla.firefox/config/mozilla/firefox/bas7n4yc.default-release"
)
REPORT = Path("/home/kernel_dev/works/conv_forest/experiment_report.json")
MAX_SESSIONS = int(sys.argv[1]) if len(sys.argv) > 1 else 6


def load_token() -> str:
    db = PROFILE_ROOT / "storage/default/https+++chat.deepseek.com/ls/data.sqlite"
    con = sqlite3.connect(db)
    try:
        row = con.execute("select value from data where key='userToken'").fetchone()
        if not row:
            raise RuntimeError("userToken missing")
        return json.loads(row[0].decode("utf-8"))["value"]
    finally:
        con.close()


def load_cookies() -> str:
    src = PROFILE_ROOT / "cookies.sqlite"
    with tempfile.TemporaryDirectory() as td:
        dst = Path(td) / "cookies.sqlite"
        shutil.copy2(src, dst)
        for suffix in ("-wal", "-shm"):
            p = Path(str(src) + suffix)
            if p.exists():
                shutil.copy2(p, Path(str(dst) + suffix))
        con = sqlite3.connect(dst)
        try:
            rows = con.execute(
                "select name,value from moz_cookies where host like '%deepseek%'"
            ).fetchall()
        finally:
            con.close()
    return "; ".join(f"{name}={value}" for name, value in rows)


def api_get(path: str, params: dict) -> dict:
    url = BASE + path + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": "Bearer " + TOKEN,
            "User-Agent": "Mozilla/5.0",
            "Accept": "application/json, text/plain, */*",
            "Referer": BASE + "/",
            "Origin": BASE,
            "Cookie": COOKIE,
        },
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read().decode("utf-8", "replace"))


TOKEN = load_token()
COOKIE = load_cookies()


def fetch_session_list() -> list[dict]:
    sessions: list[dict] = []
    cursor = None
    for _ in range(20):
        params = {"lte_cursor.pinned": "false", "count": "100"}
        if cursor is not None:
            params["lte_cursor.updated_at"] = str(cursor)
        data = api_get("/api/v0/chat_session/fetch_page", params)
        biz = data["data"]["biz_data"]
        sessions.extend(biz["chat_sessions"])
        if not biz.get("has_more"):
            break
        cursor = biz["chat_sessions"][-1]["updated_at"]
    return sessions


def validate_tree(messages: list[dict]) -> dict:
    nodes = {int(m["message_id"]): m for m in messages}
    roots = [m for m in messages if m.get("parent_id") is None]

    missing_parents = []
    for m in messages:
        pid = m.get("parent_id")
        if pid is not None and int(pid) not in nodes:
            missing_parents.append(int(m["message_id"]))

    children: dict[int, list[int]] = {mid: [] for mid in nodes}
    for m in messages:
        pid = m.get("parent_id")
        if pid is not None:
            children[int(pid)].append(int(m["message_id"]))

    color = {mid: 0 for mid in nodes}
    cycle = False
    for root in roots:
        stack = [(int(root["message_id"]), 0)]
        while stack:
            mid, state = stack.pop()
            if state == 0:
                if color[mid] == 1:
                    cycle = True
                    continue
                if color[mid] == 2:
                    continue
                color[mid] = 1
                stack.append((mid, 1))
                for child in children.get(mid, []):
                    stack.append((child, 0))
            else:
                color[mid] = 2

    reachable = set()
    stack = [int(r["message_id"]) for r in roots]
    while stack:
        mid = stack.pop()
        if mid in reachable:
            continue
        reachable.add(mid)
        stack.extend(children.get(mid, []))

    leaves = [mid for mid, kids in children.items() if not kids]
    branch_points = [mid for mid, kids in children.items() if len(kids) > 1]
    return {
        "message_count": len(messages),
        "root_count": len(roots),
        "missing_parent_count": len(missing_parents),
        "cycle_detected": cycle,
        "reachable_count": len(reachable),
        "leaf_count": len(leaves),
        "branch_point_count": len(branch_points),
        "connectivity_ok": (
            not cycle
            and not missing_parents
            and len(reachable) == len(messages)
        ),
    }


def root_key_for(uri: str) -> str:
    digest = hashlib.sha256(uri.encode("utf-8")).hexdigest()
    return digest + ":" + uri


def main() -> int:
    all_sessions = fetch_session_list()
    selected = all_sessions[:MAX_SESSIONS]
    results = []
    for sess in selected:
        sid = sess["id"]
        uri = BASE + "/a/chat/s/" + sid
        data = api_get("/api/v0/chat/history_messages", {"chat_session_id": sid})
        biz = data["data"]["biz_data"]
        messages = biz.get("chat_messages", [])
        chat_session = biz.get("chat_session", {})
        tree = validate_tree(messages)
        results.append(
            {
                "session_id": sid,
                "root_key": root_key_for(uri),
                "uri": uri,
                "list_title": sess.get("title"),
                "history_title": chat_session.get("title"),
                "title_ok": sess.get("title") == chat_session.get("title"),
                "history_id_ok": chat_session.get("id") == sid,
                "tree": tree,
            }
        )

    overall = {
        "selected_sessions": len(results),
        "all_listed_sessions": len(all_sessions),
        "all_connectivity_ok": all(r["tree"]["connectivity_ok"] for r in results),
        "all_title_ok": all(r["title_ok"] for r in results),
        "all_history_id_ok": all(r["history_id_ok"] for r in results),
        "total_messages": sum(r["tree"]["message_count"] for r in results),
        "total_leaves": sum(r["tree"]["leaf_count"] for r in results),
        "total_branch_points": sum(r["tree"]["branch_point_count"] for r in results),
    }
    REPORT.write_text(json.dumps({"overall": overall, "sessions": results}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(overall, ensure_ascii=False, indent=2))
    print("report", REPORT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Capture complete conversation trees for the first three DeepSeek sessions."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import experiment_connectivity as ec

OUT = Path("/home/kernel_dev/works/conv_forest/captures")


def message_text(message: dict) -> str:
    parts = []
    for fragment in message.get("fragments", []):
        typ = fragment.get("type")
        if typ == "FILE":
            files = fragment.get("files") or []
            parts.append("[files: " + ", ".join(f.get("file_name", "") for f in files) + "]")
        else:
            content = fragment.get("content")
            if isinstance(content, str) and content:
                parts.append(content)
    return "\n\n".join(parts).strip()


def build_tree(messages: list[dict]) -> dict:
    nodes = {}
    children = {}
    for m in messages:
        mid = int(m["message_id"])
        pid = m.get("parent_id")
        pid = int(pid) if pid is not None else None
        nodes[mid] = {
            "message_id": mid,
            "parent_id": pid,
            "role": m.get("role"),
            "text_len": len(message_text(m)),
        }
        children.setdefault(pid, []).append(mid)

    roots = children.pop(None, [])
    leaves = [mid for mid in nodes if mid not in children]
    branch_points = [mid for mid, kids in children.items() if len(kids) > 1]

    paths = []
    for root in roots:
        stack = [(root, [root])]
        while stack:
            mid, path = stack.pop()
            kids = children.get(mid, [])
            if not kids:
                paths.append(path)
            else:
                for child in kids:
                    stack.append((child, path + [child]))

    return {
        "roots": roots,
        "nodes": nodes,
        "edges": [
            {"parent": mid, "child": child}
            for mid, kids in children.items()
            for child in kids
        ],
        "leaves": leaves,
        "branch_points": branch_points,
        "paths": paths,
        "message_count": len(messages),
        "leaf_count": len(leaves),
        "branch_point_count": len(branch_points),
        "path_count": len(paths),
    }


def main() -> int:
    sessions = ec.fetch_session_list()[:3]
    OUT.mkdir(parents=True, exist_ok=True)
    summary = []
    for sess in sessions:
        sid = sess["id"]
        uri = ec.BASE + "/a/chat/s/" + sid
        root_key = ec.root_key_for(uri)
        data = ec.api_get("/api/v0/chat/history_messages", {"chat_session_id": sid})
        biz = data["data"]["biz_data"]
        messages = biz.get("chat_messages", [])
        chat_session = biz.get("chat_session", {})
        tree = build_tree(messages)

        out_dir = OUT / root_key.split(":", 1)[0]
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "messages.jsonl").write_text(
            "\n".join(
                json.dumps(
                    {
                        "message_id": int(m["message_id"]),
                        "parent_id": m.get("parent_id"),
                        "role": m.get("role"),
                        "model": m.get("model"),
                        "thinking_enabled": m.get("thinking_enabled"),
                        "status": m.get("status"),
                        "inserted_at": m.get("inserted_at"),
                        "accumulated_token_usage": m.get("accumulated_token_usage"),
                        "search_enabled": m.get("search_enabled"),
                        "text": message_text(m),
                        "fragments": m.get("fragments", []),
                    },
                    ensure_ascii=False,
                )
                for m in messages
            ),
            encoding="utf-8",
        )
        (out_dir / "tree.json").write_text(
            json.dumps(
                {
                    "root_key": root_key,
                    "uri": uri,
                    "session_id": sid,
                    "title": chat_session.get("title"),
                    **tree,
                },
                ensure_ascii=False,
                indent=2,
            ),
            encoding="utf-8",
        )
        summary.append(
            {
                "session_id": sid,
                "title": chat_session.get("title"),
                "message_count": tree["message_count"],
                "root_count": len(tree["roots"]),
                "leaf_count": tree["leaf_count"],
                "branch_point_count": tree["branch_point_count"],
                "path_count": tree["path_count"],
                "root_key": root_key,
            }
        )
        print(json.dumps(summary[-1], ensure_ascii=False))

    (OUT / "capture_report.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Write files from the cloud mirror back to the user's local workspace over the
file bridge (POST /api/bundle, incremental: identical bytes are skipped).

Credentials come from the environment so nothing is baked into the script:
  ZAN_BRIDGE_BASE   bridge base URL
  ZAN_BRIDGE_TOKEN  bearer token

Usage:
  push_local.py <mirror-root> <relative-path> [<relative-path> ...] [--summary TEXT]

Paths are workspace-relative and keep their names 1:1 (mirror <-> local).
"""
import base64, json, os, sys, urllib.request

BASE = os.environ["ZAN_BRIDGE_BASE"].rstrip("/")
TOKEN = os.environ["ZAN_BRIDGE_TOKEN"]
UA = "Mozilla/5.0 (bridge-client)"


def post(path, payload):
    req = urllib.request.Request(
        BASE + path,
        data=json.dumps(payload).encode(),
        headers={"Authorization": "Bearer " + TOKEN,
                 "User-Agent": UA,
                 "Content-Type": "application/json"},
        method="POST")
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.load(r)


def main(argv):
    summary = None
    if "--summary" in argv:
        i = argv.index("--summary")
        summary = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    root, rels = argv[0], argv[1:]
    files = []
    for rel in rels:
        with open(os.path.join(root, rel), "rb") as f:
            files.append({"path": rel.replace("/", "\\"),
                          "content": base64.b64encode(f.read()).decode(),
                          "encoding": "base64"})
    payload = {"files": files}
    if summary:
        payload["handoff"] = {"summary": summary}
    print(json.dumps(post("/api/bundle", payload), ensure_ascii=False)[:2000])


if __name__ == "__main__":
    main(sys.argv[1:])

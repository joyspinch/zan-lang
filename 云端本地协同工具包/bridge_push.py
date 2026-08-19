#!/usr/bin/env python3
"""Write files from the cloud mirror back to the user's local workspace over the
file bridge (POST /api/bundle, incremental: identical bytes are skipped).

Credentials come from the environment so nothing is baked into the script:
  ZAN_BRIDGE_BASE   bridge base URL
  ZAN_BRIDGE_TOKEN  bearer token

Usage:
  push_local.py <mirror-root> <relative-path> [<relative-path> ...] [--summary TEXT]

Paths are workspace-relative and keep their names 1:1 (mirror <-> local).

Content always travels as base64 of the exact bytes: the bridge decodes a
bundle entry as base64 (a plain UTF-8 string is decoded too, silently
producing garbage and truncating the file), and base64 also keeps CRLF
intact. Every write is read back and compared byte for byte before the
script reports success.
"""
import base64, json, os, sys, urllib.parse, urllib.request

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


def get_bytes(rel):
    q = urllib.parse.urlencode({"path": rel, "encoding": "base64"})
    req = urllib.request.Request(
        BASE + "/api/file?" + q,
        headers={"Authorization": "Bearer " + TOKEN, "User-Agent": UA})
    with urllib.request.urlopen(req, timeout=300) as r:
        return base64.b64decode(json.load(r).get("content", ""))


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
    bad = []
    for rel in rels:
        with open(os.path.join(root, rel), "rb") as f:
            want = f.read()
        if get_bytes(rel.replace("\\", "/")) != want:
            bad.append(rel)
    if bad:
        print("WRITE_BACK_MISMATCH " + " ".join(bad))
        return 1
    print("WRITE_BACK_VERIFIED " + str(len(rels)) + " file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) or 0)

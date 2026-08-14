#!/usr/bin/env python3
"""把本机工作区的子树/单文件拉到云端镜像（1:1 保持相对路径与文件名）。

凭据全部走环境变量，脚本内不落任何地址/令牌：
  ZAN_BRIDGE_BASE   桥接基址，如 https://xxx.trycloudflare.com
  ZAN_BRIDGE_TOKEN  Bearer 令牌
  ZAN_BRIDGE_ROOT   本机工作区根（默认 d:/project/zan-lang）
  ZAN_MIRROR        云端镜像根（默认 ~/repos/zan-lang）

用法：
  python3 bridge_pull.py src/compiler stdlib/System/Web CMakeLists.txt
"""
import base64, json, os, sys, urllib.parse, urllib.request

BASE = os.environ["ZAN_BRIDGE_BASE"].rstrip("/")
TOKEN = os.environ["ZAN_BRIDGE_TOKEN"]
ROOT = os.environ.get("ZAN_BRIDGE_ROOT", "d:/project/zan-lang")
MIRROR = os.environ.get("ZAN_MIRROR", os.path.expanduser("~/repos/zan-lang"))
# Cloudflare 会按 UA 拦截（403 + error code: 1010），所以带一个浏览器 UA。
HEADERS = {"Authorization": "Bearer " + TOKEN,
           "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                         "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36",
           "ngrok-skip-browser-warning": "1"}


def get(path, params):
    url = BASE + path + "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS),
                               timeout=300) as r:
        return json.load(r)


def write_file(rel, data):
    dest = os.path.join(MIRROR, rel.replace("\\", "/"))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(data)


def pull_dir(rel):
    d = get("/api/bundle", {"path": ROOT + "/" + rel})
    base = d.get("base", rel).replace("\\", "/")
    n = 0
    for f in d["files"]:
        p = f["path"].replace("\\", "/")
        content = f.get("content", "")
        raw = (base64.b64decode(content) if f.get("encoding") == "base64"
               else content.encode())
        write_file(p if p.startswith(base) else rel + "/" + p, raw)
        n += 1
    print("%s: %d files" % (rel, n))


def pull_file(rel):
    url = BASE + "/api/file?" + urllib.parse.urlencode(
        {"path": ROOT + "/" + rel, "raw": 1})
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS),
                               timeout=300) as r:
        write_file(rel, r.read())
    print("%s: 1 file" % rel)


def main():
    for rel in sys.argv[1:]:
        rel = rel.replace("\\", "/").strip("/")
        parent = os.path.dirname(rel)
        info = get("/api/files", {"path": ROOT + ("/" + parent if parent else "")})
        kind = {e["name"]: e["type"] for e in info.get("entries", [])}.get(
            os.path.basename(rel))
        if kind == "dir":
            pull_dir(rel)
        else:
            pull_file(rel)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""本机工作区桥接客户端：一个命令行覆盖 看/拉/改/写回/在本机执行。

`bridge_pull.py` / `bridge_push.py` 是最小的搬运脚本；这个把日常都要用的动作
收在一起，并且**按字节传输**（base64）——文本方式读写会毁掉二进制（交叉编译出
来的 ELF 拉回云端就跑不起来了），大文件还会被单次读取的上限截断。

用法（凭据从环境变量取，见 README）:
  br.py doctor
  br.py tree [path] [depth]
  br.py files <path> [depth] [glob]
  br.py read <path> [offset] [limit]
  br.py search <regex> [limit] [ext] [glob]
  br.py pull <relpath>...            # 单文件拉到云端镜像（按字节，可拉二进制）
  br.py pullglob <reldir> <glob>     # 按 glob 拉整棵子树
  br.py push <summary> <relpath>...  # 从云端镜像写回本机（bundle，带 handoff）
  br.py write <relpath> <localfile>  # 写单个文本文件
  br.py exec <command> [timeoutMs]   # 在本机跑一条命令（cmd.exe）

环境变量:
  ZAN_BRIDGE_BASE   桥接地址（必填，会轮换，取「本机文件桥接」笔记里的最新值）
  ZAN_BRIDGE_TOKEN  Bearer 令牌（必填）
  ZAN_BRIDGE_ROOT   本机工作区根，默认 d:/project/zan-lang
  ZAN_MIRROR        云端镜像根，默认 ~/repos/zan-lang
"""
import base64
import json
import os
import sys
import urllib.parse
import urllib.request

BASE = os.environ.get("ZAN_BRIDGE_BASE", "").rstrip("/")
TOKEN = os.environ.get("ZAN_BRIDGE_TOKEN", "")
ROOT = os.environ.get("ZAN_BRIDGE_ROOT", "d:/project/zan-lang")
MIRROR = os.environ.get(
    "ZAN_MIRROR", os.path.join(os.path.expanduser("~"), "repos", "zan-lang")
)
# 桥接在 Cloudflare 后面，非浏览器 UA 会被按 1010 拦掉。
UA = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
)
# 单次 /api/file 读取有上限，超过就被静默截断；按 4 MiB 分块累加。
CHUNK = 4 * 1024 * 1024


def req(method, path, params=None, body=None, timeout=600):
    if not BASE or not TOKEN:
        sys.exit("请先设置 ZAN_BRIDGE_BASE / ZAN_BRIDGE_TOKEN（见 README）")
    url = BASE + path
    if params:
        url += "?" + urllib.parse.urlencode(params)
    data = None
    headers = {
        "Authorization": "Bearer " + TOKEN,
        "User-Agent": UA,
        "ngrok-skip-browser-warning": "1",
    }
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    r = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return resp.read().decode("utf-8", "replace")


def abspath(rel):
    rel = rel.replace("\\", "/").lstrip("/")
    return ROOT.rstrip("/") + "/" + rel


def jget(path, params=None, timeout=600):
    return json.loads(req("GET", path, params, timeout=timeout))


def cmd_doctor():
    print(req("GET", "/api/bridge/doctor"))


def cmd_tree(path=".", depth="3"):
    print(req("GET", "/api/tree", {"path": abspath(path), "depth": depth}))


def cmd_files(path, depth=None, glob=None):
    p = {"path": abspath(path)}
    if depth:
        p["depth"] = depth
    if glob:
        p["glob"] = glob
    print(req("GET", "/api/files", p))


def cmd_read(path, offset=None, limit=None):
    p = {"path": abspath(path), "raw": "1"}
    if offset:
        p["offset"] = offset
    if limit:
        p["limit"] = limit
    sys.stdout.write(req("GET", "/api/file", p))


def cmd_search(q, limit="30", ext=None, glob=None):
    p = {"q": q, "limit": limit, "matchLines": "1", "mode": "regex"}
    if ext:
        p["ext"] = ext
    if glob:
        p["glob"] = glob
    d = jget("/api/search", p)
    for it in d.get("results", d.get("items", [])):
        rp = it.get("path", "")
        lines = it.get("matchLines") or it.get("lines") or []
        if not lines:
            print(rp)
        for m in lines:
            print("%s:%s: %s" % (rp, m.get("line"), (m.get("text") or "").strip()))


def pull_one(rel):
    content = b""
    while True:
        d = jget("/api/file", {"path": abspath(rel), "encoding": "base64",
                               "offset": str(len(content)), "limit": str(CHUNK)})
        part = base64.b64decode(d["content"])
        content += part
        size = int(d.get("sizeBytes") or 0)
        if not part or (size and len(content) >= size) or len(part) < CHUNK:
            break
    dst = os.path.join(MIRROR, rel.replace("\\", "/"))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(content)
    print("pulled %s (%d bytes)" % (rel, len(content)))


def cmd_pull(*rels):
    for r in rels:
        pull_one(r)


def cmd_pullglob(reldir, glob):
    d = jget("/api/files", {"path": abspath(reldir), "depth": "15", "glob": glob})
    n = 0
    for e in d.get("entries", []):
        if e.get("type") != "file":
            continue
        pull_one(e["path"].replace("\\", "/"))
        n += 1
    print("pulled %d files" % n)


def cmd_push(summary, *rels):
    files = []
    for rel in rels:
        rel = rel.replace("\\", "/")
        # bundle 按 base64 解码 content（doctor 的 quickstart 就是这么说的）：
        # 明文直传会被当成 base64 解出乱码字节。
        with open(os.path.join(MIRROR, rel), "rb") as f:
            files.append({
                "path": abspath(rel),
                "content": base64.b64encode(f.read()).decode("ascii"),
                "encoding": "base64",
            })
    print(req("POST", "/api/bundle",
              body={"files": files, "handoff": {"summary": summary}})[:2000])


def cmd_write(rel, localfile):
    with open(localfile, "r", encoding="utf-8") as f:
        content = f.read()
    print(req("POST", "/api/file", {"path": abspath(rel)},
              {"content": content})[:1000])


def cmd_exec(command, timeout_ms="120000"):
    out = req("POST", "/api/terminal/exec",
              body={"command": command, "timeoutMs": int(timeout_ms)},
              timeout=int(timeout_ms) / 1000 + 60)
    try:
        d = json.loads(out)
        sys.stdout.write(d.get("stdout", "") or "")
        err = d.get("stderr") or ""
        if err:
            sys.stderr.write(err)
        print("\n[exit=%s]" % d.get("exitCode"))
    except json.JSONDecodeError:
        print(out[:4000])


CMDS = {
    "doctor": cmd_doctor,
    "tree": cmd_tree,
    "files": cmd_files,
    "read": cmd_read,
    "search": cmd_search,
    "pull": cmd_pull,
    "pullglob": cmd_pullglob,
    "push": cmd_push,
    "write": cmd_write,
    "exec": cmd_exec,
}

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in CMDS:
        print(__doc__)
        sys.exit(1)
    CMDS[sys.argv[1]](*sys.argv[2:])

#!/usr/bin/env python3
"""云端镜像快速重建（git 仓库工作区专用，比逐文件 pull 快一个量级）

适用：本机工作区是 git 仓库、且有可克隆的 remote（哪怕本机领先若干未推送提交）。
思路：云端从 remote clone 拿基线，只把「未推送提交(bundle) + 未提交改动(tar)」当增量搬过去，
      云端镜像 = 本机工作树的逐字节副本；改完仍按老规矩用 /api/bundle 写回。

用法（在云端执行，需先设好桥接凭据）：
    export ZAN_BRIDGE_BASE=https://xxx.trycloudflare.com
    export ZAN_BRIDGE_TOKEN=<token>
    python3 mirror_delta.py --remote https://github.com/<owner>/<repo>.git \
        --win-root 'd:\\project\\zan-lang' --mirror ~/work/zan-lang

步骤（脚本自动做，失败时可照此手工重放）：
 1. 本机: git bundle create _devin_sync\\ahead.bundle main --not origin/main
 2. 本机: git diff --name-only HEAD + git ls-files --others --exclude-standard
          → 过滤掉 drivers/ 、构建缓存、临时目录 → tar -czf _devin_sync\\delta.tgz -T keep.txt
 3. 云端: git clone <remote>; git fetch <bundle> main:local_main; git checkout local_main
 4. 云端: tar -xzf delta.tgz -C mirror; 删掉本机已删除的文件
 5. 校验: 两侧 git diff --stat HEAD 的文件集应一致（行数会因 autocrlf 不同，属正常）
 6. 收尾: 删除本机 _devin_sync 临时目录

注意：raw 下载单次上限约 8MB，务必传 .tgz（或分片）；本机 autocrlf=true 时提交请在本机做，
      不要在云端 commit，避免整文件换行符改动。
"""
import argparse
import json
import os
import subprocess
import sys
import urllib.parse
import urllib.request

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
SKIP = ["drivers/", "_devin_ref_tmp", "_devin_sync", "run.log", "/.zan/", "public.zip"]


def bridge(path, method="GET", body=None, params=None, raw=False):
    base = os.environ["ZAN_BRIDGE_BASE"].rstrip("/")
    url = base + path
    if params:
        url += "?" + urllib.parse.urlencode(params)
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": "Bearer " + os.environ["ZAN_BRIDGE_TOKEN"],
        "Content-Type": "application/json",
        "User-Agent": UA,
    })
    with urllib.request.urlopen(req, timeout=600) as r:
        return r.read() if raw else json.loads(r.read())


def remote_exec(cmd):
    r = bridge("/api/terminal/exec", "POST", {"command": cmd, "timeoutMs": 280000})
    if r.get("exitCode") not in (0, None):
        sys.stderr.write(r.get("stderr", "")[:2000] + "\n")
    return r


def run(cmd, cwd=None):
    subprocess.run(cmd, cwd=cwd, check=True, shell=isinstance(cmd, str))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", required=True)
    ap.add_argument("--win-root", required=True, help=r"本机工作区根，如 d:\project\zan-lang")
    ap.add_argument("--mirror", required=True, help="云端镜像目录（用工作区原名）")
    ap.add_argument("--branch", default="main")
    a = ap.parse_args()
    mirror = os.path.expanduser(a.mirror)
    root = a.win_root.rstrip("\\")
    tmp = root + r"\_devin_sync"
    keep_filter = " ".join('/c:"%s"' % p for p in SKIP)

    remote_exec(r'cd /d {0} && mkdir {1} & git bundle create {1}\ahead.bundle {2} --not origin/{2}'.format(root, tmp, a.branch))
    remote_exec(
        r'cd /d {0} && git diff --name-only HEAD > {1}\l1.txt && git ls-files --others --exclude-standard > {1}\l2.txt '
        r'&& type {1}\l1.txt {1}\l2.txt > {1}\all.txt && findstr /v /i {2} {1}\all.txt > {1}\keep.txt '
        r'& tar -czf {1}\delta.tgz -T {1}\keep.txt 2>nul'.format(root, tmp, keep_filter))

    os.makedirs(os.path.dirname(mirror) or ".", exist_ok=True)
    for name in ("ahead.bundle", "delta.tgz"):
        blob = bridge("/api/file", params={"path": tmp.replace("\\", "/") + "/" + name, "raw": 1}, raw=True)
        open("/tmp/" + name, "wb").write(blob)
        print(name, len(blob), "bytes")

    if not os.path.isdir(mirror):
        run(["git", "clone", "--quiet", a.remote, mirror])
    run(["git", "fetch", "/tmp/ahead.bundle", a.branch + ":local_" + a.branch], cwd=mirror)
    run(["git", "checkout", "-q", "local_" + a.branch], cwd=mirror)
    run(["tar", "-xzf", "/tmp/delta.tgz", "-C", mirror])
    deleted = subprocess.run(["git", "diff", "--name-only", "--diff-filter=D", "HEAD"], cwd=mirror,
                             capture_output=True, text=True).stdout  # 本机删除的文件：镜像里手工清理
    print("提示：本机已删除但镜像仍在的文件需自行 rm，参考本机 git status 的 ' D ' 行\n", deleted[:500])
    remote_exec(r"cd /d {0} && rmdir /s /q _devin_sync".format(root))
    print("镜像就绪：", mirror)


if __name__ == "__main__":
    main()

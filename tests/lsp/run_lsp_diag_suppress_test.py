# -*- coding: utf-8 -*-
"""B-ID200 余量①回归：跨文件类型 E1005 误报抑制（C 宿主 zan-lsp）。

单文档语义视图下，binder 对工作区其他文件声明的类型打
"[E1005] undefined type"；LSP 发布前按项目索引（工作区 + stdlib +
包闭包）抑制"索引按名命中类型 + 命名空间被本文件 using 导入（或同
命名空间 / 全局命名空间）"的误报。四象限：

  lib.zan    声明 namespace App 的 Helper（被引用方，自身应干净）
  main.zan   using App + Helper       → 0 条（抑制生效）
  nomain.zan 无 using + Helper         → E1005 保留（命名空间门）
  typo.zan   using App + Helpr         → E1005 保留（真拼写错误不吞）

用法：python run_lsp_diag_suppress_test.py --exe <zan-lsp 路径>
"""
import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

LIB = """namespace App;

class Helper {
    static string Tag() { return "ok"; }
    int value;
    int GetValue() { return value; }
}
"""

MAIN = """using App;

class Prog {
    static void Main() {
        Helper h = new Helper();
        string s = Helper.Tag();
    }
}
"""

NOMAIN = """class Prog2 {
    static void Main() {
        Helper h = new Helper();
    }
}
"""

TYPO = """using App;

class Prog3 {
    static void Main() {
        Helpr h = new Helpr();
    }
}
"""

# 文件 → (期望保留的 "undefined type 'X'" 类型名列表；空 = 应干净)
EXPECT = [
    ("lib.zan", LIB, []),
    ("main.zan", MAIN, []),
    ("nomain.zan", NOMAIN, ["Helper"]),
    ("typo.zan", TYPO, ["Helpr"]),
]


def frame(message):
    body = json.dumps(message).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def read_msg(stdout):
    header = b""
    while b"\r\n\r\n" not in header:
        c = stdout.read(1)
        if not c:
            return None
        header += c
    n = int(header.decode("ascii").split("Content-Length:")[1].strip().split("\r\n")[0])
    return json.loads(stdout.read(n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--timeout", type=int, default=120,
                    help="总时限；首建索引在 reader 线程后台进行，不阻塞发布")
    args = ap.parse_args()

    ws = Path(tempfile.mkdtemp(prefix="lsp_diag_suppress_"))
    for name, text, _ in EXPECT:
        (ws / name).write_text(text, encoding="utf-8")

    proc = subprocess.Popen([args.exe], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    failures = []
    try:
        def send(obj):
            proc.stdin.write(frame(obj))
            proc.stdin.flush()

        send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
              "params": {"processId": None, "rootUri": ws.as_uri(),
                         "capabilities": {}}})

        # 先收 initialize 响应，再发 didOpen（会话握手次序）。
        deadline = time.time() + 20
        init_done = False
        while time.time() < deadline and not init_done:
            m = read_msg(proc.stdout)
            if m is None:
                break
            if m.get("id") == 1:
                init_done = True
        if not init_done:
            print("FAIL initialize 未在 20s 内应答", file=sys.stderr)
            return 1

        send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
        uri_of = {}
        for name, text, _ in EXPECT:
            uri = (ws / name).as_uri()
            uri_of[uri] = name
            send({"jsonrpc": "2.0", "method": "textDocument/didOpen",
                  "params": {"textDocument": {"uri": uri, "languageId": "zan",
                                              "version": 1, "text": text}}})

        first_pub = {}  # name → 首次 publishDiagnostics 的 diagnostics
        deadline = time.time() + args.timeout
        while len(first_pub) < len(EXPECT) and time.time() < deadline:
            m = read_msg(proc.stdout)
            if m is None:
                break
            if m.get("method") != "textDocument/publishDiagnostics":
                continue
            name = uri_of.get(m["params"].get("uri"))
            if name is not None and name not in first_pub:
                first_pub[name] = m["params"]["diagnostics"]

        for name, _, wanted in EXPECT:
            if name not in first_pub:
                failures.append("%s: 未收到 publishDiagnostics" % name)
                continue
            msgs = [d.get("message", "") for d in first_pub[name]]
            if not wanted:
                errs = [m for m in msgs if "E1005" in m]
                if errs:
                    failures.append("%s: 应干净仍有误报 %s" % (name, errs[:3]))
            else:
                for t in wanted:
                    if not any(("undefined type '%s'" % t) in m for m in msgs):
                        failures.append("%s: 应保留 undefined type '%s'，实际 %s"
                                        % (name, t, msgs[:3]))

        if failures:
            for f in failures:
                print("FAIL " + f, file=sys.stderr)
            return 1
        print("lsp_diag_suppress: 4/4 象限符合预期（抑制/命名空间门/真错保留）")
        return 0
    finally:
        proc.kill()


if __name__ == "__main__":
    sys.exit(main())

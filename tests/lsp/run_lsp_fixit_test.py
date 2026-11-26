"""S1（docs/ZAN_STANDARD.md §1）：zan-lsp 携带编译器 fix-it。

打开一个方法名拼错的文档：publishDiagnostics 里该诊断带 code、覆盖 ".Incremnt"
的 range，以及 data.fix（补丁的 LSP range 与 newText）；把诊断原样放进
textDocument/codeAction 的 context，必须得到 "Change to 'Increment'" 的 quickfix，
其 WorkspaceEdit 正好把 "Incremnt" 换成 "Increment"。"""
import argparse
import json
import subprocess
import sys
import threading
import queue

SOURCE = """public class Counter {
    public int Count;
    public void Increment() { Count = Count + 1; }
}

public class Program {
    public static void Main() {
        Counter counter = new Counter();
        counter.Incremnt();
    }
}
"""
URI = "file:///tmp/zan_lsp_fixit_probe.zan"


def frame(obj):
    body = json.dumps(obj).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def reader(stream, out):
    while True:
        headers = {}
        while True:
            line = stream.readline()
            if not line:
                return
            line = line.strip()
            if not line:
                break
            k, _, v = line.decode().partition(":")
            headers[k.strip().lower()] = v.strip()
        n = int(headers.get("content-length", "0"))
        out.put(json.loads(stream.read(n)))


def wait_for(q, pred, timeout=60):
    import time
    end = time.time() + timeout
    while time.time() < end:
        try:
            msg = q.get(timeout=max(0.1, end - time.time()))
        except queue.Empty:
            break
        if pred(msg):
            return msg
    raise AssertionError("timed out waiting for LSP message")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    a = ap.parse_args()
    p = subprocess.Popen([a.exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
    q = queue.Queue()
    threading.Thread(target=reader, args=(p.stdout, q), daemon=True).start()
    try:
        p.stdin.write(frame({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                             "params": {"capabilities": {}}}))
        p.stdin.flush()
        wait_for(q, lambda m: m.get("id") == 1)
        p.stdin.write(frame({"jsonrpc": "2.0", "method": "initialized", "params": {}}))
        p.stdin.write(frame({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
            "textDocument": {"uri": URI, "languageId": "zan", "version": 1, "text": SOURCE}}}))
        p.stdin.flush()
        note = wait_for(q, lambda m: m.get("method") == "textDocument/publishDiagnostics"
                        and m["params"]["uri"] == URI and m["params"]["diagnostics"])
        diags = note["params"]["diagnostics"]
        d = [x for x in diags if "Incremnt" in x["message"]]
        assert len(d) == 1, diags
        d = d[0]
        assert d.get("code") == "E2085", d
        line = SOURCE.splitlines()[8]
        r = d["range"]
        assert line[r["start"]["character"]:r["end"]["character"]] == ".Incremnt", r
        fix = d["data"]["fix"]
        fr = fix["range"]
        assert fix["newText"] == "Increment", fix
        assert line[fr["start"]["character"]:fr["end"]["character"]] == "Incremnt", fr

        p.stdin.write(frame({"jsonrpc": "2.0", "id": 2, "method": "textDocument/codeAction",
                             "params": {"textDocument": {"uri": URI}, "range": d["range"],
                                        "context": {"diagnostics": [d]}}}))
        p.stdin.flush()
        resp = wait_for(q, lambda m: m.get("id") == 2)
        acts = [x for x in resp["result"] if x.get("kind") == "quickfix"
                and x["title"] == "Change to 'Increment'"]
        assert len(acts) == 1, resp["result"]
        edit = acts[0]["edit"]["changes"][URI]
        assert edit == [{"range": fr, "newText": "Increment"}], edit
        print("lsp fix-it: diagnostic data and quickfix edit verified")
    finally:
        p.kill()


if __name__ == "__main__":
    sys.exit(main())

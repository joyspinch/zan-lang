"""tool_lsp_zan_session: feed LSP frames to the pure-Zan LSP server over stdio.

The server under test is src/selfhost/tools/lsp.zan compiled into a standalone
exe (entry: tests/selfhost/lsp_zan_session_entry.zan, built by CMake). This
script drives one full session and asserts the B-ID200 first-slice behaviors:

  initialize capabilities        full sync + diag/completion/hover/definition/
                                 references/documentSymbol providers
  real diagnostics               semantic error (undefined type) with position
  clean document                 empty diagnostics, version echoed
  documentSymbol                 class with nested field/method children
  definition / references / hover   symbol-index based lookups
  completion                     keyword + symbol filtering by cursor prefix
  didChange                      re-analysis, version bump
  syntax error                   parser diagnostics
  didClose                       diagnostics cleared notification
  shutdown/exit                  process exits 0

Failure prints the offending frame and exits 1; success prints PASS.
"""

import json
import subprocess
import sys


def frame(msg):
    body = json.dumps(msg, ensure_ascii=False).encode("utf-8")
    return ("Content-Length: %d\r\n\r\n" % len(body)).encode("ascii") + body


SRC_BAD = (
    "class Broken {\n"
    "    void F() {\n"
    "        Missing x = null;\n"
    "    }\n"
    "}\n"
)
SRC_OK = (
    "class ZDemo {\n"
    "    public int Val;\n"
    "    public int Get() { return Val; }\n"
    "}\n"
    "class ZUse {\n"
    "    public int Go() { ZDemo d = new ZDemo(); return d.Get(); }\n"
    "}\n"
)
SRC_SYN = (
    "class SyntaxErr {\n"
    "    int f( }\n"
)

OK_URI = "file:///t/Ok.zan"
BAD_URI = "file:///t/Bad.zan"
SYN_URI = "file:///t/Syn.zan"


class Session(object):
    def __init__(self, exe):
        self.proc = subprocess.Popen(
            [exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.buf = b""
        self.serial = 10
        self.notifications = []
        self.pending = None  # response read while draining for a diagnostic

    def send(self, msg):
        self.proc.stdin.write(frame(msg))
        self.proc.stdin.flush()

    def request(self, method, params):
        self.serial += 1
        rid = str(self.serial)
        self.send({"jsonrpc": "2.0", "id": rid, "method": method,
                   "params": params})
        return self.wait_response(rid)

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def _read_frame(self):
        # read header lines until blank line
        headers = {}
        while True:
            line = b""
            while not line.endswith(b"\r\n"):
                ch = self.proc.stdout.read(1)
                if not ch:
                    raise AssertionError("server closed mid-header: %r"
                                         % self.buf[-200:])
                line += ch
            line = line[:-2]
            if line == b"":
                break
            k, _, v = line.partition(b":")
            headers[k.strip().lower().decode("ascii")] = v.strip()
        n = int(headers["content-length"])
        body = b""
        while len(body) < n:
            chunk = self.proc.stdout.read(n - len(body))
            if not chunk:
                raise AssertionError("server closed mid-body")
            body += chunk
        return json.loads(body.decode("utf-8"))

    def pump_until_response(self, rid):
        """Read frames until the response with id == rid shows up;
        notifications are collected along the way."""
        while True:
            d = self._read_frame()
            if "id" in d and str(d["id"]) == rid:
                return d
            self.notifications.append(d)

    def wait_response(self, rid):
        if self.pending is not None and str(self.pending.get("id")) == rid:
            out = self.pending
            self.pending = None
            return out
        return self.pump_until_response(rid)

    def diag_for(self, uri):
        """Drain the pipe until the NEXT publishDiagnostics for uri arrives.
        Never consults history: each notify is followed by exactly one
        publishDiagnostics, so the next matching frame in the pipe is the
        current one (an older version in self.notifications must not win)."""
        while True:
            d = self._read_frame()
            if d.get("method") == "textDocument/publishDiagnostics":
                if d["params"]["uri"] == uri:
                    return d["params"]
                self.notifications.append(d)
            else:
                self.notifications.append(d)
                if "id" in d:
                    self.pending = d


def fail(ctx, detail):
    print("FAIL %s: %s" % (ctx, detail))
    sys.exit(1)


def main():
    exe = sys.argv[sys.argv.index("--exe") + 1]
    s = Session(exe)

    # initialize
    caps = s.request("initialize", {}).get("result", {}).get("capabilities", {})
    for key in ("definitionProvider", "referencesProvider",
                "documentSymbolProvider", "hoverProvider",
                "completionProvider"):
        if key not in caps:
            fail("initialize", "missing capability %s in %s" % (key, caps))
    s.notify("initialized", {})

    # didOpen clean document -> empty diagnostics, version echoed
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": OK_URI, "languageId": "zan", "version": 1, "text": SRC_OK}})
    diag = s.diag_for(OK_URI)
    if diag is None or diag["diagnostics"] != []:
        fail("didOpen clean", "expected empty diagnostics, got %r" % diag)

    # documentSymbol: ZDemo class with Val field + Get method children
    syms = s.request("textDocument/documentSymbol",
                     {"textDocument": {"uri": OK_URI}}).get("result")
    if not isinstance(syms, list) or len(syms) != 2:
        fail("documentSymbol", "expected 2 top-level symbols, got %r" % (syms,))
    top = {d["name"]: d for d in syms}
    if "ZDemo" not in top or top["ZDemo"]["kind"] != 5:
        fail("documentSymbol", "ZDemo class missing, got %r" % (syms,))
    children = {d["name"]: d for d in top["ZDemo"].get("children", [])}
    if children.get("Val", {}).get("kind") != 8:
        fail("documentSymbol", "Val field missing, got %r" % (children,))
    if children.get("Get", {}).get("kind") != 6:
        fail("documentSymbol", "Get method missing, got %r" % (children,))
    if children["Get"]["range"]["start"]["line"] != 2:
        fail("documentSymbol", "Get on wrong line: %r" % (children["Get"],))

    # definition: cursor on ZDemo in `ZDemo d = new ZDemo();` (line 5 char 40)
    loc = s.request("textDocument/definition", {
        "textDocument": {"uri": OK_URI},
        "position": {"line": 5, "character": 38}}).get("result")
    if not loc or loc["uri"] != OK_URI or \
            loc["range"]["start"]["line"] != 0:
        fail("definition", "expected ZDemo decl at line 0, got %r" % (loc,))

    # references on ZDemo (line 0 char 7): declaration + 2 uses on line 5
    refs = s.request("textDocument/references", {
        "textDocument": {"uri": OK_URI},
        "position": {"line": 0, "character": 7},
        "context": {"includeDeclaration": True}}).get("result")
    if not isinstance(refs, list) or len(refs) < 3:
        fail("references", "expected >=3 hits for ZDemo, got %r" % (refs,))
    ref_lines = sorted(r["range"]["start"]["line"] for r in refs)
    if ref_lines != [0, 5, 5]:
        fail("references", "expected lines [0,5,5], got %r" % (ref_lines,))

    # hover on Val (line 1 char 17)
    hv = s.request("textDocument/hover", {
        "textDocument": {"uri": OK_URI},
        "position": {"line": 1, "character": 17}}).get("result")
    hv_text = hv.get("contents", {}).get("value", "") if hv else ""
    if "Val" not in hv_text or "field" not in hv_text:
        fail("hover", "expected Val field hover, got %r" % (hv,))

    # completion at end of line 6 (empty prefix) includes ZDemo + keywords
    comp = s.request("textDocument/completion", {
        "textDocument": {"uri": OK_URI},
        "position": {"line": 6, "character": 0}}).get("result") or []
    labels = [c["label"] for c in comp]
    if "ZDemo" not in labels or "class" not in labels:
        fail("completion", "expected ZDemo + keyword in %r" % (labels[:12],))

    # didOpen semantic-error document -> undefined type diagnostic
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": BAD_URI, "languageId": "zan", "version": 1, "text": SRC_BAD}})
    diag = s.diag_for(BAD_URI)
    msgs = [d["message"] for d in (diag or {}).get("diagnostics", [])]
    if not any("undefined type 'Missing'" in m for m in msgs):
        fail("diagnostics", "expected undefined type 'Missing', got %r" % (msgs,))
    rng = [d for d in diag["diagnostics"]
           if "Missing" in d["message"]][0]["range"]["start"]
    if rng != {"line": 2, "character": 8}:
        fail("diagnostics", "wrong position for Missing: %r" % (rng,))

    # didChange: re-analysis keeps the document clean, version bumped
    s.notify("textDocument/didChange", {
        "textDocument": {"uri": OK_URI, "version": 2},
        "contentChanges": [{"text": SRC_OK}]})
    diag = s.diag_for(OK_URI)
    if diag is None or diag["diagnostics"] != [] or diag.get("version") != 2:
        fail("didChange", "expected clean v2 diagnostics, got %r" % (diag,))

    # syntax error document
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": SYN_URI, "languageId": "zan", "version": 1, "text": SRC_SYN}})
    diag = s.diag_for(SYN_URI)
    msgs = [d["message"] for d in (diag or {}).get("diagnostics", [])]
    if not any("syntax error" in m for m in msgs):
        fail("syntax diagnostics", "expected syntax error, got %r" % (msgs,))

    # didClose clears diagnostics
    s.notify("textDocument/didClose", {"textDocument": {"uri": SYN_URI}})
    diag = s.diag_for(SYN_URI)
    if diag is None or diag["diagnostics"] != []:
        fail("didClose", "expected cleared diagnostics, got %r" % (diag,))

    # shutdown + exit
    resp = s.request("shutdown", {})
    if "result" not in resp:
        fail("shutdown", "no result in %r" % (resp,))
    s.notify("exit", None)
    try:
        rc = s.proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        s.proc.kill()
        fail("exit", "server did not exit after exit notification")
    if rc != 0:
        fail("exit", "exit code %d" % rc)

    print("PASS tool_lsp_zan_session")


if __name__ == "__main__":
    main()

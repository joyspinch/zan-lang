"""tool_lsp_zan_session: feed LSP frames to the pure-Zan LSP server over stdio.

The server under test is src/selfhost/tools/lsp.zan compiled into a standalone
exe (entry: tests/selfhost/lsp_zan_session_entry.zan, built by CMake). This
script drives one full session and asserts the B-ID200 first-slice behaviors:

  initialize capabilities        full sync + diag/completion/hover/definition/
                                 references/documentSymbol/rename providers
  real diagnostics               semantic error (undefined type) with position
  clean document                 empty diagnostics, version echoed
  documentSymbol                 class with nested field/method children
  definition / references / hover   symbol-index based lookups
  completion                     keyword + symbol filtering by cursor prefix
  prepareRename                  word range + placeholder; null on whitespace
  rename                         WorkspaceEdit across hits (incl. CJK line,
                                 UTF-16 columns); null on invalid newName;
                                 error -32602 on keyword newName
  documentHighlight              word-boundary hits in current doc, kind=1
  foldingRange                   brace regions via string/comment state
                                 machine; same-line braces skipped
  didChange                      re-analysis, version bump
  syntax error                   parser diagnostics
  cjk utf-16 columns             diagnostics/hover positions after CJK
                                 content on the same line (UTF-16, not bytes)
  didClose                       diagnostics cleared notification
  shutdown/exit                  process exits 0

Failure prints the offending frame and exits 1; success prints PASS.
"""

import json
import subprocess
import sys


def utf16_length(text):
    """LSP character = UTF-16 code units（与 run_lsp_session_test.py 同口径）。"""
    return len(text.encode("utf-16-le")) // 2


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
# CJK 列口径用例：诊断错误 token 与 hover 目标词都落在同行的 CJK 内容
# 之后——byte 列与 UTF-16 列相差 2 units/字，任何一侧没转口径都会偏。
SRC_CJK = (
    "class CDemo {\n"
    "    public int Val;\n"
    "    public string Tag() { return \"中文标签\"; }\n"
    "    string s = \"中文备注\"; Missing m = null; // 中文尾注 Val\n"
    "}\n"
)

OK_URI = "file:///t/Ok.zan"
BAD_URI = "file:///t/Bad.zan"
SYN_URI = "file:///t/Syn.zan"
CJK_URI = "file:///t/Cjk.zan"


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
                "completionProvider", "renameProvider",
                "documentHighlightProvider", "foldingRangeProvider"):
        if key not in caps:
            fail("initialize", "missing capability %s in %s" % (key, caps))
    if not caps["renameProvider"].get("prepareProvider"):
        fail("initialize", "renameProvider.prepareProvider missing: %r"
             % (caps["renameProvider"],))
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

    # CJK UTF-16 列口径：错误 token 在同行 CJK 之后，出站列必须是 UTF-16
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": CJK_URI, "languageId": "zan", "version": 1, "text": SRC_CJK}})
    diag = s.diag_for(CJK_URI)
    miss = [d for d in (diag or {}).get("diagnostics", [])
            if "undefined type 'Missing'" in d.get("message", "")]
    if not miss:
        fail("cjk diagnostics", "expected undefined type 'Missing', got %r"
             % (diag,))
    want_char = utf16_length('    string s = "中文备注"; ')
    got = miss[0]["range"]["start"]
    if got != {"line": 3, "character": want_char}:
        fail("cjk diagnostics",
             "expected UTF-16 col %d after CJK, got %r (byte col would be %d)"
             % (want_char, got, len('    string s = "中文备注"; ')))

    # 入站：hover 词在同行 CJK 注释之后，客户端给 UTF-16 列
    hover_pos = utf16_length(
        '    string s = "中文备注"; Missing m = null; // 中文尾注 ')
    hv = s.request("textDocument/hover", {
        "textDocument": {"uri": CJK_URI},
        "position": {"line": 3, "character": hover_pos}}).get("result")
    hv_text = hv.get("contents", {}).get("value", "") if hv else ""
    if "Val" not in hv_text or "field" not in hv_text:
        fail("cjk hover", "expected Val field hover via UTF-16 col, got %r"
             % (hv,))

    # ---- rename / prepareRename（词边界近似口径）----
    # 期望列全部从源码现算，避免手数错位
    line1 = SRC_OK.splitlines()[1]   # '    public int Val;'
    line2 = SRC_OK.splitlines()[2]   # '    public int Get() { return Val; }'
    v1 = line1.index("Val")
    v2 = line2.index("Val")
    on_val = {"line": 1, "character": v1 + 1}

    # prepareRename：光标在 Val → 词 range + placeholder
    pr = s.request("textDocument/prepareRename", {
        "textDocument": {"uri": OK_URI}, "position": on_val}).get("result")
    if not pr or pr.get("placeholder") != "Val" or \
            pr["range"]["start"] != {"line": 1, "character": v1} or \
            pr["range"]["end"] != {"line": 1, "character": v1 + 3}:
        fail("prepareRename", "expected Val range at char %d, got %r"
             % (v1, pr))

    # prepareRename 在空白处 → null
    pr = s.request("textDocument/prepareRename", {
        "textDocument": {"uri": OK_URI},
        "position": {"line": 1, "character": 2}}).get("result")
    if pr is not None:
        fail("prepareRename whitespace", "expected null, got %r" % (pr,))

    # rename Val→Count：声明 + return 共 2 处；CJK 行尾注释里的 Val
    # 跨文档也命中（词边界近似，含全部打开文档）
    ren = s.request("textDocument/rename", {
        "textDocument": {"uri": OK_URI},
        "position": on_val, "newName": "Count"}).get("result") or {}
    edits = (ren.get("changes") or {}).get(OK_URI)
    if not edits or len(edits) != 2:
        fail("rename", "expected 2 edits for Val, got %r" % (ren,))
    want_starts = [{"line": 1, "character": v1}, {"line": 2, "character": v2}]
    if [e["range"]["start"] for e in edits] != want_starts:
        fail("rename", "expected starts %r, got %r"
             % (want_starts, [e["range"]["start"] for e in edits]))
    if edits[0]["range"]["end"] != {"line": 1, "character": v1 + 3} or \
            any(e["newText"] != "Count" for e in edits):
        fail("rename", "bad edit shape: %r" % (edits,))
    # SRC_CJK 第 1 行同款字段声明 + 第 3 行行尾注释，共 2 处
    cjk_val = (ren.get("changes") or {}).get(CJK_URI)
    want_cjk = [
        {"line": 1, "character": line1.index("Val")},
        {"line": 3, "character": utf16_length(
            '    string s = "中文备注"; Missing m = null; // 中文尾注 ')},
    ]
    if not cjk_val or [e["range"]["start"] for e in cjk_val] != want_cjk:
        fail("rename cross-doc", "expected CJK_URI hits %r, got %r"
             % (want_cjk, cjk_val))

    # rename 非法新名 → null；关键字新名 → error -32602
    for bad in ("9Bad", "a-b", ""):
        ren = s.request("textDocument/rename", {
            "textDocument": {"uri": OK_URI},
            "position": on_val, "newName": bad}).get("result")
        if ren is not None:
            fail("rename invalid", "newName %r: expected null, got %r"
                 % (bad, ren))
    resp = s.request("textDocument/rename", {
        "textDocument": {"uri": OK_URI},
        "position": on_val, "newName": "class"})
    if resp.get("error", {}).get("code") != -32602:
        fail("rename keyword", "expected -32602, got %r" % (resp,))

    # CJK 行 rename：错误 token Missing（UTF-16 列口径）→ Gone；
    # BAD 文档里的 Missing 同词跨文档命中
    cjk_char = utf16_length('    string s = "中文备注"; ')
    ren = s.request("textDocument/rename", {
        "textDocument": {"uri": CJK_URI},
        "position": {"line": 3, "character": cjk_char},
        "newName": "Gone"}).get("result") or {}
    cjk_edits = (ren.get("changes") or {}).get(CJK_URI)
    if not cjk_edits or len(cjk_edits) != 1 or \
            cjk_edits[0]["range"]["start"] != {"line": 3, "character": cjk_char}:
        fail("cjk rename", "expected 1 edit at UTF-16 col %d, got %r"
             % (cjk_char, ren))
    bad_edits = (ren.get("changes") or {}).get(BAD_URI)
    if not bad_edits or len(bad_edits) != 1:
        fail("cjk rename cross-doc", "expected BAD_URI Missing hit, got %r"
             % (ren,))
    pr = s.request("textDocument/prepareRename", {
        "textDocument": {"uri": CJK_URI},
        "position": {"line": 3, "character": cjk_char}}).get("result")
    if not pr or pr.get("placeholder") != "Missing" or \
            pr["range"]["start"] != {"line": 3, "character": cjk_char}:
        fail("cjk prepareRename", "expected Missing at col %d, got %r"
             % (cjk_char, pr))

    # foldingRange：两个类体区间；同行闭合的 Get/Go 花括号不报
    fold = s.request("textDocument/foldingRange", {
        "textDocument": {"uri": OK_URI}}).get("result")
    if fold != [{"startLine": 0, "endLine": 2}, {"startLine": 4, "endLine": 5}]:
        fail("foldingRange", "expected [{0,2},{4,5}], got %r" % (fold,))

    # documentHighlight：Val 两处（声明 + return），kind=1 Text
    hl = s.request("textDocument/documentHighlight", {
        "textDocument": {"uri": OK_URI},
        "position": {"line": 1, "character": v1 + 1}}).get("result")
    want_hl = [
        {"range": {"start": {"line": 1, "character": v1},
                   "end": {"line": 1, "character": v1 + 3}}, "kind": 1},
        {"range": {"start": {"line": 2, "character": v2},
                   "end": {"line": 2, "character": v2 + 3}}, "kind": 1},
    ]
    if hl != want_hl:
        fail("documentHighlight", "expected %r, got %r" % (want_hl, hl))

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

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
  semanticTokens                 real-lexer token stream, symbol-index
                                 classified, delta-encoded; CJK string
                                 length in UTF-16
  formatting                     full-document indent rewrite edit
  rangeFormatting                line-range indent adjustment; empty on clean
  inlayHint                      inferred types (kind 1) + param names (kind 2)
  didChange                      re-analysis, version bump
  syntax error                   parser diagnostics
  cjk utf-16 columns             diagnostics/hover positions after CJK
                                 content on the same line (UTF-16, not bytes)
  didClose                       diagnostics cleared notification
  manifest changes               project boundaries update while stdin is idle
  shutdown/exit                  polling stops, then process exits 0

Failure prints the offending frame and exits 1; success prints PASS.
"""

import json
import queue
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


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
SRC_FMT = (
    "class FmtDemo {\n"
    "int a;\n"
    "void M() {\n"
    "int b;\n"
    "}\n"
    "}\n"
)
SRC_HINT = (
    "class Greeter {\n"
    "    static void Hello(string name, int age) {\n"
    "    }\n"
    "    static void Run() {\n"
    "        var x = 100;\n"
    "        Hello(\"world\", 42);\n"
    "    }\n"
    "}\n"
)

OK_URI = "file:///t/Ok.zan"
BAD_URI = "file:///t/Bad.zan"
SYN_URI = "file:///t/Syn.zan"
CJK_URI = "file:///t/Cjk.zan"
FMT_URI = "file:///t/Fmt.zan"
HINT_URI = "file:///t/Hint.zan"


class Session(object):
    def __init__(self, exe):
        self.proc = subprocess.Popen(
            [exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.serial = 10
        self.notifications = []
        self.pending = None  # response read while draining for a diagnostic
        self.frames = queue.Queue()
        threading.Thread(target=self._read_frames, daemon=True).start()

    def send(self, msg):
        self.proc.stdin.write(frame(msg))
        self.proc.stdin.flush()

    def expect_exit(self, reason):
        try:
            rc = self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
            fail(reason, "server did not exit")
        if rc != 0:
            fail(reason, "exit code %d" % rc)

    def request(self, method, params):
        self.serial += 1
        rid = str(self.serial)
        self.send({"jsonrpc": "2.0", "id": rid, "method": method,
                   "params": params})
        return self.wait_response(rid)

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def _read_frames(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.proc.stdout.readline()
                    if not line.endswith(b"\r\n"):
                        raise AssertionError("invalid or incomplete LSP header: %r" % line)
                    if line == b"\r\n":
                        break
                    key, _, value = line.partition(b":")
                    headers[key.strip().lower().decode("ascii")] = value.strip()
                size = int(headers["content-length"])
                body = self.proc.stdout.read(size)
                if len(body) != size:
                    raise AssertionError("server closed mid-body")
                self.frames.put(json.loads(body.decode("utf-8")))
        except Exception as error:
            self.frames.put(error)

    def _read_frame(self):
        try:
            frame = self.frames.get(timeout=10)
        except queue.Empty:
            raise AssertionError("timed out waiting for an LSP frame") from None
        if isinstance(frame, Exception):
            raise frame
        return frame

    def expect_quiet(self, seconds):
        try:
            frame = self.frames.get(timeout=seconds)
        except queue.Empty:
            return
        fail("unexpected LSP output", repr(frame))

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


def check_workspace(s, workspace):
    caller = (workspace / "app/src/Use.zan").as_uri()
    library = (workspace / "app/lib/Service.zan").as_uri()
    manifest = workspace / "app/src/zan.proj"
    library_manifest = workspace / "app/lib/zan.proj"

    def open_doc(uri, text):
        s.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "zan", "version": 1, "text": text}})

    def change_doc(uri, version, text):
        s.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": version},
            "contentChanges": [{"text": text}],
        })

    def close_doc(uri):
        s.notify("textDocument/didClose", {"textDocument": {"uri": uri}})

    def expect_diag(uri, version, messages):
        diag = s.diag_for(uri)
        actual = [d["message"] for d in diag["diagnostics"] if d["severity"] == 1]
        if diag["version"] != version or len(actual) != len(messages) or set(actual) != messages:
            fail("workspace diagnostics", "%s: expected version %d and %r, got %r"
                 % (uri, version, messages, diag))
        return diag["diagnostics"]

    text = ("using Library; class Use { static int Run(Service service) "
            "{ return Service.Read() + service.Count; } }")
    open_doc(caller, text)
    expect_diag(caller, 1, set())
    change_doc(caller, 2, "using Library; class Use { SiblingOnly a; NestedOnly b; }")
    expect_diag(caller, 2, {"undefined type 'SiblingOnly'", "undefined type 'NestedOnly'"})
    change_doc(caller, 3, "class Use { Service a; Other.Service b; }")
    errors = expect_diag(caller, 3, {"undefined type 'Service'", "undefined type 'Other.Service'"})
    for error in errors:
        if error["range"]["start"]["line"] != 0:
            fail("workspace diagnostics", "wrong source line: %r" % error)
    change_doc(caller, 4, text)
    expect_diag(caller, 4, set())

    open_doc(library, "namespace Library; public class Service { public int Count; }")
    expect_diag(caller, 4, {"type 'Library.Service' has no method 'Read'"})
    expect_diag(library, 1, set())
    change_doc(library, 2, "namespace Library; public class Service { public static int Read() { return 1; } }")
    expect_diag(caller, 4, {"type 'Library.Service' has no member 'Count'"})
    expect_diag(library, 2, set())
    change_doc(library, 1, "class Stale {}")
    close_doc(library)
    expect_diag(library, 2, set())
    expect_diag(caller, 4, set())

    # Verify the watched-file path independently by editing a dependency's
    # contents without changing any manifest.
    library_source = workspace / "app/lib/Service.zan"
    original = library_source.read_text(encoding="utf-8")
    for updated, messages in (("class Other {}", {"undefined type 'Service'"}), (original, set())):
        library_source.write_text(updated, encoding="utf-8")
        s.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": library, "type": 2}]})
        expect_diag(caller, 4, messages)

    check_project_navigation(s, workspace, open_doc, change_doc, close_doc, expect_diag)
    check_inheritance(workspace, open_doc, change_doc, close_doc, expect_diag)
    check_inheritance_edges(s, workspace, open_doc, change_doc, close_doc, expect_diag)
    check_nested_types(workspace, open_doc, change_doc, close_doc, expect_diag)

    # No client messages during these mutations. The final pair changes only
    # the dependency's project, while the open caller stays in app.
    missing = {"undefined type 'Service'"}
    manifest.write_text("name=NestedApp\n", encoding="utf-8")
    expect_diag(caller, 6, missing)
    manifest.rename(library_manifest)
    expect_diag(caller, 6, missing)
    isolated_def = s.request("textDocument/definition", {
        "textDocument": {"uri": caller},
        "position": {"line": 0, "character": text.index("Service.Read") + 1},
    }).get("result")
    if isolated_def is not None:
        fail("project boundary definition", "expected null across zan.proj, got %r" % (isolated_def,))
    library_manifest.unlink()
    expect_diag(caller, 6, set())
    restored_def = s.request("textDocument/definition", {
        "textDocument": {"uri": caller},
        "position": {"line": 0, "character": text.index("Service.Read") + 1},
    }).get("result")
    if not restored_def or restored_def.get("uri") != library:
        fail("project boundary definition restore", "expected %s, got %r" % (library, restored_def))
    library_manifest.write_text("name=NestedLib\n", encoding="utf-8")
    expect_diag(caller, 6, missing)
    library_manifest.unlink()
    expect_diag(caller, 6, set())
    s.expect_quiet(1.2)
    return manifest


def check_project_navigation(s, workspace, open_doc, change_doc, close_doc, expect_diag):
    """B-ID200: project-level definition/hover/completion/references/rename/
    workspaceSymbol/signatureHelp across unopened disk files with overlay
    precedence and zan.proj isolation."""
    caller = (workspace / "app/src/Use.zan").as_uri()
    library = (workspace / "app/lib/Service.zan").as_uri()
    helper_path = workspace / "app/lib/Helper.zan"
    helper_uri = helper_path.as_uri()
    sibling_uri = (workspace / "app-extra/Types.zan").as_uri()

    helper_path.write_text(
        "namespace Library;\n"
        "public class Helper {\n"
        "    public static int Compute(string key) { return 1; }\n"
        "    public static int Compute(string key, int count) { return count; }\n"
        "}\n",
        encoding="utf-8",
    )
    s.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": helper_uri, "type": 1}]})
    expect_diag(caller, 4, set())

    nav_text = (
        "using Library;\n"
        "class Use {\n"
        "    static int Run(Service service) {\n"
        "        int a = Service.Read() + service.Count;\n"
        "        int b = Helper.Compute(\"k\", 2);\n"
        "        return a + b;\n"
        "    }\n"
        "}\n"
    )
    change_doc(caller, 5, nav_text)
    expect_diag(caller, 5, set())

    lines = nav_text.splitlines()
    # 1. definition into unopened Service.zan: class Service, method Read, field Count
    def_svc = s.request("textDocument/definition", {
        "textDocument": {"uri": caller},
        "position": {"line": 2, "character": lines[2].index("Service") + 1},
    }).get("result")
    if not def_svc or def_svc["uri"] != library or def_svc["range"]["start"]["line"] != 0:
        fail("project definition Service", "expected %s:0, got %r" % (library, def_svc))

    def_read = s.request("textDocument/definition", {
        "textDocument": {"uri": caller},
        "position": {"line": 3, "character": lines[3].index("Read") + 1},
    }).get("result")
    if not def_read or def_read["uri"] != library or def_read["range"]["start"]["line"] != 1:
        fail("project definition Read", "expected %s:1, got %r" % (library, def_read))

    def_count = s.request("textDocument/definition", {
        "textDocument": {"uri": caller},
        "position": {"line": 3, "character": lines[3].index("Count") + 1},
    }).get("result")
    if not def_count or def_count["uri"] != library or def_count["range"]["start"]["line"] != 1:
        fail("project definition Count", "expected %s:1, got %r" % (library, def_count))

    # 2. hover on unopened Service.Read and service.Count
    hv_read = s.request("textDocument/hover", {
        "textDocument": {"uri": caller},
        "position": {"line": 3, "character": lines[3].index("Read") + 1},
    }).get("result")
    hv_val = (hv_read or {}).get("contents", {}).get("value", "")
    if "Read(" not in hv_val or "Service" not in hv_val or library not in hv_val:
        fail("project hover Read", "unexpected hover: %r" % (hv_read,))

    hv_count = s.request("textDocument/hover", {
        "textDocument": {"uri": caller},
        "position": {"line": 3, "character": lines[3].index("Count") + 1},
    }).get("result")
    hv_cval = (hv_count or {}).get("contents", {}).get("value", "")
    if "Count" not in hv_cval or "field" not in hv_cval or "Service" not in hv_cval:
        fail("project hover Count", "unexpected hover: %r" % (hv_count,))

    # 3. completion: project symbol + dot-member completion + zan.proj isolation
    comp_all = s.request("textDocument/completion", {
        "textDocument": {"uri": caller},
        "position": {"line": 2, "character": lines[2].index("Service") + 3},
    }).get("result") or []
    all_labels = [c["label"] for c in comp_all]
    if "Service" not in all_labels:
        fail("project completion", "missing Service in %r" % (all_labels,))
    if "SiblingOnly" in all_labels or "NestedOnly" in all_labels:
        fail("project completion isolation", "leaked other project symbols: %r" % (all_labels,))

    comp_dot = s.request("textDocument/completion", {
        "textDocument": {"uri": caller},
        "position": {"line": 3, "character": lines[3].index("service.Count") + len("service.")},
    }).get("result") or []
    dot_labels = [c["label"] for c in comp_dot]
    if "Count" not in dot_labels or "Read" not in dot_labels:
        fail("project dot completion", "expected Service members, got %r" % (dot_labels,))
    if "Compute" in dot_labels or "class" in dot_labels:
        fail("project dot completion filter", "non-Service items leaked: %r" % (dot_labels,))

    # 4. references across open Use.zan + unopened Service.zan (excluding app-extra)
    refs_incl = s.request("textDocument/references", {
        "textDocument": {"uri": caller},
        "position": {"line": 2, "character": lines[2].index("Service") + 1},
        "context": {"includeDeclaration": True},
    }).get("result") or []
    ref_uris = {r["uri"] for r in refs_incl}
    if caller not in ref_uris or library not in ref_uris:
        fail("project references", "expected both caller and library, got %r" % (refs_incl,))
    if sibling_uri in ref_uris:
        fail("project references isolation", "leaked sibling project: %r" % (refs_incl,))

    refs_excl = s.request("textDocument/references", {
        "textDocument": {"uri": caller},
        "position": {"line": 2, "character": lines[2].index("Service") + 1},
        "context": {"includeDeclaration": False},
    }).get("result") or []
    if len(refs_excl) >= len(refs_incl):
        fail("project references includeDeclaration=false",
             "expected fewer hits without decl: excl=%r incl=%r" % (refs_excl, refs_incl))

    # 5. rename across open Use.zan + unopened Service.zan
    ren = s.request("textDocument/rename", {
        "textDocument": {"uri": caller},
        "position": {"line": 3, "character": lines[3].index("Count") + 1},
        "newName": "TotalCount",
    }).get("result")
    changes = (ren or {}).get("changes") or {}
    if caller not in changes or library not in changes or sibling_uri in changes:
        fail("project rename", "expected edits in caller & library only, got %r" % (ren,))

    # 6. workspace/symbol across open + unopened project files
    ws_syms = s.request("workspace/symbol", {"query": "comp"}).get("result") or []
    comp_hits = [x for x in ws_syms if x.get("name") == "Compute"
                 and x.get("containerName") == "Helper"
                 and x.get("location", {}).get("uri") == helper_uri]
    if len(comp_hits) < 2:
        fail("workspace/symbol", "expected 2 Helper.Compute overloads, got %r" % (ws_syms,))

    # 7. signatureHelp across unopened Helper.zan with overload selection
    sig_cross = s.request("textDocument/signatureHelp", {
        "textDocument": {"uri": caller},
        "position": {"line": 4, "character": lines[4].index("2)")},
    }).get("result")
    if not sig_cross or sig_cross.get("activeParameter") != 1 or sig_cross.get("activeSignature") != 1:
        fail("project signatureHelp", "expected activeSig=1 activeParam=1, got %r" % (sig_cross,))
    sig_labels = [x.get("label") for x in sig_cross.get("signatures") or []]
    if "int Compute(string key)" not in sig_labels or \
            "int Compute(string key, int count)" not in sig_labels:
        fail("project signatureHelp labels", "got %r" % (sig_labels,))

    # 7b. inlayHint across unopened Helper.zan
    proj_hints = s.request("textDocument/inlayHint", {
        "textDocument": {"uri": caller},
        "range": {"start": {"line": 0, "character": 0}, "end": {"line": 7, "character": 0}},
    }).get("result") or []
    hint_labels = [h.get("label") for h in proj_hints]
    if "key:" not in hint_labels or "count:" not in hint_labels:
        fail("project inlayHint", "expected key:/count: from unopened Helper.zan, got %r" % (proj_hints,))

    # 8. live overlay precedence: opening Service.zan supersedes disk without duplicates
    open_doc(library, "namespace Library;\npublic class Service {\n    public int LiveOnly;\n}\n")
    expect_diag(caller, 5, {
        "type 'Library.Service' has no method 'Read'",
        "type 'Library.Service' has no member 'Count'",
    })
    expect_diag(library, 1, set())
    ws_live = s.request("workspace/symbol", {"query": "liveonly"}).get("result") or []
    if not any(x.get("name") == "LiveOnly" and x.get("location", {}).get("uri") == library for x in ws_live):
        fail("overlay workspace/symbol", "expected LiveOnly from live buffer, got %r" % (ws_live,))
    ws_svc = [x for x in (s.request("workspace/symbol", {"query": "service"}).get("result") or [])
              if x.get("name") == "Service" and x.get("location", {}).get("uri") == library]
    if len(ws_svc) != 1:
        fail("overlay dedup", "expected exactly 1 Service symbol for library, got %r" % (ws_svc,))

    close_doc(library)
    expect_diag(library, 1, set())
    expect_diag(caller, 5, set())
    ws_after = s.request("workspace/symbol", {"query": "liveonly"}).get("result") or []
    if any(x.get("name") == "LiveOnly" for x in ws_after):
        fail("overlay close restore", "LiveOnly should disappear after close: %r" % (ws_after,))

    # Restore caller version/text expected by downstream manifest assertions
    helper_path.unlink()
    s.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": helper_uri, "type": 3}]})
    expect_diag(caller, 5, set())
    ws_deleted = s.request("workspace/symbol", {"query": "comp"}).get("result") or []
    if any(x.get("name") == "Compute" and x.get("location", {}).get("uri") == helper_uri
           for x in ws_deleted):
        fail("watched file delete eviction", "Helper.Compute still indexed after delete: %r" % (ws_deleted,))
    def_deleted = s.request("textDocument/definition", {
        "textDocument": {"uri": caller},
        "position": {"line": 4, "character": lines[4].index("Helper") + 1},
    }).get("result")
    if def_deleted is not None:
        fail("watched file delete definition", "expected null after Helper.zan delete, got %r" % (def_deleted,))
    orig_text = ("using Library; class Use { static int Run(Service service) "
                 "{ return Service.Read() + service.Count; } }")
    change_doc(caller, 6, orig_text)
    expect_diag(caller, 6, set())


CONTRACTS = (
    "namespace Contracts;\n"
    "public interface IRead<T> { int Read(T input); int Optional(); }\n"
    "public interface IReader : IRead<int> { int Optional() { return 0; } }\n"
    "public abstract class AbstractReader { public abstract int Fetch(); }\n"
    "public abstract class Middle : AbstractReader {}\n"
    "public class BaseReader { public int Read(int input) { return input; } }\n"
)


def check_inheritance(workspace, open_doc, change_doc, close_doc, expect_diag):
    implementation = (workspace / "app/contracts/Implementation.zan").as_uri()
    dependency = (workspace / "app/contracts/Contracts.zan").as_uri()
    missing_read = "[E1015] type 'Reader' does not implement interface method 'Read'"
    missing_fetch = ("[E2093] non-abstract class 'ConcreteReader' does not implement abstract method "
                     "'Fetch' inherited from 'Contracts.AbstractReader'")
    open_doc(implementation, "using Contracts;\n"
             "class Reader : IReader { public int Read(string input) { return 0; } }\n"
             "class ConcreteReader : Middle {}\n"
             "class Unknown : Helpr {}\n"
             "class UnknownInterface : BaseReader, NoContract {}\n")
    errors = expect_diag(implementation, 1, {
        missing_read, missing_fetch, "undefined base type 'Helpr'", "undefined base type 'NoContract'",
    })
    expected_positions = {missing_read: (1, 0), missing_fetch: (2, 0),
                          "undefined base type 'Helpr'": (3, 16),
                          "undefined base type 'NoContract'": (4, 37)}
    for error in errors:
        start = error["range"]["start"]
        if (start["line"], start["character"]) != expected_positions[error["message"]]:
            fail("inheritance diagnostic range", repr(error))

    change_doc(implementation, 2, "using Contracts;\n"
               "class Reader : BaseReader, IReader {}\n"
               "class ConcreteReader : Middle { public override int Fetch() { return 1; } }\n")
    expect_diag(implementation, 2, set())
    open_doc(dependency, CONTRACTS.replace("IRead<int>", "IRead<string>"))
    expect_diag(implementation, 2, {missing_read})
    expect_diag(dependency, 1, set())
    close_doc(dependency)
    expect_diag(dependency, 1, set())
    expect_diag(implementation, 2, set())
    close_doc(implementation)
    expect_diag(implementation, 2, set())


def check_inheritance_edges(s, workspace, open_doc, change_doc, close_doc, expect_diag):
    edges = (workspace / "app/inheritance/Edges.zan").as_uri()
    text = (
        "namespace Inheritance;\n"
        "class Alpha : Beta {}\n"
        "interface IFirst : ISecond {}\n"
        "class Illegal : Closed {}\n"
        "class Hides : Base { public virtual int Read(string value) { return 0; } }\n"
        "class Overload : Base { public virtual int Read() { return 0; } }\n"
        "class Replaces : Base { public override int Read(int value) { return value; } }\n"
    )
    class_cycle = "[E1011] cyclic inheritance involving 'Inheritance.Alpha'"
    interface_cycle = "[E1011] cyclic inheritance involving 'Inheritance.IFirst'"
    sealed_base = "[E1014] cannot derive from sealed type 'Inheritance.Closed'"
    hides_slot = ("[W2097] 'Read' is declared 'virtual' but hides an inherited virtual with the same "
                  "name and arity; calls through base-class references will run the base implementation "
                  "-- use 'override' to replace it")
    open_doc(edges, text)
    diagnostics = expect_diag(edges, 1, {class_cycle, interface_cycle, sealed_base})
    expected = {
        class_cycle: (1, 1, 0),
        interface_cycle: (1, 2, 0),
        sealed_base: (1, 3, 16),
        hides_slot: (2, 4, 36),
    }
    actual = {d["message"]: (d["severity"], d["range"]["start"]["line"],
                             d["range"]["start"]["character"]) for d in diagnostics}
    if len(diagnostics) != len(expected) or actual != expected:
        fail("inheritance edge diagnostics", repr(diagnostics))

    response = s.request("textDocument/documentSymbol", {"textDocument": {"uri": edges}})
    if not any(symbol["name"] == "Alpha" for symbol in response.get("result", [])):
        fail("request with inheritance cycle", repr(response))

    repaired = (text.replace("Alpha : Beta", "Alpha")
                .replace("IFirst : ISecond", "IFirst")
                .replace("Illegal : Closed", "Legal")
                .replace("virtual int Read(string value)", "override int Read(int value)"))
    change_doc(edges, 2, repaired)
    if expect_diag(edges, 2, set()):
        fail("inheritance recovery", "diagnostics remain after fixing the inheritance clauses")
    close_doc(edges)
    expect_diag(edges, 2, set())


def check_nested_types(workspace, open_doc, change_doc, close_doc, expect_diag):
    use = (workspace / "app/qualified/Use.zan").as_uri()
    imported = (
        "using Library;\n"
        "class Holder { public Value Inner; }\n"
        "struct Value { public int ValueOnly() { return 1; } }\n"
        "class Use : Outer.Inner {\n"
        "    Library.Outer.Inner first;\n"
        "    Other.Outer.Inner second;\n"
        "    int Imported() { return Outer.Inner.Read(); }\n"
        "    int Shadowed(Holder Outer, int Inner) {\n"
        "        return Outer.Inner.ValueOnly() + Library.Outer.Inner.Read() + Inner;\n"
        "    }\n"
        "}\n"
    )
    open_doc(use, imported)
    expect_diag(use, 1, set())

    relative = (
        "namespace Library.Client;\n"
        "class Use {\n"
        "    Outer.Inner first;\n"
        "    int Call() { return Outer.Inner.Read(); }\n"
        "}\n"
    )
    change_doc(use, 2, relative)
    expect_diag(use, 2, set())

    scoped = (
        "namespace Library;\n"
        "public partial class Outer {\n"
        "    Inner item;\n"
        "    int Call() { return Inner.Read(); }\n"
        "    public class Branch : Inner {\n"
        "        Inner local;\n"
        "        Deep.Leaf leaf;\n"
        "        int ReadSibling() { return Inner.Read() + Deep.Leaf.ReadLeaf(); }\n"
        "    }\n"
        "}\n"
    )
    change_doc(use, 3, scoped)
    expect_diag(use, 3, set())

    invalid = (
        "using Library;\n"
        "class Use {\n"
        "    Other.Missing.Inner wrongOwner;\n"
        "    Missing.Outer.Inner wrongNamespace;\n"
        "    Inner unqualified;\n"
        "    int Read() { return Other.Outer.Inner.Read(); }\n"
        "}\n"
    )
    change_doc(use, 4, invalid)
    errors = expect_diag(use, 4, {
        "undefined type 'Other.Missing.Inner'",
        "undefined type 'Missing.Outer.Inner'",
        "undefined type 'Inner'",
        "type 'Other.Outer.Inner' has no method 'Read'",
    })
    if {d["range"]["start"]["line"] for d in errors} != {2, 3, 4, 5}:
        fail("nested type diagnostic range", repr(errors))
    change_doc(use, 5, imported)
    expect_diag(use, 5, set())
    close_doc(use)
    expect_diag(use, 5, set())


def main():
    exe = sys.argv[sys.argv.index("--exe") + 1]
    with tempfile.TemporaryDirectory(prefix="zan lsp + ") as temp:
        workspace = Path(temp)
        sources = {
            "app/zan.proj": "name=App\n",
            "app/src/Use.zan": "class DiskUse {}\n",
            "app/lib/Service.zan": ("namespace Library; public class Service {\n"
                                    "public int Count; public static int Read() { return 1; }\n"
                                    "MissingInDependency bad; }\n"),
            "app/Broken.zan": "/* unfinished comment\n",
            "app/contracts/zan.proj": "name=Contracts\n",
            "app/contracts/Contracts.zan": CONTRACTS,
            "app/inheritance/zan.proj": "name=Inheritance\n",
            "app/inheritance/Bases.zan": (
                "namespace Inheritance;\n"
                "class Beta : Alpha {}\n"
                "interface IEmpty {}\n"
                "interface ISecond : IEmpty, IFirst {}\n"
                "sealed class Closed {}\n"
                "class Ancestor { public virtual int Read(int value) { return value; } }\n"
                "class Base : Ancestor {}\n"
                "class Probe { void Use(ISecond value) { value.Missing(); } }\n"
            ),
            "app/qualified/zan.proj": "name=Qualified\n",
            "app/qualified/Library.zan": (
                "namespace Library;\n"
                "public partial class Outer {\n"
                "    public class Inner { public static int Read() { return 1; } }\n"
                "    public class Deep {\n"
                "        public class Leaf { public static int ReadLeaf() { return 2; } }\n"
                "    }\n"
                "}\n"
            ),
            "app/qualified/Other.zan": (
                "namespace Other;\n"
                "public class Outer { public class Inner {} }\n"
            ),
            "app/nested/zan.proj": "name=Nested\n",
            "app/nested/Types.zan": "namespace Library; public class NestedOnly {}\n",
            "app-extra/zan.proj": "name=Sibling\n",
            "app-extra/Types.zan": "namespace Library; public class Service {} public class SiblingOnly {}\n",
        }
        for name, text in sources.items():
            source = workspace / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text(text, encoding="utf-8")
        run_session(exe, workspace)
        check_workspace_eof(exe, workspace)
    print("PASS tool_lsp_zan_session")


def check_workspace_eof(exe, workspace):
    s = Session(exe)
    s.request("initialize", {"rootUri": workspace.as_uri() + "/"})
    uri = (workspace / "app/src/Use.zan").as_uri()
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "zan", "version": 1, "text": "class Use {}"}})
    diag = s.diag_for(uri)
    if diag["version"] != 1 or diag["diagnostics"]:
        fail("workspace EOF", "expected a clean open document, got %r" % diag)
    s.proc.stdin.close()
    s.expect_exit("workspace EOF")


def run_session(exe, workspace):
    s = Session(exe)

    # initialize
    caps = s.request("initialize", {"rootUri": workspace.as_uri() + "/"}).get(
        "result", {}).get("capabilities", {})
    for key in ("definitionProvider", "referencesProvider",
                "documentSymbolProvider", "workspaceSymbolProvider",
                "hoverProvider", "completionProvider",
                "signatureHelpProvider", "renameProvider",
                "documentHighlightProvider", "foldingRangeProvider",
                "documentFormattingProvider", "documentRangeFormattingProvider",
                "inlayHintProvider",
                "semanticTokensProvider"):
        if key not in caps:
            fail("initialize", "missing capability %s in %s" % (key, caps))
    sig_triggers = caps["signatureHelpProvider"].get("triggerCharacters", [])
    if "(" not in sig_triggers or "," not in sig_triggers:
        fail("initialize", "signatureHelpProvider triggerCharacters wrong: %r"
             % (caps["signatureHelpProvider"],))
    sem_legend = caps["semanticTokensProvider"].get("legend", {})
    if sem_legend.get("tokenTypes", [])[2] != "class" or \
            not caps["semanticTokensProvider"].get("full"):
        fail("initialize", "semanticTokens legend/full wrong: %r"
             % (caps["semanticTokensProvider"],))
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
    if "Go" in children:
        fail("documentSymbol", "ZUse.Go leaked into ZDemo children: %r" % (children,))
    if children["Get"]["range"]["start"]["line"] != 2:
        fail("documentSymbol", "Get on wrong line: %r" % (children["Get"],))
    use_children = {d["name"]: d for d in top["ZUse"].get("children", [])}
    if use_children.get("Go", {}).get("kind") != 6 or "Val" in use_children:
        fail("documentSymbol", "ZUse children wrong: %r" % (use_children,))

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

    # semanticTokens/full：真 Lexer 流，delta 还原绝对 (line, char, len,
    # type) 后断关键分类。legend：11 keyword 1 type 2 class 8 property
    # 10 method 7 variable 12 string
    def decode_sem(data):
        out, pl, pc = [], 0, 0
        for k in range(0, len(data), 5):
            dl, ds, ln, ty = data[k], data[k + 1], data[k + 2], data[k + 3]
            line = pl + dl
            char = (pc + ds) if dl == 0 else ds
            out.append((line, char, ln, ty))
            pl, pc = line, char
        return out

    line0 = SRC_OK.splitlines()[0]   # 'class ZDemo {'
    g1 = line2.index("Get")
    st = decode_sem(s.request("textDocument/semanticTokens/full", {
        "textDocument": {"uri": OK_URI}}).get("result", {}).get("data", []))
    want_st = {
        (0, 0, 5, 11),                       # class
        (0, line0.index("ZDemo"), 5, 2),     # ZDemo → class 符号
        (1, v1, 3, 8),                       # Val → field/property
        (2, g1, 3, 10),                      # Get → method
        (1, line1.index("int"), 3, 1),       # int → 内建类型
        (2, line2.index("return"), 6, 11),   # return → keyword
    }
    missing = want_st - set(st)
    if missing:
        fail("semanticTokens", "missing tokens %r in %r" % (missing, st))
    # 相对编码单调性：行不减，同行内列不减
    for a, b in zip(st, st[1:]):
        if (b[0], b[1]) < (a[0], a[1]):
            fail("semanticTokens", "non-monotonic order: %r then %r" % (a, b))

    # CJK 行：字符串字面量 length 走 UTF-16。"中文备注" 是 BMP 汉字
    # （UTF-16 每字 1 unit，非 4 字节 UTF-8 代理对）：4 units + 2 引号 = 6
    st = decode_sem(s.request("textDocument/semanticTokens/full", {
        "textDocument": {"uri": CJK_URI}}).get("result", {}).get("data", []))
    cjk_line = SRC_CJK.splitlines()[3]
    q = cjk_line.index('"')
    if (3, q, 6, 12) not in st:
        fail("semanticTokens cjk", "expected (3,%d,6,12) string literal, "
             "got %r" % (q, st))
    if (3, cjk_line.index("string"), 6, 1) not in st:
        fail("semanticTokens cjk", "expected builtin string -> type, got %r"
             % (st,))

    # formatting / rangeFormatting
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": FMT_URI, "languageId": "zan", "version": 1, "text": SRC_FMT}})

    # formatting: full document format edit
    fmt_res = s.request("textDocument/formatting", {
        "textDocument": {"uri": FMT_URI},
        "options": {"tabSize": 4, "insertSpaces": True}}).get("result")
    want_fmt_text = (
        "class FmtDemo {\n"
        "    int a;\n"
        "    void M() {\n"
        "        int b;\n"
        "    }\n"
        "}\n"
    )
    if not isinstance(fmt_res, list) or len(fmt_res) != 1:
        fail("formatting", "expected 1 full edit, got %r" % (fmt_res,))
    if fmt_res[0].get("newText") != want_fmt_text:
        fail("formatting", "expected formatted text %r, got %r"
             % (want_fmt_text, fmt_res[0].get("newText")))

    # rangeFormatting: format lines 1..3 of FMT_URI
    rf_res = s.request("textDocument/rangeFormatting", {
        "textDocument": {"uri": FMT_URI},
        "range": {"start": {"line": 1, "character": 0},
                  "end": {"line": 3, "character": 0}},
        "options": {"tabSize": 4, "insertSpaces": True}}).get("result")
    # line 1 (int a;) -> 4 spaces, line 2 (void M() {) -> 4 spaces, line 3 (int b;) -> 8 spaces
    want_rf = [
        {"range": {"start": {"line": 1, "character": 0}, "end": {"line": 1, "character": 0}}, "newText": "    "},
        {"range": {"start": {"line": 2, "character": 0}, "end": {"line": 2, "character": 0}}, "newText": "    "},
        {"range": {"start": {"line": 3, "character": 0}, "end": {"line": 3, "character": 0}}, "newText": "        "},
    ]
    if rf_res != want_rf:
        fail("rangeFormatting", "expected %r, got %r" % (want_rf, rf_res))

    # rangeFormatting on already-clean lines produces empty edits []
    rf_clean = s.request("textDocument/rangeFormatting", {
        "textDocument": {"uri": OK_URI},
        "range": {"start": {"line": 0, "character": 0},
                  "end": {"line": 2, "character": 0}},
        "options": {"tabSize": 4, "insertSpaces": True}}).get("result")
    if rf_clean != []:
        fail("rangeFormatting clean", "expected [], got %r" % (rf_clean,))

    s.notify("textDocument/didClose", {"textDocument": {"uri": FMT_URI}})

    # inlayHint: Greeter with var x and Hello("world", 42) call
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": HINT_URI, "languageId": "zan", "version": 1, "text": SRC_HINT}})

    hints = s.request("textDocument/inlayHint", {
        "textDocument": {"uri": HINT_URI}}).get("result") or []
    # 期望 3 条 hint：
    # 1. var x: line 4, label ": int", kind 1, paddingLeft True
    # 2. arg "world": line 5, label "name:", kind 2, paddingRight True
    # 3. arg 42: line 5, label "age:", kind 2, paddingRight True
    type_hints = [h for h in hints if h.get("kind") == 1]
    param_hints = [h for h in hints if h.get("kind") == 2]
    if len(type_hints) != 1 or type_hints[0].get("label") != ": int" or \
            not type_hints[0].get("paddingLeft"):
        fail("inlayHint", "expected 1 type hint ': int', got %r" % (type_hints,))
    if type_hints[0].get("position", {}).get("line") != 4:
        fail("inlayHint", "expected type hint on line 4, got %r" % (type_hints[0],))

    p_labels = [h.get("label") for h in param_hints]
    if "name:" not in p_labels or "age:" not in p_labels:
        fail("inlayHint", "expected parameter hints 'name:' and 'age:', got %r"
             % (param_hints,))
    for ph in param_hints:
        if ph.get("position", {}).get("line") != 5 or not ph.get("paddingRight"):
            fail("inlayHint", "wrong param hint pos/padding: %r" % (ph,))

    # range 过滤：只查第 4 行
    line4_hints = s.request("textDocument/inlayHint", {
        "textDocument": {"uri": HINT_URI},
        "range": {"start": {"line": 4, "character": 0},
                  "end": {"line": 4, "character": 30}}}).get("result") or []
    if len(line4_hints) != 1 or line4_hints[0].get("label") != ": int":
        fail("inlayHint range", "expected only ': int' for line 4, got %r"
             % (line4_hints,))

    # signatureHelp on Greeter.Hello("world", 42) (line 5)
    hint_line5 = SRC_HINT.splitlines()[5]
    sig0 = s.request("textDocument/signatureHelp", {
        "textDocument": {"uri": HINT_URI},
        "position": {"line": 5, "character": hint_line5.index('"world"') + 1},
    }).get("result")
    if not sig0 or sig0.get("activeParameter") != 0 or sig0.get("activeSignature") != 0:
        fail("signatureHelp arg0", "got %r" % (sig0,))
    sigs0 = sig0.get("signatures") or []
    if len(sigs0) != 1 or sigs0[0].get("label") != "void Hello(string name, int age)":
        fail("signatureHelp label", "got %r" % (sigs0,))
    param_labels = [p.get("label") for p in sigs0[0].get("parameters") or []]
    if param_labels != ["string name", "int age"]:
        fail("signatureHelp params", "got %r" % (param_labels,))

    sig1 = s.request("textDocument/signatureHelp", {
        "textDocument": {"uri": HINT_URI},
        "position": {"line": 5, "character": hint_line5.index("42")},
    }).get("result")
    if not sig1 or sig1.get("activeParameter") != 1:
        fail("signatureHelp arg1", "expected activeParameter=1, got %r" % (sig1,))

    sig_none = s.request("textDocument/signatureHelp", {
        "textDocument": {"uri": HINT_URI},
        "position": {"line": 0, "character": 2},
    }).get("result")
    if sig_none is not None:
        fail("signatureHelp outside call", "expected null, got %r" % (sig_none,))

    s.notify("textDocument/didClose", {"textDocument": {"uri": HINT_URI}})

    # didClose clears diagnostics
    s.notify("textDocument/didClose", {"textDocument": {"uri": SYN_URI}})
    diag = s.diag_for(SYN_URI)
    if diag is None or diag["diagnostics"] != []:
        fail("didClose", "expected cleared diagnostics, got %r" % (diag,))

    manifest = check_workspace(s, workspace)

    # Keep a project document open so an active poller would publish on change.
    resp = s.request("shutdown", {})
    if "result" not in resp or resp["result"] is not None:
        fail("shutdown", "expected a null result in %r" % (resp,))
    manifest.write_text("name=AfterShutdown\n", encoding="utf-8")
    s.expect_quiet(2.2)
    s.notify("exit", None)
    s.expect_exit("exit")


if __name__ == "__main__":
    main()

"""Exercise cross-file diagnostics and document revisions through C zan-lsp stdio."""

import argparse
import json
import queue
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


DISK_DEPENDENCY = """namespace App.Services;
public class Service {
    public int Count;
    public static int Read() { return 1; }
    MissingSignatureType broken;
}
public interface IReader { int Read(); }
public abstract class AbstractReader { public abstract int Read(); }
"""
MISSING_METHOD = "[E2084] type 'App.Services.Service' has no method 'Read'"


PROJECT_SOURCES = {
    "parent/zan.proj": "name=Parent\n",
    "parent-extra/zan.proj": "name=Sibling\n",
    "parent/nested/zan.proj": "name=Nested\n",
    "parent/Token.zan": "namespace Shared; public class ParentToken {}",
    "parent/src/lib/Helper.zan": """namespace Shared;
public class Helper { public static int ParentOnly() { return 1; } }
""",
    "parent-extra/src/Helper.zan": """namespace Shared;
public class Helper { public static int SiblingOnly() { return 2; } }
public class SiblingOnly {}
""",
    "parent/nested/src/Library.zan": """namespace Shared;
public class NestedOnly {}
""",
}


CASES = [
    ("parent/src/Valid.zan", """using Shared;
class ParentUse { static int Read() { return Helper.ParentOnly(); } }
""", set()),
    ("parent/src/Invalid.zan", """using Shared;
class InvalidUse { SiblingOnly sibling; NestedOnly nested; }
""", {"[E1005] undefined type 'SiblingOnly'", "[E1005] undefined type 'NestedOnly'"}),
    ("parent-extra/src/Valid.zan", """using Shared;
class SiblingUse { static int Read() { return Helper.SiblingOnly(); } }
""", set()),
    ("parent/nested/src/Invalid.zan", """using Shared;
class InvalidUse { Helper fromParent; }
""", {"[E1005] undefined type 'Helper'"}),
    ("loose.zan", """using Shared;
class LooseUse { Helper fromProject; }
""", {"[E1005] undefined type 'Helper'"}),
    ("lib.zan", """namespace App.Shared;

public class Helper {
    public static string Tag() { return "ok"; }
    public class Nested {}
}
""", set()),
    ("qualified.zan", """class Qualified : App.Shared.Helper {
    App.Shared.Helper value;
}
""", set()),
    ("wrong_qualifier.zan", """using App.Shared;
class WrongQualifier {
    App.Other.Helper value;
}
""", {"[E1005] undefined type 'App.Other.Helper'"}),
    ("nested_qualifier.zan", """class NestedQualifier {
    Helper.Nested value;
}
""", {"[E1005] undefined type 'Helper.Nested'"}),
    ("main.zan", """using App.Shared;

class Prog {
    static void Main() {
        Helper h = new Helper();
        string s = Helper.Tag();
    }
}
""", set()),
    ("members.zan", """using App.Services;
class MemberUse {
    static int Read(Service service) {
        return Service.Read() + service.Count;
    }
}
""", set()),
    ("member_typos.zan", """using App.Services;
class MemberTypos {
    static int Read(Service service) {
        int a = App.Services.Service.Reed();
        int b = service.Cuont;
        return a + b;
    }
}
""", {"[E2084] type 'App.Services.Service' has no method 'Reed'",
      "[E2084] type 'App.Services.Service' has no member 'Cuont'"}),
    ("generic_shadow.zan", """namespace App.Services;
class GenericShadow<Service> {
    Service Echo(Service value) { return value; }
}
class GenericUse {
    static int Run(GenericShadow<int> box) { return box.Echo(3); }
}
""", set()),
    ("reserved.zan", """namespace App.Services;
class Task {}
""", {"[E1009] type declaration 'Task' collides with a built-in type name"}),
    ("missing_implementation.zan", """using App.Services;
class Reader : IReader {}
""", {"[E1015] type 'Reader' does not implement interface method 'Read'"}),
    ("missing_override.zan", """using App.Services;
class ConcreteReader : AbstractReader {}
""", {"[E2093] non-abstract class 'ConcreteReader' does not implement abstract method "
      "'Read' inherited from 'App.Services.AbstractReader'"}),
    ("no_import.zan", """// namespace App.Shared;
class NoImport {
    Helper value;
}
""", {"[E1005] undefined type 'Helper'"}),
    ("typo.zan", """using App.Shared;
class Typo {
    Helpr value;
}
""", {"[E1005] undefined type 'Helpr'"}),
    ("derived.zan", """using App.Shared;
class Derived : Helper {}
""", set()),
    ("same_namespace.zan", """// namespace Other;
namespace App /* comment between name parts */ . Shared;
class SameNamespace : Helper {}
""", set()),
    ("missing_base.zan", """using App.Shared;
class MissingBase : Helpr {}
""", {"[E1013] undefined base type 'Helpr'"}),
]


def frame(message):
    body = json.dumps(message).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def read_messages(stdout, messages):
    try:
        while True:
            headers = {}
            while True:
                line = stdout.readline()
                if not line:
                    raise EOFError("zan-lsp closed stdout")
                if line == b"\r\n":
                    break
                key, value = line.split(b":", 1)
                headers[key.lower()] = value.strip()
            size = int(headers[b"content-length"])
            body = stdout.read(size)
            if len(body) != size:
                raise EOFError("incomplete LSP message")
            messages.put(json.loads(body))
    except (EOFError, KeyError, OSError, ValueError) as exc:
        messages.put(exc)


def run_session(proc, workspace, messages, deadline):
    def send(method, params=None, request_id=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if request_id is not None:
            message["id"] = request_id
        proc.stdin.write(frame(message))
        proc.stdin.flush()

    def receive_until(matches, waiting_for):
        while True:
            remaining = deadline - time.monotonic()
            try:
                if remaining <= 0:
                    raise queue.Empty
                message = messages.get(timeout=remaining)
            except queue.Empty:
                raise TimeoutError(f"timed out waiting for {waiting_for}") from None
            if isinstance(message, Exception):
                raise RuntimeError(f"waiting for {waiting_for}: {message}") from message
            if matches(message):
                return message

    def request(method, request_id, params=None):
        send(method, params, request_id)
        response = receive_until(lambda msg: msg.get("id") == request_id, method)
        if "result" not in response:
            raise RuntimeError(f"{method} failed: {response}")

    request("initialize", 1, {"rootUri": workspace.as_uri() + "/", "capabilities": {}})
    send("initialized", {})
    pending = {}
    for name, text, wanted in CASES:
        uri = (workspace / name).as_uri()
        pending[uri] = name, wanted
        send("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "zan", "version": 1, "text": text,
        }})

    def is_pending_diagnostics(msg):
        return (msg.get("method") == "textDocument/publishDiagnostics"
                and msg["params"]["uri"] in pending
                and msg["params"].get("version") == 1)

    while pending:
        waiting_for = ", ".join(name for name, _ in pending.values())
        params = receive_until(is_pending_diagnostics, f"diagnostics for {waiting_for}")["params"]
        name, wanted = pending.pop(params["uri"])
        actual = {d["message"] for d in params["diagnostics"] if d["severity"] == 1}
        if actual != wanted:
            raise RuntimeError(f"{name}: expected {sorted(wanted)}, got {sorted(actual)}")

    def next_diagnostics(uri, version, wanted):
        def for_document(msg):
            return (msg.get("method") == "textDocument/publishDiagnostics"
                    and msg["params"]["uri"] == uri)

        params = receive_until(for_document, f"document revision {version}")["params"]
        actual = {d["message"] for d in params["diagnostics"] if d["severity"] == 1}
        if params.get("version") != version or actual != wanted:
            raise RuntimeError(f"revision {version}: expected {sorted(wanted)}, got {params}")

    def open_document(uri, version, text):
        send("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "zan", "version": version, "text": text,
        }})

    def change_document(uri, version, text):
        send("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                        "contentChanges": [{"text": text}]})

    def close_document(uri):
        send("textDocument/didClose", {"textDocument": {"uri": uri}})

    # An unsaved dependency changes its own project's caller at the same version.
    project_lib = (workspace / "parent/src/lib/Helper.zan").as_uri()
    project_caller = (workspace / "parent/src/Valid.zan").as_uri()
    open_document(project_lib, 1, "namespace Shared; public class Helper {}")
    next_diagnostics(project_caller, 1, {"[E2084] type 'Shared.Helper' has no method 'ParentOnly'"})
    close_document(project_lib)
    next_diagnostics(project_caller, 1, set())

    # Reopen the last document slot with a lower version and different text.
    uri = (workspace / "missing_base.zan").as_uri()
    change_document(uri, 2, "class Changing { StillMissing value; }")
    change_document(uri, 3, "class Changing {}")
    change_document(uri, 2, "class Changing { StaleValue value; }")
    next_diagnostics(uri, 3, set())

    close_document(uri)
    next_diagnostics(uri, 3, set())
    open_document(uri, 0, "class Reopened { NewlyMissing value; }")
    next_diagnostics(uri, 0, {"[E1005] undefined type 'NewlyMissing'"})
    change_document(uri, 1, "class Reopened {}")
    next_diagnostics(uri, 1, set())

    # Closing a dirty document publishes its clear before the same version is reused.
    change_document(uri, 2, "class Reopened { ClosedDraft value; }")
    close_document(uri)
    open_document(uri, 2, "class Reopened { NewDraft value; }")
    next_diagnostics(uri, 2, set())
    next_diagnostics(uri, 2, {"[E1005] undefined type 'NewDraft'"})

    # Closing the unsaved overlay restores the disk declarations for other files.
    lib = (workspace / "lib.zan").as_uri()
    use = "using App.Shared; class Use { Helper saved; DraftOnly draft; }"
    change_document(lib, 2, "namespace App.Shared; public class DraftOnly {}")
    next_diagnostics(lib, 2, set())
    change_document(uri, 3, use)
    next_diagnostics(uri, 3, {"[E1005] undefined type 'Helper'"})
    close_document(lib)
    next_diagnostics(lib, 2, set())
    change_document(uri, 4, use)
    next_diagnostics(uri, 4, {"[E1005] undefined type 'DraftOnly'"})

    # A source without a disk copy contributes declarations only while open.
    unsaved = (workspace / "unsaved.zan").as_uri()
    open_document(unsaved, 0, "namespace App.Shared; public class DraftOnly {}")
    next_diagnostics(unsaved, 0, set())
    change_document(uri, 5, use)
    next_diagnostics(uri, 5, set())
    close_document(unsaved)
    next_diagnostics(unsaved, 0, set())
    change_document(uri, 6, use)
    next_diagnostics(uri, 6, {"[E1005] undefined type 'DraftOnly'"})

    # Dependency edits republish diagnostics with the caller's unchanged version.
    service = (workspace / "service.zan").as_uri()
    caller = (workspace / "members.zan").as_uri()
    open_document(service, 1, "namespace App.Services; public class Service { public int Count; }")
    next_diagnostics(caller, 1, {MISSING_METHOD})
    close_document(service)
    next_diagnostics(caller, 1, set())

    # Change project ownership on disk with no editor notifications or version changes.
    ownership = (workspace / "parent/src/Ownership.zan").as_uri()
    open_document(ownership, 1, "using Shared; class Ownership { ParentToken parent; Helper helper; }")
    next_diagnostics(ownership, 1, set())
    manifest = workspace / "parent/src/zan.proj"
    manifest.write_text("name=SourceProject\n", encoding="utf-8")
    next_diagnostics(ownership, 1, {"[E1005] undefined type 'ParentToken'"})
    moved = workspace / "parent/src/lib/zan.proj"
    manifest.rename(moved)
    next_diagnostics(ownership, 1, {"[E1005] undefined type 'Helper'"})
    moved.unlink()
    next_diagnostics(ownership, 1, set())

    # Opening a source in a new directory discovers its manifest as well.
    new_project = workspace / "parent/new/src"
    new_project.mkdir(parents=True)
    new_manifest = new_project.parent / "zan.proj"
    new_manifest.write_text("name=NewProject\n", encoding="utf-8")
    new_uri = (new_project / "Use.zan").as_uri()
    open_document(new_uri, 1, "using Shared; class UseParent { ParentToken parent; }")
    next_diagnostics(new_uri, 1, {"[E1005] undefined type 'ParentToken'"})
    new_manifest.unlink()
    next_diagnostics(new_uri, 1, set())

    request("shutdown", 2)
    send("exit")
    code = proc.wait(timeout=max(0, deadline - time.monotonic()))
    if code:
        raise RuntimeError(f"zan-lsp exited with status {code}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--timeout", type=int, default=120, help="session deadline in seconds")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="lsp_diag_suppress_") as temp_dir:
        workspace = Path(temp_dir)
        (workspace / "service.zan").write_text(DISK_DEPENDENCY, encoding="utf-8")
        for name, text in PROJECT_SOURCES.items():
            path = workspace / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        for name, text, _ in CASES:
            (workspace / name).write_text(text, encoding="utf-8")

        with subprocess.Popen([args.exe], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE) as proc:
            messages = queue.Queue()
            reader = threading.Thread(target=read_messages, args=(proc.stdout, messages))
            reader.start()
            try:
                run_session(proc, workspace, messages, time.monotonic() + args.timeout)
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                print(f"FAIL {exc}", file=sys.stderr)
                return 1
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait()
                reader.join()

    print(f"lsp_diag_suppress: {len(CASES)}/{len(CASES)} documents matched")
    return 0


if __name__ == "__main__":
    sys.exit(main())

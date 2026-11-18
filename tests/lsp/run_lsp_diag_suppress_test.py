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


CASES = [
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

    request("initialize", 1, {"rootUri": workspace.as_uri(), "capabilities": {}})
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

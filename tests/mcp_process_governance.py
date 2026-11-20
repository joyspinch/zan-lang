"""Exercise the MCP process limits over the real stdio protocol."""
import argparse
import json
import os
from pathlib import Path
from queue import Empty, Queue
import subprocess
import tempfile
from threading import Thread
import time


def check(condition, detail):
    if not condition:
        raise AssertionError(detail)


def exercise(exe, root, workspace, zanc):
    server_args = [str(exe), "--stdio", str(workspace), "--sdk-root", str(root)]
    options = set()
    if zanc:
        server_args.extend(("--zanc", str(zanc)))
        help_result = subprocess.run([str(zanc), "--help"], capture_output=True, timeout=10)
        if help_result.returncode == 0:
            options = {fields[0] for stream in (help_result.stdout, help_result.stderr)
                       for line in stream.splitlines() if (fields := line.split())}
    with subprocess.Popen(server_args, stdin=subprocess.PIPE, stdout=subprocess.PIPE) as server:
        responses = Queue()

        def receive():
            for line in server.stdout:
                responses.put(line)
            responses.put(b"")

        reader = Thread(target=receive, daemon=True)
        reader.start()

        def send(message):
            server.stdin.write(json.dumps({"jsonrpc": "2.0", **message}).encode() + b"\n")
            server.stdin.flush()

        def response(request_id):
            try:
                line = responses.get(timeout=10)
            except Empty as exc:
                raise AssertionError(f"no response for request {request_id!r}") from exc
            check(line, f"stdio closed while waiting for {request_id!r}, exit={server.poll()}")
            reply = json.loads(line)
            check(reply.get("id") == request_id, reply)
            return reply["result"]

        def tool(request_id, name, **arguments):
            send({"id": request_id, "method": "tools/call", "params": {
                "name": name, "arguments": arguments,
            }})

        def command(request_id, text, **arguments):
            tool(request_id, "run_command", command=text, **arguments)

        def result(request_id):
            return json.loads(response(request_id)["content"][0]["text"])

        # The shell itself blocks on its stdin pipe, so cancellation must reach
        # a running direct child. The marker proves the job has already started.
        wait_for_input = "set /p value=" if os.name == "nt" else "read value"
        separator = " & " if os.name == "nt" else "; "
        try:
            send({"id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}})
            check(response(1)["protocolVersion"] == "2025-06-18", "initialize")

            command(2, 'echo "ready"' if os.name == "nt" else "echo ready")
            reply = result(2)
            expected = '"ready"' if os.name == "nt" else "ready"
            check(reply["ok"] and reply["status"] == "completed"
                  and reply["stdout"].splitlines() == [expected], reply)

            command(3, "echo " + "x" * 1024, maxOutputBytes=32)
            reply = result(3)
            check(reply["ok"] and reply["outputTruncated"] and reply["outputBytes"] == 32, reply)

            line = "x" * 6000
            (workspace / "burst.txt").write_text((line + "\n") * 3 + "final-marker\n",
                                                  encoding="ascii")
            command("burst", "type burst.txt" if os.name == "nt" else "cat burst.txt")
            reply = result("burst")
            check(reply["ok"] and not reply["outputTruncated"], reply)
            check(reply["stdout"].splitlines() == [line] * 3 + ["final-marker"],
                  f"incomplete burst: {reply['outputBytes']} bytes")

            command(4, wait_for_input, timeoutMs=200)
            reply = result(4)
            check(reply["status"] == "timeout" and reply["exitCode"] == -1, reply)

            command("running", "echo started > started.txt" + separator + wait_for_input)
            deadline = time.monotonic() + 10
            while not (workspace / "started.txt").is_file() and time.monotonic() < deadline:
                check(server.poll() is None, "server exited before the child started")
                time.sleep(0.01)
            check((workspace / "started.txt").is_file(), "child did not start")
            command("queued", "echo should-not-run > queued.txt")
            send({"method": "notifications/cancelled", "params": {"requestId": "queued"}})
            send({"method": "notifications/cancelled", "params": {"requestId": "running"}})
            for request_id in ("running", "queued"):
                reply = result(request_id)
                check(reply["status"] == "cancelled" and reply["exitCode"] == -1, reply)
            check(not (workspace / "queued.txt").exists(), "cancelled queued command ran")

            command(5, "echo rejected", cwd="..")
            reply = response(5)
            check(reply["isError"] and "escapes" in reply["content"][0]["text"], reply)
            send({"id": 6, "method": "ping"})
            check(response(6) == {}, "server failed to answer ping after cancellation")

            if zanc:
                tool("diagnostic", "zan_compile", filename="diagnostic.zan",
                     content='#error message with "quotes"\n')
                reply = result("diagnostic")
                check(not reply["ok"] and reply["exitCode"] != 0, reply)
                diagnostic = next((d for d in reply["diagnostics"] if d["severity"] == "error"), None)
                check(diagnostic and diagnostic["line"] == 1
                      and diagnostic["file"].endswith("diagnostic.zan"), reply)
                check('message with "quotes"' in diagnostic["message"], diagnostic)
                if b"--diagnostics-format=jsonl" in options:
                    check(diagnostic["format"] == "zan-diagnostic-v1"
                          and diagnostic["code"] == "E0002", diagnostic)

                source = "class P {\n    static int Pick(int value) {\n        return value + value;\n    }\n}\n"
                (workspace / "references.zan").write_text(source, encoding="ascii")
                send({"id": "catalog", "method": "tools/list"})
                catalog = response("catalog")["tools"]
                ref_tool = next((item for item in catalog if item["name"] == "zan_find_refs"), None)
                check(ref_tool and {"file", "line", "col"} <= ref_tool["inputSchema"]["properties"].keys(),
                      "semantic references missing from the tool catalog")
                tool("escaped-refs", "zan_find_refs", file="../references.zan", line=3, col=16)
                invalid = response("escaped-refs")
                check(invalid["isError"] and "escapes" in invalid["content"][0]["text"], invalid)
                tool("refs", "zan_find_refs", file="references.zan", line=3, col=16, limit=1)
                if b"--semantic-refs" in options:
                    refs = result("refs")
                    check(refs["ok"] and refs["format"] == "zan.semantic-refs.v1"
                          and refs["symbol"]["kind"] == "parameter"
                          and refs["symbol"]["name"] == "value", refs)
                    check(refs["refCount"] == 2 and refs["truncated"]
                          and len(refs["references"]) == 1, refs)
                    first = refs["references"][0]
                    check(first["line"] == 3 and first["col"] == 16
                          and first["length"] == 5 and first["file"].endswith("references.zan"), refs)
                else:
                    unavailable = response("refs")
                    check(unavailable["isError"] and "--semantic-refs" in unavailable["content"][0]["text"],
                          unavailable)

            server.stdin.close()
            check(server.wait(timeout=10) == 0, "server did not shut down cleanly")
        finally:
            if server.poll() is None:
                server.kill()
            server.wait()
            reader.join(timeout=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--zanc", type=Path)
    args = parser.parse_args()
    if args.zanc:
        args.exe.parent.mkdir(parents=True, exist_ok=True)
        source_dirs = ("tools/mcp_server", "tools/shared/gallery", "tools/shared/semantic_index")
        sources = [str(source.resolve()) for directory in source_dirs
                   for source in sorted((args.root / directory).glob("*.zan"))]
        subprocess.run([str(args.zanc.resolve()), *sources, "--stdlib-path",
                        str((args.root / "stdlib").resolve()), "-o", str(args.exe.resolve())],
                       cwd=args.root, check=True, timeout=120)
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mcp-process-", dir=args.work) as directory:
        exercise(args.exe.resolve(), args.root.resolve(), Path(directory).resolve(),
                 args.zanc.resolve() if args.zanc else None)
    print("MCP stdio process governance passed")


if __name__ == "__main__":
    main()

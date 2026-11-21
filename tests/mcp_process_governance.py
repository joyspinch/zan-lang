"""Exercise MCP process governance and the compiler diagnostics matrix."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
from http.client import HTTPConnection
import json
import os
from pathlib import Path
from queue import Empty, Queue
import shlex
import socket
import subprocess
import sys
import tempfile
from threading import Thread
import time


JSONL_OPTION = "--diagnostics-format=jsonl"


def check(condition, detail):
    if not condition:
        raise AssertionError(detail)


def lock_byte(stream):
    if os.name == "nt":
        import msvcrt
        msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
    else:
        import fcntl
        fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)


def wait_for_marker(marker, process):
    deadline = time.monotonic() + 5
    while not marker.is_file() and time.monotonic() < deadline:
        check(process.poll() is None, f"process exited before creating {marker.name}")
        time.sleep(0.01)
    check(marker.is_file(), f"missing marker {marker.name}")


def run_process_fixture(mode, name):
    marker = Path(name + ".ready")
    if mode == "holder":
        with Path(name + ".lock").open("r+b") as stream:
            lock_byte(stream)
            marker.touch()
            time.sleep(15)
    else:
        child = subprocess.Popen([*sys.orig_argv[:-3], "--process-fixture", "holder", name],
                                 start_new_session=mode.endswith("-session"))
        wait_for_marker(marker, child)
        if mode.startswith("waiting"):
            child.wait()


def assert_lock_released(path):
    deadline = time.monotonic() + 2
    while True:
        with path.open("r+b") as stream:
            try:
                lock_byte(stream)
            except OSError as exc:
                if time.monotonic() >= deadline:
                    raise AssertionError(f"descendant still holds {path.name}") from exc
            else:
                return
        time.sleep(0.01)


def exercise(exe, root, workspace, zanc, cgroup=None, *, diagnostics_format=None):
    server_args = [str(exe), "--stdio", str(workspace), "--sdk-root", str(root),
                   "--sandbox", "required" if cgroup else "process"]
    options = set()
    if zanc:
        server_args.extend(("--zanc", str(zanc)))
        help_result = subprocess.run([str(zanc), "--help"], capture_output=True, timeout=10)
        if help_result.returncode == 0:
            options = {fields[0] for stream in (help_result.stdout, help_result.stderr)
                       for line in stream.splitlines() if (fields := line.split())}
    workspace.mkdir(exist_ok=True)
    if diagnostics_format:
        check((JSONL_OPTION.encode() in options) == (diagnostics_format == "jsonl"),
              f"{zanc} does not match the expected {diagnostics_format} diagnostics mode")
        stdlib = workspace / "stdlib"
        stdlib.mkdir()
        server_args.extend(("--stdlib", str(stdlib)))
    temp = workspace / "process temp"
    temp.mkdir()
    env = {**os.environ, "TMPDIR": str(temp), "TEMP": str(temp), "TMP": str(temp)}
    if cgroup:
        env["MCP_SANDBOX_CGROUP"] = str(cgroup)
    with subprocess.Popen(server_args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env) as server:
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
            reply = response(request_id)
            check(not reply.get("isError", False), reply)
            return json.loads(reply["content"][0]["text"])

        def stop():
            server.stdin.close()
            check(server.wait(timeout=10) == 0, "server did not shut down cleanly")

        # The shell itself blocks on its stdin pipe, so cancellation must reach
        # a running direct child. The marker proves the job has already started.
        wait_for_input = "set /p value=" if os.name == "nt" else "read value"
        separator = " & " if os.name == "nt" else "; "
        try:
            send({"id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}})
            check(response(1)["protocolVersion"] == "2025-06-18", "initialize")
            if diagnostics_format:
                exercise_diagnostics(tool, result, workspace, zanc, stdlib, diagnostics_format)
                stop()
                return

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

            for expected_status in ("completed", "cancelled", "timeout"):
                name = "tree-" + expected_status
                lock_path = workspace / (name + ".lock")
                lock_path.write_bytes(b"0")
                mode = "exiting" if expected_status == "completed" else "waiting"
                if cgroup:
                    mode += "-session"
                fixture = Path(__file__).resolve()
                script = ["-c", fixture.read_text(encoding="utf-8")] if cgroup else [str(fixture)]
                args = [sys.executable, "-I", "-B", *script, "--process-fixture", mode, name]
                if os.name == "nt":
                    # Force quoting for path arguments even if they have no spaces.
                    text = f'"{args[0]}" -I -B "{args[3]}" --process-fixture {mode} {name}'
                else:
                    text = shlex.join(args)
                command(name, text, timeoutMs=3000 if expected_status == "timeout" else 8000)
                wait_for_marker(workspace / (name + ".ready"), server)
                if cgroup and expected_status != "completed":
                    groups = list(cgroup.glob("zan-mcp-*"))
                    check(len(groups) == 1, groups)
                    for control, expected in {
                        "cpu.max": "100000 100000", "memory.max": "1073741824",
                        "memory.swap.max": "0", "pids.max": "64",
                    }.items():
                        check((groups[0] / control).read_text().strip() == expected, control)
                    check(len((groups[0] / "cgroup.procs").read_text().split()) >= 2,
                          "descendants did not inherit the cgroup")
                if expected_status == "cancelled":
                    send({"method": "notifications/cancelled", "params": {"requestId": name}})
                reply = result(name)
                check(reply["status"] == expected_status, reply)
                check(reply["exitCode"] == (0 if expected_status == "completed" else -1), reply)
                assert_lock_released(lock_path)
                if cgroup:
                    check(not list(cgroup.glob("zan-mcp-*")), "job cgroup was left behind")

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
                if JSONL_OPTION.encode() in options:
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

                    project = workspace / "reference project"
                    for directory in (project / "src", project / "build", project / "child"):
                        directory.mkdir(parents=True)
                    (project / "zan.proj").write_text('{"entry":"src/Query.zan"}\n', encoding="ascii")
                    (project / "src/Query.zan").write_text(
                        "class Query {\n    static int Pick(int value) {\n"
                        "        return Helper.Keep(value) + value;\n    }\n}\n", encoding="ascii")
                    (project / "src/Helper implementation.zan").write_text(
                        "class Helper { public static int Keep(int input) { return input; } }\n",
                        encoding="ascii")
                    (project / "build/Generated.zan").write_text("#error generated\n", encoding="ascii")
                    (project / "child/zan.proj").write_text("{}\n", encoding="ascii")
                    (project / "child/Other.zan").write_text("#error nested\n", encoding="ascii")
                    if os.name != "nt":
                        (project / "src/Linked.zan").symlink_to(project / "child/Other.zan")
                        (project / "linked-dir").symlink_to(project / "build", target_is_directory=True)
                    tool("project-refs", "zan_find_refs", file="reference project/src/Query.zan",
                         line=3, col=28)
                    refs = result("project-refs")
                    check(refs["ok"] and refs["symbol"]["name"] == "value"
                          and refs["refCount"] == 2, refs)
                    check([ref["col"] for ref in refs["references"]] == [28, 37], refs)
                    check(not list(temp.glob("zan-mcp-refs-*.rsp")), "reference response file was left behind")
                else:
                    unavailable = response("refs")
                    check(unavailable["isError"] and "--semantic-refs" in unavailable["content"][0]["text"],
                          unavailable)

            stop()
        finally:
            if server.poll() is None:
                server.kill()
            server.wait()
            reader.join(timeout=2)


def exercise_diagnostics(tool, result, workspace, zanc, stdlib, diagnostics_format):
    # The shared failure needs no stdlib sources and should precede linking.
    source = workspace / "Compiler diagnostic.zan"
    source.write_bytes(b"class Program {\n    static void Main() {\n"
                       b"        char value = '\\q';\n    }\n}\n")
    expected_message = "invalid escape sequence '\\q'"
    jsonl = diagnostics_format == "jsonl"

    tool("diagnostics", "zan_build_project", entry=source.name)
    reply = result("diagnostics")
    check(reply["ok"] is False and reply["status"] == "completed"
          and reply["exitCode"] == 1 and reply["outputTruncated"] is False, reply)
    check(len(reply["diagnostics"]) == 1, reply)
    diag = reply["diagnostics"][0]
    check(diag["severity"] == "error", diag)
    # gen1 normalizes slash direction and path casing on Windows.
    check(Path(diag["file"]) == source and diag["line"] == 3, diag)
    check(diag["location"] == f"{diag['file']}:3:{diag['col']}", diag)
    if jsonl:
        check(diag["format"] == "zan-diagnostic-v1" and diag["code"] == "E0017"
              and diag["message"] == expected_message, diag)
    else:
        check("format" not in diag and "code" not in diag
              and diag["message"] == f"[E0017] {expected_message}", diag)
    check(f"{diag['location']}: error: [E0017] {expected_message}" in reply["raw"], reply)
    check(not any((workspace / "_scratch" / "mcp_build").iterdir()),
          "MCP left compiler request files behind")

    # MCP removes the request directory even if the compiler wrote an output.
    # Re-run the same failed compilation with a persistent output path to check for that.
    output = workspace / ("out.exe" if os.name == "nt" else "out")
    args = [zanc, source, "--stdlib-path", stdlib]
    if jsonl:
        args.append(JSONL_OPTION)
    completed = subprocess.run([*args, "-o", output], cwd=workspace,
                               capture_output=True, timeout=30)
    check(completed.returncode == 1, completed)
    check(not output.exists(), f"failed compilation created {output}")
    print(f"MCP {diagnostics_format} diagnostics passed for {zanc}")


def exercise_http(exe, root, workspace, workers=1, zanc=None):
    workspace.mkdir(exist_ok=True)
    host = "127.0.0.1"
    with socket.socket() as listener, socket.socket() as control:
        listener.bind((host, 0))
        control.bind((host, 0))
        port = listener.getsockname()[1]
        control_port = control.getsockname()[1]

    def exchange(method, payload=None, session="", path="/mcp", connection=None, auth=True):
        created = connection is None
        if created:
            connection = HTTPConnection(host, port, timeout=12)
        headers = {"Content-Type": "application/json", "MCP-Protocol-Version": "2025-06-18"}
        if auth:
            headers["Authorization"] = "Bearer test-mcp-governance"
        if session:
            headers["Mcp-Session-Id"] = session
        try:
            body = None if payload is None else json.dumps({"jsonrpc": "2.0", **payload})
            connection.request(method, path, body, headers)
            response = connection.getresponse()
            return response.status, response.getheader("Mcp-Session-Id", ""), response.read()
        finally:
            if created:
                connection.close()

    def initialize(connection):
        status, session, body = exchange("POST", {"id": 1, "method": "initialize",
                                                   "params": {"protocolVersion": "2025-06-18"}},
                                         connection=connection)
        check(status == 200 and session, (status, body))
        check(json.loads(body)["result"]["protocolVersion"] == "2025-06-18", body)
        return session

    def run(session, request_id, marker, connection=None):
        hold = "set /p value=" if os.name == "nt" else "read value"
        separator = " & " if os.name == "nt" else "; "
        return exchange("POST", {"id": request_id, "method": "tools/call", "params": {
            "name": "run_command", "arguments": {
                "command": f"echo started > {marker}" + separator + hold, "timeoutMs": 8000,
            },
        }}, session, connection=connection)

    def cancel(session, request_id, connection):
        status, _, body = exchange("POST", {"method": "notifications/cancelled",
                                            "params": {"requestId": request_id}}, session,
                                   connection=connection)
        check(status == 202 and not body, (status, body))

    def expect_cancelled(call, request_id):
        status, _, body = call.result(timeout=10)
        check(status == 200, (status, body))
        reply = json.loads(body)
        check(reply["id"] == request_id, reply)
        result = json.loads(reply["result"]["content"][0]["text"])
        check(result["status"] == "cancelled" and result["exitCode"] == -1, result)

    def tool(session, connection, name, **arguments):
        status, _, body = exchange("POST", {"id": f"{name}-{id(connection)}", "method": "tools/call", "params": {
            "name": name, "arguments": arguments,
        }}, session, connection=connection)
        check(status == 200, (status, body))
        return json.loads(body)["result"]["content"][0]["text"]

    args = [str(exe), str(workspace), "--sdk-root", str(root), "--host", host,
            "--port", str(port), "--workers", str(workers), "--token-env", "ZAN_TEST_MCP_TOKEN",
            "--name", "governance", "--max-read", "1234567", "--sandbox", "process"]
    if zanc:
        args.extend(("--zanc", str(zanc)))
        (workspace / "src").mkdir()
        (workspace / "src/main.zan").write_text(
            "class Program { static int Main() { return 0; } }\n", encoding="ascii")
        (workspace / ".zanmap").mkdir()
        (workspace / ".zanmap/routes.approx.json").write_text('["approximate"]', encoding="ascii")
    env = {**os.environ, "ZAN_TEST_MCP_TOKEN": "test-mcp-governance",
           "ZAN_CTL_PORT": str(control_port), "TMPDIR": str(workspace), "TEMP": str(workspace),
           "TMP": str(workspace)}
    with subprocess.Popen(args, cwd=workspace, env=env, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL) as server, ExitStack() as connections:
        try:
            deadline = time.monotonic() + 10
            while True:
                check(server.poll() is None, f"HTTP server exited: {server.returncode}")
                try:
                    status, _, body = exchange("GET", path="/health")
                    check(status == 200, (status, body))
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.05)

            pinned = {}
            while len(pinned) < workers:
                check(time.monotonic() < deadline, f"found workers {list(pinned)}")
                conn = HTTPConnection(host, port, timeout=12)
                connections.callback(conn.close)
                status, _, body = exchange("GET", path="/health", connection=conn)
                check(status == 200, (status, body))
                health = json.loads(body)
                check(health["server"] == "governance" and health["sandbox"] == "process"
                      and health["maxReadBytes"] == 1234567
                      and Path(health["workspace"]) == workspace, health)
                worker = health["worker"]
                if worker in pinned:
                    conn.close()
                    time.sleep(0.01)
                    continue
                status, _, body = exchange("GET", path="/health", connection=conn, auth=False)
                check(status == 401, (worker, status, body))
                pinned[worker] = conn
            run_conn = next(iter(pinned.values()))
            control_conn = pinned[max(pinned)] if workers == 1 else next(reversed(pinned.values()))
            if control_conn == run_conn:
                control_conn = HTTPConnection(host, port, timeout=12)
                connections.callback(control_conn.close)

            first, second = initialize(control_conn), initialize(run_conn)
            check(first != second, "server reused the session ID")
            with ThreadPoolExecutor(max_workers=3) as pool:
                number = pool.submit(run, first, 7, "http-number.ready", run_conn)
                text = pool.submit(run, first, "7", "http-text.ready")
                other = pool.submit(run, second, 7, "http-other.ready")
                for marker in ("http-number.ready", "http-text.ready", "http-other.ready"):
                    wait_for_marker(workspace / marker, server)
                cancel(first, 7, control_conn)
                expect_cancelled(number, 7)
                check(not text.done() and not other.done(), "cancellation crossed request or session scope")
                cancel(first, "7", control_conn)
                expect_cancelled(text, "7")
                status, _, body = exchange("DELETE", session=second)
                check(status == 204 and not body, (status, body))
                expect_cancelled(other, 7)

            status, _, body = exchange("POST", {"id": 2, "method": "ping"}, second)
            check(status == 404, (status, body))
            for session in (first, ""):
                status, _, body = exchange("POST", {"id": 2, "method": "ping"}, session)
                check(status == 200 and json.loads(body)["result"] == {}, (status, body))
            if workers > 1:
                with ThreadPoolExecutor(max_workers=1) as pool:
                    delete_session = initialize(control_conn)
                    call = pool.submit(run, delete_session, "delete", "http-deleted.ready", run_conn)
                    wait_for_marker(workspace / "http-deleted.ready", server)
                    status, _, body = exchange("DELETE", session=delete_session, connection=control_conn)
                    check(status == 204 and not body, (status, body))
                    expect_cancelled(call, "delete")
            if zanc:
                with ThreadPoolExecutor(max_workers=2) as pool:
                    calls = [pool.submit(tool, first, conn, "zan_compile", filename="same.zan",
                                         content=f"#error {marker}\n")
                             for conn, marker in ((run_conn, "first-snippet"),
                                                  (control_conn, "second-snippet"))]
                    files = []
                    for call, marker in zip(calls, ("first-snippet", "second-snippet")):
                        result = json.loads(call.result())
                        check(not result["ok"] and marker in result["raw"], result)
                        files.append(result["diagnostics"][0]["file"])
                    check(len(set(files)) == 2, files)

                    build = pool.submit(tool, first, run_conn, "zan_build_project")
                    other_build = pool.submit(tool, first, control_conn, "zan_build_project")
                    for call in (build, other_build):
                        result = json.loads(call.result())
                        check(result["ok"], result)

                    def routes():
                        status, _, body = exchange("POST", {"id": "routes", "method": "resources/read",
                            "params": {"uri": "repo://routes"}}, first, connection=control_conn)
                        check(status == 200, (status, body))
                        return json.loads(json.loads(body)["result"]["contents"][0]["text"])

                    check(routes() == ["approximate"], "approximate route fallback was skipped")
                    refresh = json.loads(tool(first, run_conn, "zan_refresh_index"))
                    check(refresh["ok"] and refresh["sources"] == 1, refresh)
                    check(routes() == [], "worker kept the approximate route file after refresh")

                    if os.name == "nt":
                        calls = [pool.submit(tool, first, conn, "zan_gui_snapshot", file="src/main.zan")
                                 for conn in (run_conn, control_conn)]
                        outputs = []
                        for call in calls:
                            result = call.result()
                            check("exit code 0" in result, result)
                            output = result.split("Output Dir: ", 1)[1].splitlines()[0]
                            check((workspace / output / "script.txt").is_file(), result)
                            outputs.append(output)
                        check(len(set(outputs)) == 2, outputs)
                for tool_dir in ("mcp_compile", "mcp_build", "mcp_index"):
                    check(not list((workspace / "_scratch" / tool_dir).iterdir()),
                          f"request files left in {tool_dir}")
        finally:
            try:
                connections.close()
                if server.poll() is None and workers > 1:
                    with socket.create_connection((host, control_port), timeout=5) as conn:
                        conn.sendall(b"PING\n")
                        reply = conn.makefile("rb").readline().split()
                    check(reply[:2] == [b"PONG", str(server.pid).encode()], reply)
                    with socket.create_connection((host, control_port), timeout=5) as conn:
                        conn.sendall(b"STOP\n")
                        reply = conn.makefile("rb").readline()
                    check(reply.startswith(b"stopping "), reply)
                    check(server.wait(timeout=15) == 0, "worker master did not stop")
            finally:
                if server.poll() is None:
                    server.kill()


def exercise_sandbox(exe, root, workspace, cgroup=None):
    command = "echo started > sandbox-marker"
    request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {
        "name": "run_command", "arguments": {"command": command},
    }}
    args = [str(exe), "--stdio", str(workspace), "--sdk-root", str(root)]
    env = {**os.environ, "MCP_SANDBOX": "required", "MCP_SANDBOX_CGROUP": ""}
    completed = subprocess.run(args, input=json.dumps(request).encode() + b"\n",
                               capture_output=True, env=env, timeout=15, check=True)
    reply = json.loads(completed.stdout)
    check(reply["id"] == 1, reply)
    result = json.loads(reply["result"]["content"][0]["text"])
    check(result["status"] == "sandbox-unavailable" and not result["ok"], result)
    check(not (workspace / "sandbox-marker").exists(), "unavailable sandbox launched the command")

    if not cgroup:
        return

    import fcntl

    probe = '''
import errno, os, socket, sys
from pathlib import Path

def denied(expected, operation, *args):
    try:
        resource = operation(*args)
    except OSError as error:
        assert error.errno == expected, (operation, error)
    else:
        raise AssertionError((operation, resource))

outside = sys.argv[1]
denied(errno.EACCES, os.open, outside, os.O_RDONLY)
denied(errno.EACCES, os.open, outside, os.O_WRONLY)
denied(errno.EPERM, socket.socket)
denied(errno.EBADF, os.fstat, int(sys.argv[2]))
assert "MCP_SANDBOX_TEST_SECRET" not in os.environ
Path("sandbox-marker").write_text("started", encoding="ascii")
'''
    with tempfile.TemporaryDirectory(prefix="zan-mcp-denied-", dir="/tmp") as directory:
        outside = Path(directory) / "outside"
        outside.write_text("outside", encoding="ascii")
        with outside.open("r+b") as stream, socket.socket():
            stream.write(b"outside")
            stream.flush()
            fd = fcntl.fcntl(stream.fileno(), fcntl.F_DUPFD, 128)
            try:
                request["params"]["arguments"]["command"] = shlex.join(
                    [sys.executable, "-I", "-S", "-B", "-c", probe, str(outside), str(fd)])
                completed = subprocess.run(args, input=json.dumps(request).encode() + b"\n",
                                           capture_output=True, timeout=15, check=True,
                                           pass_fds=(fd,), env={
                                               **env, "MCP_SANDBOX_CGROUP": cgroup,
                                               "MCP_SANDBOX_TEST_SECRET": "host-only",
                                           })
            finally:
                os.close(fd)
        result = json.loads(json.loads(completed.stdout)["result"]["content"][0]["text"])
        check(result["ok"] and result["status"] == "completed", result)
        check(outside.read_text(encoding="ascii") == "outside", "sandbox changed the outside file")
        check((workspace / "sandbox-marker").read_text() == "started", result)
    print("MCP strict sandbox file, network, environment and descriptor checks passed")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="path to the MCP server executable")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--zanc", type=Path, help="compiler passed to the MCP server")
    parser.add_argument("--host-zanc", type=Path, help="C compiler used to build the MCP server")
    parser.add_argument("--diagnostics", choices=("jsonl", "text"),
                        help="run one diagnostics matrix row, using a prebuilt --exe unless --host-zanc is given")
    args = parser.parse_args()
    if args.diagnostics and not args.zanc:
        parser.error("--diagnostics requires --zanc")
    host_zanc = args.host_zanc
    if not args.diagnostics and host_zanc is None:
        host_zanc = args.zanc  # Preserve the CTest invocation.
    if host_zanc:
        args.exe.parent.mkdir(parents=True, exist_ok=True)
        source_dirs = ("tools/mcp_server", "tools/shared/gallery", "tools/shared/semantic_index")
        sources = [str(source.resolve()) for directory in source_dirs
                   for source in sorted((args.root / directory).glob("*.zan"))]
        subprocess.run([str(host_zanc.resolve()), *sources, "--stdlib-path",
                        str((args.root / "stdlib").resolve()), "-o", str(args.exe.resolve())],
                       cwd=args.root, check=True, timeout=120)
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mcp-process-", dir=args.work) as directory:
        workspace = Path(directory).resolve()
        exercise(args.exe.resolve(), args.root.resolve(), workspace,
                 args.zanc.resolve() if args.zanc else None, diagnostics_format=args.diagnostics)
        if args.diagnostics:
            return
        exercise_http(args.exe.resolve(), args.root.resolve(), workspace)
        exercise_http(args.exe.resolve(), args.root.resolve(), workspace / "workers", workers=2,
                      zanc=args.zanc.resolve() if args.zanc else None)
        cgroup = os.environ.get("ZAN_TEST_MCP_SANDBOX_CGROUP") if sys.platform == "linux" else None
        if cgroup:
            # WSL's Windows mounts do not support all Landlock write checks.
            with tempfile.TemporaryDirectory(prefix="zan-mcp-strict-", dir="/tmp") as directory:
                strict = Path(directory)
                exercise_sandbox(args.exe.resolve(), args.root.resolve(), strict, cgroup)
                exercise(args.exe.resolve(), args.root.resolve(), strict / "commands",
                         args.zanc.resolve() if args.zanc else None, Path(cgroup))
            print("MCP strict sandbox limits and descendant cleanup passed")
        else:
            exercise_sandbox(args.exe.resolve(), args.root.resolve(), workspace)
    print("MCP stdio and HTTP process governance passed")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--process-fixture":
        run_process_fixture(sys.argv[2], sys.argv[3])
    else:
        main()

"""用 TCP 假服务器验证真实 LspSession，客户端由调用者独立编译。"""
import argparse
import ctypes
import json
import os
import secrets
import socket
import subprocess
import threading
import tempfile
import time
from pathlib import Path


def frame(message):
    body = json.dumps(message, ensure_ascii=False).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def utf16_length(text):
    return len(text.encode("utf-16-le")) // 2


def serve(listener, errors):
    try:
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(10)
            stream = connection.makefile("rb")
            opens = 0
            versions = []
            current = ""
            timed_out = None
            superseded = None
            cancelled = False
            completions = 0
            semantic_counts = {"textDocument/signatureHelp": 0, "textDocument/hover": 0}
            dependency = None
            held_hover = None
            cache_columns = []
            prefix = "/*中𐐀*/ obj."
            while True:
                length = None
                while True:
                    header = stream.readline()
                    assert header, "客户端提前关闭连接"
                    if header == b"\r\n":
                        break
                    if header.lower().startswith(b"content-length:"):
                        length = int(header.split(b":", 1)[1])
                assert length is not None
                message = json.loads(stream.read(length))
                method = message["method"]
                params = message.get("params") or {}
                if method == "textDocument/didOpen":
                    document = params["textDocument"]
                    if document["uri"].endswith("/dependency.zan"):
                        assert dependency is None and document["version"] == 1
                        assert document["text"] == "class Dependency { int value; }"
                        dependency = document
                        continue
                    assert document["uri"].endswith("/unsaved.zan"), document
                    opens += 1
                    assert opens == 1, "相同文档重复 didOpen"
                    assert document["version"] == 1
                    current = document["text"]
                    assert current == prefix + "WrTail;"
                    continue
                if method == "textDocument/didChange":
                    document = params["textDocument"]
                    changes = params["contentChanges"]
                    assert len(changes) == 1 and "range" not in changes[0]
                    if document["uri"].endswith("/dependency.zan"):
                        assert dependency is not None and document["version"] == dependency["version"] + 1 == 2
                        assert changes[0]["text"] == "class Dependency { string value; }"
                        dependency = {**document, "text": changes[0]["text"]}
                        continue
                    assert document["uri"].endswith("/unsaved.zan"), document
                    version = document["version"]
                    versions.append(version)
                    assert version == len(versions) + 1
                    current = changes[0]["text"]
                    continue
                if method == "initialized":
                    continue
                if method == "$/cancelRequest":
                    assert params["id"] == superseded, message
                    cancelled = True
                    continue
                if method == "exit":
                    assert opens == 1 and versions == [2, 3, 4, 5, 6] and cancelled
                    assert semantic_counts == {"textDocument/signatureHelp": 3, "textDocument/hover": 3}, semantic_counts
                    assert dependency["version"] == 2 and cache_columns == [*range(66), 1], cache_columns
                    return
                request_id = message["id"]
                if method == "initialize":
                    result = {"capabilities": {"textDocumentSync": 1}}
                elif method == "textDocument/completion":
                    completions += 1
                    assert current == prefix + ("WrTail;" if completions == 1 else "WrTail; "), "补全没有发送未保存文本"
                    assert params["position"] == {"line": 0, "character": utf16_length(prefix + "Wr")}
                    if completions == 1:
                        superseded = request_id
                        continue
                    assert completions == 2 and cancelled, "过期请求没有取消"
                    connection.sendall(frame({"jsonrpc": "2.0", "id": superseded, "result": [{"label": "PoisonStale"}]}))
                    time.sleep(0.35)
                    result = {"isIncomplete": False, "items": [{
                        "label": "WriteLine", "kind": 2, "detail": "server signature",
                        "documentation": "server docs", "insertText": "ignored",
                        "textEdit": {"range": {
                            "start": {"line": 0, "character": utf16_length(prefix)},
                            "end": {"line": 0, "character": utf16_length(prefix + "WrTail")}},
                            "newText": "WriteLine()"}}]}
                elif method in semantic_counts:
                    assert params["textDocument"]["uri"].endswith("/unsaved.zan")
                    if current.startswith("abcdefghijklmnopqrstuvwxyz"):
                        assert method == "textDocument/hover" and params["position"]["line"] == 0
                        column = params["position"]["character"]
                        cache_columns.append(column)
                        if column == 0:
                            assert held_hover is None
                            held_hover = request_id
                            continue
                        if column % 3 == 2:
                            connection.sendall(frame({"jsonrpc": "2.0", "id": request_id,
                                                      "error": {"code": -32603, "message": "cache error"}}))
                            continue
                        result = {"contents": "" if column % 3 == 1 else f"cache-{column}"}
                    else:
                        semantic_counts[method] += 1
                        phase = semantic_counts[method]
                        assert 1 <= phase <= 3, "签名/悬停重复请求没有去重"
                        assert current == prefix + ("WriteLine(); " if phase == 1 else "WriteLine(); edited"), "语义请求未同步当前文本"
                        assert params["position"] == {"line": 0, "character": utf16_length(prefix + "WriteLine()")}
                        assert dependency is not None and dependency["version"] == (1 if phase < 3 else 2), "跨文档失效次序错误"
                        # 延迟响应验证 provider 返回时没有同步等待。
                        time.sleep(0.35)
                        if method == "textDocument/signatureHelp":
                            labels = ["void WriteLine(string value)", "void WriteLine(int edited)", "void WriteLine(string dependency)"]
                            result = {"signatures": [{"label": labels[phase - 1], "parameters": []}],
                                      "activeSignature": 0, "activeParameter": 0}
                        else:
                            contents = [["hover text", {"language": "zan", "value": "hover details"}],
                                        "edited hover", "dependency hover"]
                            result = {"contents": contents[phase - 1]}
                elif method == "probeRelease":
                    assert held_hover is not None and cache_columns == [*range(66), 1], cache_columns
                    connection.sendall(frame({"jsonrpc": "2.0", "id": held_hover, "result": {"contents": "cache-0"}}))
                    result = "released"
                elif method == "probeTimeout":
                    assert current == "last unsaved buffer"
                    timed_out = request_id
                    continue
                elif method == "probeNext":
                    connection.sendall(frame({"jsonrpc": "2.0", "id": timed_out, "result": "late"}))
                    result = "fresh"
                elif method == "shutdown":
                    result = None
                else:
                    raise AssertionError(f"未知方法 {method}")
                # 数字前缀碰撞、嵌套 id 的通知以及有空格的正确响应。
                data = (frame({"jsonrpc": "2.0", "id": request_id * 10, "result": "poison"})
                        + frame({"jsonrpc": "2.0", "method": "test/notice",
                                 "params": {"id": request_id, "result": "poison"}})
                        + frame({"jsonrpc": "2.0", "id": request_id, "result": result}))
                if method == "textDocument/completion":
                    for begin in range(0, len(data), 7):
                        connection.sendall(data[begin:begin + 7])
                        time.sleep(0.002)
                else:
                    connection.sendall(data)
    except Exception as error:
        errors.append(error)


def project_probe(server_path):
    with tempfile.TemporaryDirectory(prefix="zan lsp 中 ") as directory:
        workspace = Path(directory)
        dependency = workspace / "Other%20File.zan"
        dependency.write_text('class ProjectOwner { public string Visible; private int Hidden; void Pick(int correct) {} void LeakMethod(int LeakParam) { int LeakLocal = 1; } }\n', encoding="utf-8")
        uri = (workspace / "Unsaved.zan").as_uri()
        source = 'class MainOwner { void Pick(string wrong) {} void Run(ProjectOwner a) { string Visible = "x"; a.Visible.Length; a.Pick(1); Leak; } }\n'
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
        server = subprocess.Popen([str(server_path.resolve()), "--port", str(port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        connection = None
        try:
            for _ in range(100):
                try:
                    connection = socket.create_connection(("127.0.0.1", port), timeout=0.1)
                    break
                except OSError:
                    time.sleep(0.02)
            assert connection is not None, "项目探针连接失败"
            # 冷启动的 zan-lsp 在应答 initialize 前要同步索引工具链 stdlib，
            # 实测 ~22s，构建竞争下更慢；太短的读超时会把慢应答误判成挂死。
            connection.settimeout(90)
            stream = connection.makefile("rb")
            serial = 0
            def request(method, params):
                nonlocal serial
                serial += 1
                connection.sendall(frame({"jsonrpc": "2.0", "id": serial, "method": method, "params": params}))
                while True:
                    length = None
                    while True:
                        header = stream.readline()
                        assert header, "项目服务器提前退出"
                        if header == b"\r\n":
                            break
                        if header.lower().startswith(b"content-length:"):
                            length = int(header.split(b":", 1)[1])
                    message = json.loads(stream.read(length))
                    if message.get("id") == serial:
                        return message
            def params(marker):
                offset = source.index(marker) + len(marker)
                return {"textDocument": {"uri": uri}, "position": {"line": 0, "character": utf16_length(source[:offset])}}
            request("initialize", {"rootUri": workspace.as_uri(), "capabilities": {}})
            connection.sendall(frame({"jsonrpc": "2.0", "method": "initialized", "params": {}}))
            connection.sendall(frame({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": uri, "languageId": "zan", "version": 1, "text": source}}}))
            completion = request("textDocument/completion", params("a."))["result"]
            labels = {item["label"] for item in completion}
            assert "Visible" in labels and "Hidden" not in labels, labels
            leaked = request("textDocument/completion", params("Leak"))["result"]
            assert not {"LeakLocal", "LeakParam", "LeakMethod"} & {item["label"] for item in leaked}, leaked
            definition = request("textDocument/definition", params("a.Visible"))["result"]
            assert definition and definition["uri"] == dependency.as_uri(), definition
            signature = request("textDocument/signatureHelp", params("a.Pick("))["result"]
            assert signature and "int correct" in signature["signatures"][0]["label"], signature
            rename_params = params("a.Visible")
            rename_params["newName"] = "RenamedVisible"
            rename = request("textDocument/rename", rename_params)
            if "error" not in rename and rename.get("result"):
                for file_uri, edits in rename["result"].get("changes", {}).items():
                    assert file_uri in {uri, dependency.as_uri()}, rename
                    if file_uri == uri:
                        assert len(edits) == 1 and edits[0]["range"]["start"]["character"] == source.index("a.Visible") + 2, rename
            request("shutdown", None)
            connection.sendall(frame({"jsonrpc": "2.0", "method": "exit"}))
            connection.close(); connection = None
            server.communicate(timeout=5)
            assert server.returncode == 0
            print("PASS project URI spaces Unicode percent, owner completion definition signature, no foreign locals or rename edits")
        finally:
            if connection:
                connection.close()
            if server.poll() is None:
                server.kill()
            server.communicate()


def executable_resolution_probe(client):
    # Empty marker files exercise discovery only; no compiler/server/source copies.
    output_dir = client.resolve().parent / "test-out"
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lsp_paths_", dir=output_dir) as temporary:
        root = Path(temporary)
        directories = [root / "SDK with spaces 中%" / "toolchain",
                       root / "SDK with spaces 中%", root / "isolated candidate"]
        for directory in directories:
            directory.mkdir(parents=True, exist_ok=True)

        def check(override, suffix, expected, detail="", code=0):
            result = subprocess.run([str(client.resolve()), "resolve", str(override),
                                     *[p.as_posix() for p in directories], suffix,
                                     str(expected), detail, str(code)],
                                    capture_output=True, text=True, errors="replace", timeout=10)
            assert result.returncode == 0 and "PASS LspSession executable resolution" in result.stdout, result.stdout + result.stderr

        for suffix in (".exe", ""):
            candidates = [directory / ("zan-lsp" + suffix) for directory in directories]
            for mask in range(8):
                for index, candidate in enumerate(candidates):
                    if mask & (1 << index):
                        candidate.touch()
                    else:
                        candidate.unlink(missing_ok=True)
                present = [candidate.as_posix() for index, candidate in enumerate(candidates)
                           if mask & (1 << index)]
                detail = " / ".join(p.as_posix() for p in candidates) if not present else ""
                check("", suffix, present[0] if present else "", detail, 0 if present else 1)
            explicit = root / ("explicit server" + suffix)
            explicit.touch()
            check(explicit.as_posix(), suffix, explicit.as_posix())
            missing = (root / ("missing explicit" + suffix)).as_posix()
            check(missing, suffix, "", missing, 1)
            for candidate in candidates:
                candidate.unlink(missing_ok=True)
    print("PASS executable resolution: 20 cases, strict override, all candidate priorities, missing paths, spaces Unicode percent and both suffixes")


def launcher_identity(pid):
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.OpenProcess.restype = ctypes.c_void_p
        handle = kernel.OpenProcess(0x100001, False, pid)
        assert handle, f"cannot retain owned launcher {pid}"
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        return kernel, handle
    return subprocess.check_output(["ps", "-p", str(pid), "-o", "lstart=", "-o", "comm="], text=True).strip()


def reap_launcher(pid, identity):
    if os.name == "nt":
        kernel, handle = identity
        try:
            if kernel.WaitForSingleObject(handle, 5000) == 258:
                assert kernel.TerminateProcess(handle, 1), f"launcher termination failed: {pid}"
            assert kernel.WaitForSingleObject(handle, 5000) == 0, f"launcher remains: {pid}"
        finally:
            kernel.CloseHandle(handle)
    else:
        for _ in range(100):
            current = subprocess.run(["ps", "-p", str(pid), "-o", "lstart=", "-o", "comm="], capture_output=True, text=True, timeout=5).stdout.strip()
            if not current or current != identity:
                return
            time.sleep(0.05)
        raise AssertionError(f"owned launcher did not exit: {pid}")


LSP_STARTUP_TIMEOUT = 90
LSP_CLIENT_TIMEOUT = 120


def natural_discovery_probe(client, server, launcher, mode="normal"):
    output_dir = client.resolve().parent / "test-out"
    with tempfile.TemporaryDirectory(prefix="lsp_start_", dir=output_dir) as workspace:
        environment = os.environ.copy()
        environment.pop("ZAN_IDE_LSP_EXE", None)
        root = Path(workspace)
        toolchain, candidate = root / "missing toolchain", root / "isolated candidate"
        toolchain.mkdir(); candidate.mkdir()
        errors, servers, launchers = [], [], []
        stop = threading.Event()
        registered = threading.Event()
        token = secrets.token_hex(24)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(4); listener.settimeout(0.1)
            environment["ZAN_LSP_TEST_OWNER_PORT"] = str(listener.getsockname()[1])
            environment["ZAN_LSP_TEST_OWNER_TOKEN"] = token
            environment["ZAN_LSP_TEST_FAULT"] = mode

            def own_servers():
                try:
                    while not stop.is_set():
                        try:
                            connection, _ = listener.accept()
                        except socket.timeout:
                            continue
                        with connection:
                            connection.settimeout(5)
                            request = connection.makefile("rb").readline(200).decode("ascii").split()
                            assert len(request) == 3 and request[0] == token, request
                            pid, port = int(request[1]), int(request[2])
                            assert not servers and 0 < port < 65536, "duplicate or invalid server registration"
                            launchers.append((pid, launcher_identity(pid)))
                            real = subprocess.Popen([str(server.resolve()), "--port", str(port)],
                                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                            servers.append(real)
                            connection.sendall(b"\x01")
                            registered.set()
                except BaseException as error:
                    errors.append(error)
                    registered.set()

            owner = threading.Thread(target=own_servers, daemon=True)
            owner.start()
            process = subprocess.Popen([str(client.resolve()), "start", str(root),
                                        toolchain.as_posix(), launcher.parent.as_posix(),
                                        candidate.as_posix(), launcher.suffix], env=environment,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, errors="replace")
            timed_out = False
            try:
                startup_deadline = time.monotonic() + LSP_STARTUP_TIMEOUT
                assert registered.wait(LSP_STARTUP_TIMEOUT), "launcher did not register an owned server"
                if errors:
                    raise errors[0]
                if mode != "normal":
                    while not (root / "fault-ready").exists() and process.poll() is None and time.monotonic() < startup_deadline:
                        time.sleep(0.02)
                    assert (root / "fault-ready").read_text() == mode, "client did not reach fault injection after initialize"
                try:
                    stdout, stderr = process.communicate(timeout=2 if mode == "timeout" else LSP_CLIENT_TIMEOUT)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    if mode != "timeout":
                        raise
                if mode == "normal":
                    assert process.returncode == 0 and "PASS LspSession natural SDK sibling discovery" in stdout and "runtime error:" not in (stdout + stderr).lower(), stdout + stderr
                    print(stdout.strip())
                elif mode == "timeout":
                    assert timed_out, "timeout fault did not hang the client"
                else:
                    assert process.returncode == 23, f"exit fault returned {process.returncode}, expected 23"
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)
                stop.set(); owner.join(timeout=6)
                assert not owner.is_alive(), "server owner did not stop"
                for real in servers:
                    if mode == "normal":
                        try:
                            real.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            pass
                    if real.poll() is None:
                        real.terminate()
                    try:
                        real.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        real.kill(); real.communicate(timeout=5)
                    assert real.poll() is not None, f"owned server remains: {real.pid}"
                for pid, identity in launchers:
                    reap_launcher(pid, identity)
                assert not errors, errors
                assert servers, "no real server was owned"
                print(f"PASS natural discovery cleanup mode={mode}: server PIDs={[p.pid for p in servers]} exited, launcher PIDs={[p for p, _ in launchers]} exited")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--zanc", type=Path)
    parser.add_argument("--server", type=Path)
    args = parser.parse_args()
    if args.zanc:
        root = Path(__file__).resolve().parents[2]
        sources = [root / "tests/lsp/lsp_session_test.zan",
                   *sorted((root / "src/ide_zan/src/models/lsp").glob("*.zan")),
                   *sorted((root / "src/ide_zan/src/models/dap").glob("*.zan")),
                   root / "src/ide_zan/src/services/LspSession.zan",
                   root / "src/ide_zan/src/services/LspSession.Transport.zan",
                   root / "src/ide_zan/src/services/LspSession.Protocol.zan",
                   root / "src/ide_zan/src/services/LspSession.Diagnostics.zan",
                   root / "src/ide_zan/src/editor/LspSession.Editor.zan",
                   root / "src/ide_zan/src/services/DebugSession.zan"]
        compiled = subprocess.run([str(args.zanc.resolve()), *map(str, sources),
                                   "--auto-stdlib", "-o", str(args.client.resolve())],
                                  cwd=root, capture_output=True, text=True, errors="replace", timeout=240)
        output = (compiled.stdout or "") + (compiled.stderr or "")
        assert compiled.returncode == 0 and "error:" not in output.lower() and args.client.exists(), output
    executable_resolution_probe(args.client)
    errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        thread = threading.Thread(target=serve, args=(listener, errors), daemon=True)
        thread.start()
        process = subprocess.run([str(args.client.resolve()), str(listener.getsockname()[1])],
                                 capture_output=True, text=True, errors="replace", timeout=20)
        thread.join(timeout=2)
        assert not thread.is_alive(), "假服务器未完成协议序列"
        if errors:
            raise AssertionError(f"{errors[0]}\n{process.stdout}\n{process.stderr}")
        assert process.returncode == 0 and "runtime error:" not in (process.stdout + process.stderr).lower(), process.stdout + process.stderr
        assert "PASS LspSession protocol" in process.stdout, process.stdout + process.stderr
        print(process.stdout.strip())
        print("PASS wire didOpen/didChange versions, UTF-16, stale IDs, semantic cache invalidation and 64 terminals")
    if args.server:
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
        with tempfile.TemporaryDirectory(prefix="zan_lsp_editor_") as workspace:
            server = subprocess.Popen([str(args.server.resolve()), "--port", str(port)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                client = subprocess.run([str(args.client.resolve()), str(port), "real", workspace],
                                        capture_output=True, text=True, errors="replace", timeout=120)
                if client.returncode != 0 or "PASS real CodeEditor" not in client.stdout:
                    print("=== CLIENT STDOUT ===")
                    print(client.stdout)
                    print("=== CLIENT STDERR ===")
                    print(client.stderr)
                    if server.poll() is None:
                        server.kill()
                    sout, serr = server.communicate()
                    print("=== SERVER STDOUT ===")
                    print(sout.decode("utf-8", errors="replace"))
                    print("=== SERVER STDERR ===")
                    print(serr.decode("utf-8", errors="replace"))
                assert client.returncode == 0 and "PASS real CodeEditor" in client.stdout and "runtime error:" not in (client.stdout + client.stderr).lower(), client.stdout + client.stderr
                server.communicate(timeout=5)
                assert server.returncode == 0, "真实服务器退出失败"
                print(client.stdout.strip())
            finally:
                if server.poll() is None:
                    server.kill()
                    server.communicate()
        assert args.zanc, "natural discovery regression requires --zanc for its test launcher"
        output_dir = args.client.resolve().parent / "test-out"
        with tempfile.TemporaryDirectory(prefix="lsp_launcher_", dir=output_dir) as temporary:
            launcher = Path(temporary) / ("zan-lsp" + args.server.suffix)
            root = Path(__file__).resolve().parents[2]
            compiled = subprocess.run([str(args.zanc.resolve()), str(root / "tests/lsp/lsp_launcher_test.zan"),
                                       "--auto-stdlib", "-o", str(launcher)], cwd=root,
                                      capture_output=True, text=True, errors="replace", timeout=90)
            assert compiled.returncode == 0 and launcher.exists(), compiled.stdout + compiled.stderr
            for mode in ("normal", "timeout", "exit"):
                natural_discovery_probe(args.client, args.server, launcher, mode)
        project_probe(args.server)


if __name__ == "__main__":
    main()

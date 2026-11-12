#!/usr/bin/env python3
"""Independent loopback interoperability checks for Zan.Net.

Run from any directory after the shared build gate is available:
    python -I tests/integration/net_interop_test.py --compiler build/zanc.exe
Optional HTTP/2 peer: install h2 into an isolated _scratch venv, then run this
script with that venv's Python. No dependencies are installed by the harness.
Exit 0 = every item passed, 1 = failure, 2 = incomplete (explicit skips).
Certificates/keys are generated for each run and deleted even on failure.
The caller owns build serialization; this script must not overlap another build.
"""
from __future__ import annotations

import argparse
import contextlib
import datetime
import gzip
import hashlib
import http.client
import http.server
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import socket
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
HTTP_CASES = (
    "binary_get", "binary_echo", "gzip_empty", "gzip_one", "gzip_binary",
    "gzip_exact", "gzip_over", "gzip_bad_crc", "gzip_truncated",
    "gzip_missing_decoder", "disconnect_recovery", "http_stress",
)
TLS_CASES = tuple(
    f"tls_{kind}_{version}" for version in ("12", "13")
    for kind in ("trusted", "wrong_ca", "wrong_host", "missing_intermediate")
)
H2_CASES = ("h2_multiplex_binary", "h2_large_flow_control", "h2_binary_post", "h2_reconnect")
PREFIX = "ZAN_INTEROP_RESULT "
ZAN_DIAGNOSTICS = ("memory leak detected", "runtime error:", "unhandled exception", "arc guard")


def runtime_diagnostics(text):
    lowered = text.lower()
    return [marker for marker in ZAN_DIAGNOSTICS if marker in lowered]


def zan_environment(settings=None):
    env = os.environ.copy()
    env.update({key: str(value) for key, value in (settings or {}).items()})
    # An inherited ZAN_RT_HARD=0 must not override --strict-runtime.
    env["ZAN_RT_HARD"] = "1"
    return env


class WindowsJob:
    """Assign a suspended child before it can spawn; closing kills all descendants."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        class BasicLimits(ctypes.Structure):
            _fields_ = [("ProcessTime", ctypes.c_int64), ("JobTime", ctypes.c_int64),
                        ("LimitFlags", wintypes.DWORD), ("MinWorkingSet", ctypes.c_size_t),
                        ("MaxWorkingSet", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                        ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                        ("SchedulingClass", wintypes.DWORD)]
        class IoCounters(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in
                        ("ReadOps", "WriteOps", "OtherOps", "ReadBytes", "WriteBytes", "OtherBytes")]
        class ExtendedLimits(ctypes.Structure):
            _fields_ = [("Basic", BasicLimits), ("Io", IoCounters),
                        ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                        ("PeakProcessMemory", ctypes.c_size_t), ("PeakJobMemory", ctypes.c_size_t)]
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.kernel.SetInformationJobObject.restype = wintypes.BOOL
        self.kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.kernel.AssignProcessToJobObject.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.handle = self.kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedLimits()
        limits.Basic.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            error = ctypes.WinError(ctypes.get_last_error())
            self.close()
            raise error

    def assign_and_resume(self, process):
        if not self.kernel.AssignProcessToJobObject(self.handle, int(process._handle)):
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        # Popen closes the initial thread handle. Resume the suspended process
        # through NT's HANDLE -> NTSTATUS API only after Job assignment succeeds.
        resume = self.ctypes.WinDLL("ntdll").NtResumeProcess
        resume.argtypes = [self.ctypes.c_void_p]
        resume.restype = self.ctypes.c_long
        status = resume(int(process._handle))
        if status < 0:
            raise OSError(f"NtResumeProcess failed: NTSTATUS 0x{status & 0xffffffff:08x}")

    def close(self):
        if self.handle:
            if not self.kernel.CloseHandle(self.handle):
                raise self.ctypes.WinError(self.ctypes.get_last_error())
            self.handle = None


def close_process_job(process):
    job = getattr(process, "_interop_job", None)
    if job is not None:
        job.close()


def start_process(argv, *, env=None, stdout=subprocess.PIPE):
    job = WindowsJob() if os.name == "nt" else None
    process = None
    try:
        # CREATE_SUSPENDED prevents the child from spawning before Job assignment.
        options = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP | 0x4} if job else {"start_new_session": True}
        process = subprocess.Popen([str(arg) for arg in argv], cwd=ROOT, env=env,
                                   stdout=stdout, stderr=subprocess.STDOUT, **options)
        if job:
            process._interop_job = job
            job.assign_and_resume(process)
        return process
    except BaseException:
        if job:
            job.close()
        if process is not None:
            # Assignment may have failed: the suspended parent cannot have children.
            process.kill()
            process.communicate(timeout=10)
        raise


def terminate_process_tree(process):
    if os.name == "nt":
        # The Job survives its original parent PID and owns orphaned descendants.
        close_process_job(process)
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The isolated process group already exited.


def finish_process(process, timeout):
    try:
        try:
            output, _ = process.communicate(timeout=timeout)
            return output or b"", False
        except subprocess.TimeoutExpired:
            terminate_process_tree(process)
            output, _ = process.communicate(timeout=10)
            return output or b"", True
    except BaseException as error:
        # KeyboardInterrupt/SystemExit and ordinary errors have the same ownership
        # obligation as a timeout. Drain/reap before propagating the original error.
        try:
            terminate_process_tree(process)
            process.communicate(timeout=10)
        except BaseException as cleanup_error:
            error.add_note(f"process cleanup also failed: {cleanup_error!r}")
        raise
    finally:
        # Also prevent silent background descendants after a successful parent exit.
        close_process_job(process)


def payload(size: int) -> bytes:
    return bytes((i * 31 + 7) % 256 for i in range(size))


class Recorder:
    def __init__(self):
        self.lock = threading.Lock()
        self.events = []

    def add(self, **event):
        with self.lock:
            self.events.append(event)

    def snapshot(self):
        with self.lock:
            return list(self.events)


class HttpPeer(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def reply(self, data, *, coding=None, advertised=None):
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data) if advertised is None else advertised))
        self.send_header("Connection", "close")
        if coding:
            self.send_header("Content-Encoding", coding)
        self.end_headers()
        self.wfile.write(data)
        self.wfile.flush()
        self.close_connection = True

    def do_GET(self):
        self.server.record.add(method="GET", path=self.path)
        if self.path.startswith("/binary/"):
            self.reply(payload(int(self.path.rsplit("/", 1)[1])))
        elif self.path.startswith("/gzip/"):
            self.reply(gzip.compress(payload(int(self.path.rsplit("/", 1)[1])), mtime=0), coding="gzip")
        elif self.path.startswith("/gzip_"):
            size = 4097 if self.path == "/gzip_over" else 257
            data = gzip.compress(payload(size), mtime=0)
            if self.path == "/gzip_bad_crc":
                data = data[:-8] + bytes([data[-8] ^ 1]) + data[-7:]
            elif self.path == "/gzip_truncated":
                data = data[:-5]
            elif self.path not in ("/gzip_over", "/gzip_missing_decoder"):
                self.send_error(404)
                return
            self.reply(data, coding="gzip")
        elif self.path == "/disconnect":
            # Use the real HTTP server and deliberately break only body delivery.
            self.reply(b"partial", advertised=100)
            self.connection.shutdown(socket.SHUT_WR)
        else:
            self.send_error(404)

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "-1"))
        if self.path != "/echo" or not 0 <= size <= 131072:
            self.send_error(400)
            return
        data = self.rfile.read(size)
        valid = len(data) == size and data == payload(size)
        self.server.record.add(method="POST", path=self.path, size=size, valid=valid)
        if not valid:
            self.send_error(422)
            return
        self.reply(data)


class HttpPeerServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = False

    def __init__(self):
        super().__init__(("127.0.0.1", 0), HttpPeer)
        self.record = Recorder()

    def handle_error(self, request, client_address):
        self.record.add(error=str(sys.exc_info()[1]))


class TlsPeer(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(12)
        try:
            with self.server.tls_context.wrap_socket(self.request, server_side=True) as stream:
                self.server.record.add(handshake=True, version=stream.version())
                data = bytearray()
                while len(data) < 257:
                    part = stream.recv(257 - len(data))
                    if not part:
                        break
                    data.extend(part)
                valid = bytes(data) == payload(257)
                self.server.record.add(echo_bytes=len(data), valid=valid)
                if valid:
                    stream.sendall(data)
        except (ssl.SSLError, OSError) as error:
            # Certificate-negative tests intentionally abort the Python handshake.
            self.server.record.add(tls_error=str(error))


class TlsPeerServer(socketserver.ThreadingTCPServer):
    daemon_threads = True
    block_on_close = False

    def __init__(self, tls_context):
        super().__init__(("127.0.0.1", 0), TlsPeer)
        self.tls_context = tls_context
        self.record = Recorder()


@contextlib.contextmanager
def serving(server):
    thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


class Harness:
    def __init__(self, args):
        self.args = args
        run_id = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        self.output = ROOT / "build" / "test-out" / "net-interop" / run_id
        self.output.mkdir(parents=True)
        self.cases = ROOT / "build" / "cases"
        self.cases.mkdir(parents=True, exist_ok=True)
        self.results = []
        self.report = {
            "schema": 1, "started_utc": run_id, "python": sys.version,
            "platform": platform.platform(), "ssl": ssl.OPENSSL_VERSION,
            "scope": "independent loopback peers; bounded functional stress, not a throughput benchmark",
            "loops": args.loops, "results": self.results,
        }

    def add(self, name, status, detail, **extra):
        result = {"case": name, "status": status, "detail": detail, **extra}
        self.results.append(result)
        print(f"{status.upper():4} {name}: {detail}", flush=True)
        self.save()
        return result

    def skip(self, names, reason):
        for name in names:
            self.add(name, "skip", reason)

    def save(self):
        counts = {status: sum(r["status"] == status for r in self.results)
                  for status in ("pass", "fail", "skip")}
        self.report["counts"] = counts
        self.report["status"] = "fail" if counts["fail"] else "incomplete" if counts["skip"] else "pass"
        destination = self.output / "results.json"
        temporary = destination.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        temporary.replace(destination)

    def command(self, name, argv, *, env=None, timeout=None):
        started = time.monotonic()
        log = self.output / f"{name}.log"
        process = start_process(argv, env=env)
        output, timed_out = finish_process(process, timeout or self.args.timeout)
        text = output.decode("utf-8", errors="replace")
        code = process.returncode
        if timed_out:
            text += "\nHARNESS TIMEOUT (process tree terminated)\n"
            code = -1
        log.write_text(text, encoding="utf-8", newline="")
        return code, text, {"command": [str(arg) for arg in argv], "log": str(log),
                            "seconds": round(time.monotonic() - started, 3), "exit_code": code}

    def build(self, stem):
        source = ROOT / "tests" / "integration" / f"{stem}.zan"
        suffix = ".exe" if os.name == "nt" else ""
        binary = self.cases / f"{stem}{suffix}"
        # Remove a stale executable before compilation; a failed build cannot reuse it.
        binary.unlink(missing_ok=True)
        argv = [self.args.compiler, source, "--auto-stdlib", "--package-project", ROOT,
                "--check-leaks", "--strict-runtime", "-o", binary]
        code, text, evidence = self.command(f"build_{stem}", argv, timeout=self.args.compile_timeout)
        ok = code == 0 and binary.is_file()
        self.add(f"build_{stem}", "pass" if ok else "fail",
                 "fixture compiled" if ok else "fixture compilation failed; see log", **evidence,
                 source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
        return binary if ok else None

    def fixture(self, binary, name, port, **settings):
        env = zan_environment({"ZAN_INTEROP_CASE": name, "ZAN_INTEROP_PORT": port,
                               "ZAN_INTEROP_LOOPS": self.args.loops, **settings})
        code, text, evidence = self.command(name, [binary], env=env)
        rows = [line[len(PREFIX):] for line in text.splitlines() if line.startswith(PREFIX)]
        result = None
        if len(rows) == 1:
            try:
                parsed = json.loads(rows[0])
                if isinstance(parsed, dict):
                    result = parsed
            except json.JSONDecodeError:
                pass
        diagnostics = runtime_diagnostics(text)
        ok = code == 0 and not diagnostics and result is not None and result.get("case") == name and result.get("status") == "pass"
        detail = result.get("detail", "missing detail") if result else "missing/invalid fixture result"
        if diagnostics:
            detail = f"Zan runtime diagnostics ({', '.join(diagnostics)}): {detail}"
        if code != 0:
            detail = f"fixture exited {code}: {detail}"
        return self.add(name, "pass" if ok else "fail", detail, fixture=result,
                        runtime_diagnostics=diagnostics, **evidence)

    def http(self, binary):
        with serving(HttpPeerServer()) as server:
            for name in HTTP_CASES:
                start = len(server.record.snapshot())
                result = self.fixture(binary, name, server.server_address[1])
                events = server.record.snapshot()[start:]
                result["peer_events"] = events
                expected = self.args.loops if name == "http_stress" else 2 if name == "disconnect_recovery" else 1
                requests = [event for event in events if "method" in event]
                if len(requests) != expected or any(event.get("valid") is False or "error" in event for event in events):
                    result.update(status="fail", detail="Python peer observed unexpected request count/data/errors")
                self.save()

    def certificates(self, directory):
        openssl = shutil.which(self.args.openssl)
        if not openssl:
            return None
        def command(*arguments):
            code, text, evidence = self.command("certificates", [openssl, *arguments], timeout=30)
            with (self.output / "certificate-generation.log").open("a", encoding="utf-8") as log:
                log.write(json.dumps(evidence) + "\n" + text + "\n")
            if code != 0:
                raise RuntimeError("openssl certificate generation failed; see certificate-generation.log")
        def root(stem):
            command("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-days", "2",
                    "-subj", f"/CN=Zan Interop {stem}", "-keyout", directory / f"{stem}.key",
                    "-out", directory / f"{stem}.pem", "-addext", "basicConstraints=critical,CA:TRUE,pathlen:1",
                    "-addext", "keyUsage=critical,keyCertSign,cRLSign")
        def signed(stem, issuer, extensions):
            extension_file = directory / f"{stem}.ext"
            extension_file.write_text(extensions, encoding="ascii")
            command("req", "-new", "-newkey", "rsa:2048", "-nodes", "-sha256",
                    "-subj", f"/CN={'localhost' if stem == 'leaf' else 'Zan Interop Intermediate'}",
                    "-keyout", directory / f"{stem}.key", "-out", directory / f"{stem}.csr")
            command("x509", "-req", "-in", directory / f"{stem}.csr", "-CA", directory / f"{issuer}.pem",
                    "-CAkey", directory / f"{issuer}.key", "-CAcreateserial", "-days", "2", "-sha256",
                    "-extfile", extension_file, "-out", directory / f"{stem}.pem")
        root("root")
        root("unrelated")
        signed("intermediate", "root", "basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n")
        signed("leaf", "intermediate", "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n")
        (directory / "chain.pem").write_bytes((directory / "leaf.pem").read_bytes() + (directory / "intermediate.pem").read_bytes())
        command("verify", "-CAfile", directory / "root.pem", "-untrusted", directory / "intermediate.pem",
                "-purpose", "sslserver", "-verify_hostname", "localhost", directory / "leaf.pem")
        self.add("certificate_generation", "pass", "fresh root/intermediate/leaf and unrelated CA generated; OpenSSL verified chain/hostname")
        return directory

    def tls(self, binary):
        # The private keys never enter a permanent output directory or the system trust store.
        with tempfile.TemporaryDirectory(prefix="certs-", dir=self.output) as temporary:
            try:
                directory = self.certificates(Path(temporary))
            except Exception as error:
                self.add("certificate_generation", "fail", str(error))
                self.skip(TLS_CASES, "certificate generation failed")
                return
            if directory is None:
                self.skip(TLS_CASES, "openssl executable unavailable")
                return
            for name in TLS_CASES:
                version = ssl.TLSVersion.TLSv1_2 if name.endswith("12") else ssl.TLSVersion.TLSv1_3
                if name.endswith("13") and not ssl.HAS_TLSv1_3:
                    self.add(name, "skip", "Python ssl has no TLS 1.3 support")
                    continue
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = version
                context.maximum_version = version
                certificate = "leaf.pem" if "missing_intermediate" in name else "chain.pem"
                context.load_cert_chain(directory / certificate, directory / "leaf.key")
                with serving(TlsPeerServer(context)) as server:
                    result = self.fixture(binary, name, server.server_address[1],
                                          ZAN_INTEROP_CA=directory / "root.pem",
                                          ZAN_INTEROP_WRONG_CA=directory / "unrelated.pem")
                    events = server.record.snapshot()
                    result["peer_events"] = events
                    if "trusted" in name:
                        expected_version = "TLSv1.2" if name.endswith("12") else "TLSv1.3"
                        if not any(e.get("version") == expected_version for e in events) or not any(e.get("valid") is True for e in events):
                            result.update(status="fail", detail="Python peer did not observe expected TLS version and binary echo")
                    elif any(e.get("valid") is True for e in events):
                        result.update(status="fail", detail="certificate-negative case exchanged application data")
                    self.save()

    def http2(self, binary):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        env = zan_environment({"ZAN_INTEROP_PORT": port})
        log_path = self.output / "http2-server.log"
        timed_out = False
        with log_path.open("wb") as log:
            process = start_process([binary], env=env, stdout=log)
            try:
                deadline = time.monotonic() + 15
                while True:
                    if process.poll() is not None:
                        raise RuntimeError(f"HTTP/2 fixture exited before readiness: {process.returncode}")
                    try:
                        self.server_request(port, "/ready")
                        break
                    except (OSError, http.client.HTTPException):
                        if time.monotonic() >= deadline:
                            raise RuntimeError("HTTP/2 fixture readiness timed out")
                        time.sleep(0.05)
                peer = H2Peer(port)
                try:
                    self.h2_case("h2_multiplex_binary", lambda: peer.exchange([(1, "GET", "/binary", b"", payload(257)),
                                                                                (3, "GET", "/binary", b"", payload(257)),
                                                                                (5, "GET", "/binary", b"", payload(257))]))
                    self.h2_case("h2_large_flow_control", lambda: peer.exchange([(7, "GET", "/large", b"", payload(100000))]))
                    self.h2_case("h2_binary_post", lambda: peer.exchange([(9, "POST", "/echo", payload(20000), payload(20000))]))
                finally:
                    peer.close()
                def reconnect():
                    with H2Peer(port) as fresh:
                        return fresh.exchange([(1, "GET", "/binary", b"", payload(257))])
                self.h2_case("h2_reconnect", reconnect)
            except Exception as error:
                completed = {item["case"] for item in self.results}
                for name in H2_CASES:
                    if name not in completed:
                        self.add(name, "fail", str(error), server_log=str(log_path))
            finally:
                try:
                    self.server_request(port, "/shutdown")
                except (OSError, http.client.HTTPException, RuntimeError):
                    terminate_process_tree(process)
                    timed_out = True
                except BaseException:
                    # An interrupt during the shutdown request still owns the Job.
                    terminate_process_tree(process)
                    finish_process(process, 10)
                    raise
                _, cleanup_timeout = finish_process(process, 10)
                timed_out = timed_out or cleanup_timeout
        text = log_path.read_text(encoding="utf-8", errors="replace")
        self.server_shutdown_result(process.returncode, text, log_path, timed_out)

    def server_shutdown_result(self, code, text, log_path, timed_out=False):
        diagnostics = runtime_diagnostics(text)
        ok = code == 0 and not timed_out and not diagnostics
        return self.add("h2_server_shutdown", "pass" if ok else "fail",
                        "graceful exit and runtime diagnostics check" if ok else "server exit/timeout/runtime diagnostic failure",
                        exit_code=code, log=str(log_path), timed_out=timed_out,
                        runtime_diagnostics=diagnostics)

    @staticmethod
    def server_request(port, path):
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=1)
        try:
            connection.request("GET", path)
            response = connection.getresponse()
            body = response.read()
            expected = b"ready" if path == "/ready" else b"stopping"
            if response.status != 200 or body != expected:
                raise RuntimeError("unexpected fixture control response")
        finally:
            connection.close()

    def h2_case(self, name, action):
        started = time.monotonic()
        try:
            evidence = action()
            self.add(name, "pass", "independent h2 peer compared every response byte", evidence=evidence,
                     seconds=round(time.monotonic() - started, 3))
        except Exception as error:
            self.add(name, "fail", f"{type(error).__name__}: {error}", seconds=round(time.monotonic() - started, 3))


class H2Peer:
    """Protocol state, HPACK and flow control are supplied by python-h2."""
    def __init__(self, port):
        from h2.config import H2Configuration
        from h2.connection import H2Connection
        self.port = port
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.socket.settimeout(5)
        self.h2 = H2Connection(config=H2Configuration(client_side=True, header_encoding="utf-8"))
        self.h2.initiate_connection()
        self.socket.sendall(self.h2.data_to_send())

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()

    def close(self):
        self.socket.close()

    def exchange(self, requests):
        from h2.events import ConnectionTerminated, DataReceived, ResponseReceived, StreamEnded, StreamReset
        bodies = {stream: bytearray() for stream, *_rest in requests}
        statuses = {}
        completed = set()
        for stream, method, path, data, _expected in requests:
            headers = [(":method", method), (":scheme", "http"), (":authority", f"127.0.0.1:{self.port}"), (":path", path)]
            if data:
                headers.append(("content-length", str(len(data))))
            self.h2.send_headers(stream, headers, end_stream=not data)
            for offset in range(0, len(data), self.h2.max_outbound_frame_size):
                part = data[offset:offset + self.h2.max_outbound_frame_size]
                self.h2.send_data(stream, part, end_stream=offset + len(part) == len(data))
        self.socket.sendall(self.h2.data_to_send())
        frames = 0
        flow_bytes = 0
        deadline = time.monotonic() + 20
        while len(completed) < len(requests):
            if time.monotonic() >= deadline:
                raise TimeoutError("HTTP/2 streams did not complete")
            data = self.socket.recv(65536)
            if not data:
                raise RuntimeError("HTTP/2 connection closed before END_STREAM")
            for event in self.h2.receive_data(data):
                frames += 1
                if isinstance(event, ResponseReceived):
                    statuses[event.stream_id] = dict(event.headers).get(":status")
                elif isinstance(event, DataReceived):
                    if event.stream_id not in bodies:
                        raise RuntimeError("unexpected response stream")
                    bodies[event.stream_id].extend(event.data)
                    if len(bodies[event.stream_id]) > 131072:
                        raise RuntimeError("response exceeds harness bound")
                    flow_bytes += event.flow_controlled_length
                    self.h2.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                elif isinstance(event, StreamEnded):
                    completed.add(event.stream_id)
                elif isinstance(event, (StreamReset, ConnectionTerminated)):
                    raise RuntimeError(f"unexpected HTTP/2 event: {event}")
            outgoing = self.h2.data_to_send()
            if outgoing:
                self.socket.sendall(outgoing)
        for stream, _method, _path, _data, expected in requests:
            if statuses.get(stream) != "200" or bytes(bodies[stream]) != expected:
                raise AssertionError(f"stream {stream}: status/body mismatch (got {len(bodies[stream])} bytes)")
        return {"streams": sorted(completed), "response_bytes": sum(map(len, bodies.values())),
                "events": frames, "acknowledged_flow_bytes": flow_bytes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("zanc.exe" if os.name == "nt" else "zanc"))
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--loops", type=int, default=128, help="bounded echo iterations (1..1000)")
    parser.add_argument("--timeout", type=int, default=60, help="per-fixture wall-clock seconds")
    parser.add_argument("--compile-timeout", type=int, default=180, help="per-fixture compile seconds")
    args = parser.parse_args()
    if not 1 <= args.loops <= 1000 or args.timeout < 1 or args.compile_timeout < 1:
        parser.error("loops must be 1..1000; timeouts must be positive")
    args.compiler = args.compiler.resolve()
    harness = Harness(args)
    try:
        if not args.compiler.is_file():
            harness.skip((*HTTP_CASES, *TLS_CASES, *H2_CASES), f"compiler unavailable: {args.compiler}")
        else:
            client = harness.build("net_client_fixture")
            if client:
                harness.http(client)
                harness.tls(client)
            else:
                harness.skip((*HTTP_CASES, *TLS_CASES), "client fixture compilation failed")
            if importlib.util.find_spec("h2") is None:
                harness.skip(H2_CASES, "optional Python h2 dependency unavailable; install into an isolated _scratch venv")
            else:
                import h2
                harness.report["h2_version"] = h2.__version__
                server = harness.build("net_http2_server_fixture")
                if server:
                    harness.http2(server)
                else:
                    harness.skip(H2_CASES, "HTTP/2 server fixture compilation failed")
    except Exception as error:
        harness.add("harness", "fail", f"{type(error).__name__}: {error}")
    finally:
        # Every planned test gets a row, including tests blocked by an earlier error.
        recorded = {result["case"] for result in harness.results}
        harness.skip([name for name in (*HTTP_CASES, *TLS_CASES, *H2_CASES) if name not in recorded],
                     "not executed after harness error")
        harness.report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        harness.save()
    print(f"JSON: {harness.output / 'results.json'}", flush=True)
    counts = harness.report["counts"]
    return 1 if counts["fail"] else 2 if counts["skip"] else 0


if __name__ == "__main__":
    sys.exit(main())

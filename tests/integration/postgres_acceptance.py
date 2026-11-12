#!/usr/bin/env python3
"""Real PostgreSQL acceptance. Run with Python -I while owning the build gate.

--preflight-only needs no compiler/build lock: it exercises the private WSL
cluster lifecycle and Windows loopback TCP/SCRAM login, never claims Zan passed.
No existing PostgreSQL service, credentials, stdlib or packages are copied/used.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time


CASES = (
    "parameters_sync", "parameters_async",
    "uow_commit_sync", "uow_commit_async",
    "uow_rollback_sync", "uow_rollback_async",
    "uow_savepoint_sync", "uow_savepoint_async",
    "aborted_commit_sync", "aborted_commit_async",
    "pool_server_disconnect_recovery", "pool_bounded_reuse",
    "pool_auth_failure_bounded",
)
PAYLOAD = "O'Brien'); DROP TABLE acceptance_records; -- ? $1 \\ 中文\nline"


def fixture_records(stdout: str, expected=CASES) -> list[dict]:
    """Require every named boolean result exactly once; pool diagnostics may coexist."""
    records = {}
    for line in stdout.splitlines():
        if not line.lstrip().startswith("{"):
            continue
        item = json.loads(line)
        if not isinstance(item, dict) or item.get("case") not in expected:
            raise RuntimeError("unexpected fixture JSON result")
        if type(item.get("ok")) is not bool or not isinstance(item.get("detail"), str):
            raise RuntimeError("malformed fixture result")
        name = item["case"]
        if name in records:
            raise RuntimeError(f"duplicate fixture case: {name}")
        records[name] = item
    missing = sorted(set(expected) - records.keys())
    if missing:
        raise RuntimeError(f"fixture omitted cases: {', '.join(missing)}")
    return [records[name] for name in expected]


def stress_counts(loops):
    rolled_back = (loops + 2) // 3
    return {"loops": loops, "completed": loops, "inserted": loops,
            "committed": loops - rolled_back, "rolled_back": rolled_back}


def validate_stress_worker(item, worker, loops):
    expected = {"worker": worker, **stress_counts(loops)}
    for name, value in expected.items():
        if type(item.get(name)) is not int or item[name] != value:
            raise RuntimeError(f"stress worker {worker} counter {name} expected {value}, got {item.get(name)!r}")


class WindowsJob:
    """Own suspended children before they run; close kills descendants after parent exit."""
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


class Acceptance:
    def __init__(self, args):
        self.args = args
        self.root = Path(__file__).resolve().parents[2]
        self.output = self.root / "build" / "test-out"
        self.output.mkdir(parents=True, exist_ok=True)
        base_prefix = "postgres_acceptance.preflight" if args.preflight_only else "postgres_acceptance"
        self.run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "-" + secrets.token_hex(3)
        self.prefix = base_prefix + "." + self.run_id
        self.report_path = self.output / (self.prefix + ".json")
        self.latest_report = self.output / (base_prefix + ".json")
        self.journal_path = self.output / (self.prefix + ".commands.jsonl")
        self.command_number = 0
        self.current_stage = "runner_init"
        self.results = []
        self.children = []
        self.passwords = [secrets.token_hex(24) for _ in range(3)]
        self.linux_root = None
        self.scratch = None
        self.start_attempted = False
        self.psycopg2 = None
        self.port = 0
        self.role = "zan_accept"
        self.database = "zan_acceptance"
        self.admin = "zan_accept_admin"
        self.env = {k: v for k, v in os.environ.items()
                    if not k.upper().startswith(("PG", "ZAN_PG_ACCEPTANCE_")) and k.upper() != "WSLENV"}

    def redact(self, value) -> str:
        text = str(value)
        for password in self.passwords:
            text = text.replace(password, "<redacted>")
        return text

    def record(self, name: str, ok: bool, detail, **extra):
        # Sanitize both console and persisted reports, even on subprocess failures.
        item = {"case": name, "ok": bool(ok), "detail": self.redact(detail), **extra}
        item = json.loads(self.redact(json.dumps(item, ensure_ascii=False)))
        self.results.append(item)
        print(json.dumps(item, ensure_ascii=True), flush=True)

    def log(self, name: str, text: str):
        (self.output / f"{self.prefix}.{name}.log").write_text(self.redact(text), encoding="utf-8")

    def command_event(self, event, **fields):
        item = {"event": event, "run_id": self.run_id,
                "timestamp": datetime.now(timezone.utc).isoformat(), **fields}
        text = self.redact(json.dumps(item, ensure_ascii=True))
        with self.journal_path.open("a", encoding="utf-8") as journal:
            journal.write(text + "\n")
        print(text, flush=True)

    def start_owned_process(self, argv, **kwargs):
        job = WindowsJob() if os.name == "nt" else None
        process = None
        try:
            options = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP | 0x4} if job else {"start_new_session": True}
            process = subprocess.Popen([str(arg) for arg in argv], cwd=self.root, **kwargs, **options)
            if job:
                process._pg_job = job
                job.assign_and_resume(process)
            return process
        except BaseException as error:
            try:
                if job:
                    job.close()
                if process is not None:
                    process.kill()
                    process.communicate(timeout=5)
            except BaseException as cleanup_error:
                error.add_note(f"launch cleanup failed: {cleanup_error!r}")
            raise

    def terminate_tree(self, process, stage):
        if os.name == "nt":
            job = getattr(process, "_pg_job", None)
            if job is None:
                raise RuntimeError("owned Windows child has no process Job")
            # Job ownership survives the original PID and also covers a successful parent exit.
            job.close()
            mechanism = "job_kill_on_close"
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            mechanism = "killpg"
        self.command_event("process_tree_stop", stage=stage, pid=process.pid, mechanism=mechanism)

    def run(self, argv, *, stdin=None, timeout=40, env=None, check=True, stage=None):
        self.command_number += 1
        number = self.command_number
        self.current_stage = stage or Path(str(argv[0])).name
        label = self.current_stage
        started = time.monotonic()
        self.command_event("command_start", command_number=number, stage=label, timeout_seconds=timeout)
        process = None
        try:
            process = self.start_owned_process(
                argv, stdin=subprocess.DEVNULL if stdin is None else subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8", errors="replace",
                env=self.env if env is None else env,
            )
            try:
                stdout, stderr = process.communicate(input=stdin, timeout=timeout)
            except subprocess.TimeoutExpired:
                stop_error = None
                try:
                    self.terminate_tree(process, label)
                except Exception as exc:
                    stop_error = str(exc)
                    if process.poll() is None:
                        process.kill()
                try:
                    stdout, stderr = process.communicate(timeout=5)
                except subprocess.TimeoutExpired as exc:
                    stdout = exc.stdout or b""
                    stderr = exc.stderr or b""
                    if isinstance(stdout, bytes):
                        stdout = stdout.decode("utf-8", "replace")
                    if isinstance(stderr, bytes):
                        stderr = stderr.decode("utf-8", "replace")
                    stop_error = (stop_error or "") + "; process tree pipes did not close after kill"
                    for pipe in (process.stdout, process.stderr):
                        pipe.close()
                self.log(f"command_{number:03d}.stdout", stdout)
                self.log(f"command_{number:03d}.stderr", stderr)
                self.command_event("command_timeout", command_number=number, stage=label,
                                   elapsed_seconds=round(time.monotonic() - started, 3),
                                   timeout_seconds=timeout, stdout=stdout, stderr=stderr, cleanup_error=stop_error)
                raise RuntimeError(f"stage={label}: {Path(str(argv[0])).name} timed out after {timeout}s; tree_cleanup_error={stop_error}\n{stdout}{stderr}") from None
            result = subprocess.CompletedProcess(argv, process.returncode, stdout, stderr)
        except BaseException as error:
            self.command_event("command_failed", command_number=number, stage=label, error=repr(error))
            if process is not None:
                try:
                    self.terminate_tree(process, label)
                    stdout, stderr = process.communicate(timeout=5)
                    self.log(f"command_{number:03d}.stdout", stdout or "")
                    self.log(f"command_{number:03d}.stderr", stderr or "")
                except BaseException as cleanup_error:
                    error.add_note(f"command cleanup failed: {cleanup_error!r}")
            raise
        finally:
            if process is not None:
                # Close even after successful parent exit: descendants must not escape acceptance.
                self.terminate_tree(process, label)
        self.log(f"command_{number:03d}.stdout", result.stdout)
        self.log(f"command_{number:03d}.stderr", result.stderr)
        self.command_event("command_end", command_number=number, stage=label,
                           elapsed_seconds=round(time.monotonic() - started, 3), exit_code=result.returncode)
        if check and result.returncode:
            raise RuntimeError(f"stage={label}: {Path(str(argv[0])).name} exited {result.returncode}\n{result.stdout}{result.stderr}")
        return result

    def wsl(self, argv, *, stage=None, **kwargs):
        label = stage or "wsl_" + Path(str(argv[0])).name
        return self.run(["wsl.exe", "-d", self.args.distro, "-u", self.args.wsl_user,
                         "--cd", "/tmp", "--exec", *argv], stage=label, **kwargs)

    def dependencies(self):
        if sys.platform != "win32":
            raise RuntimeError("this runner requires Windows Python and an Ubuntu WSL distribution")
        import psycopg2
        self.psycopg2 = psycopg2
        uid = self.wsl(["id", "-u"], stage="wsl_cold_start_id", timeout=self.args.wsl_start_timeout).stdout.strip()
        if uid == "0" or not uid.isdigit():
            raise RuntimeError(f"WSL user must be non-root; id -u returned {uid!r}")
        versions = []
        for name in ("initdb", "pg_ctl"):
            versions.append(self.wsl([f"{self.args.pg_bin}/{name}", "--version"], stage=f"wsl_{name}_version").stdout.strip())
        self.record("dependencies", True, "Windows psycopg2 and non-root WSL PostgreSQL tools available",
                    wsl_user=self.args.wsl_user, uid=int(uid), versions=versions)

    def compile_fixture(self):
        executable = self.root / "build" / "cases" / "postgres_acceptance.exe"
        executable.parent.mkdir(parents=True, exist_ok=True)
        if self.args.skip_build:
            if not executable.is_file():
                raise RuntimeError(f"compiled fixture is missing: {executable}")
            self.record("fixture_build", True, "explicit --skip-build: using existing executable", built=False)
            return executable
        compiler = Path(self.args.compiler) if self.args.compiler else self.root / "build" / "zanc.exe"
        if not compiler.is_file():
            raise RuntimeError(f"compiler is missing: {compiler}")
        result = self.run([
            compiler, self.root / "tests" / "integration" / "postgres_acceptance.zan",
            "--auto-stdlib", "--package-project", self.root,
            "-g", "--check-leaks", "--strict-runtime", "-o", executable,
        ], timeout=self.args.compile_timeout, check=False, stage="zan_fixture_compile")
        self.log("compile", result.stdout + result.stderr)
        if result.returncode or not executable.is_file():
            raise RuntimeError(f"fixture compilation exited {result.returncode}; see {self.prefix}.compile.log")
        self.record("fixture_build", True, "compiled fixture with leak and strict-runtime checks", built=True)
        return executable

    def initialize(self):
        scratch_parent = self.root / "_scratch"
        scratch_parent.mkdir(exist_ok=True)
        self.scratch = Path(tempfile.mkdtemp(prefix="postgres_acceptance-", dir=scratch_parent))
        # All libpq inputs are explicit; do not consult a user's pgpass/service files.
        for filename in ("pgpass", "pg_service.conf"):
            (self.scratch / filename).write_text("", encoding="ascii")
        self.env.update(PGPASSFILE=str(self.scratch / "pgpass"),
                        PGSERVICEFILE=str(self.scratch / "pg_service.conf"),
                        PGSYSCONFDIR=str(self.scratch))
        root = self.wsl(["mktemp", "-d", "/tmp/zan-pg-XXXXXXXXXX"], stage="private_directory_create").stdout.strip()
        if not re.fullmatch(r"/tmp/zan-pg-[A-Za-z0-9]+", root):
            raise RuntimeError(f"unexpected mktemp output: {root!r}")
        self.linux_root = root
        # The password travels over stdin and a mode-0600 Linux file, never argv/logs.
        script = '''set -eu
umask 077
IFS= read -r password
printf '%s\\n' "$password" > "$1/password"
trap 'rm -f "$1/password"' EXIT
"$2/initdb" -D "$1/data" --username=zan_accept_admin --encoding=UTF8 --locale=C --auth-local=reject --auth-host=scram-sha-256 --pwfile="$1/password"
'''
        result = self.wsl(["sh", "-c", script, "sh", root, self.args.pg_bin],
                          stdin=self.passwords[0] + "\n", timeout=60, stage="private_initdb_password_stdin")
        self.log("initdb", result.stdout + result.stderr)
        # Reserve an available Windows loopback port; PostgreSQL must also bind it in WSL.
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as candidate:
            candidate.bind(("127.0.0.1", 0))
            self.port = candidate.getsockname()[1]
        configuration = f"""
listen_addresses = '127.0.0.1'
port = {self.port}
unix_socket_directories = ''
ssl = off
password_encryption = 'scram-sha-256'
max_connections = 16
statement_timeout = '8s'
idle_in_transaction_session_timeout = '15s'
log_min_messages = warning
log_min_error_statement = panic
"""
        self.wsl(["sh", "-c", 'umask 077; cat >> "$1/data/postgresql.conf"', "sh", root],
                 stdin=configuration, stage="private_configuration_stdin")
        self.record("private_cluster_init", True, "created a fresh SCRAM cluster; no system services accessed",
                    linux_data=f"{root}/data", host="127.0.0.1", port=self.port)

    def start(self):
        self.start_attempted = True
        result = self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data",
                           "-l", f"{self.linux_root}/server.log", "-w", "-t", "20", "start"], timeout=30, stage="private_pg_ctl_start")
        self.log("start", result.stdout + result.stderr)
        self.record("private_cluster_start", True, "pg_ctl confirmed private server readiness")

    def connect(self, *, database="postgres", user=None, password=None):
        # psycopg2 uses the environment of this Python process, not a child env.
        # Temporarily replace only libpq settings so inherited PGSERVICE cannot load secrets.
        saved = {k: v for k, v in os.environ.items() if k.upper().startswith("PG")}
        try:
            for key in saved:
                del os.environ[key]
            for key in ("PGPASSFILE", "PGSERVICEFILE", "PGSYSCONFDIR"):
                os.environ[key] = self.env[key]
            return self.psycopg2.connect(
                host="127.0.0.1", port=self.port, dbname=database,
                user=self.admin if user is None else user,
                password=self.passwords[0] if password is None else password,
                connect_timeout=3, sslmode="disable",
                options="-c statement_timeout=8000", application_name="zan_pg_acceptance_observer",
            )
        finally:
            for key in list(os.environ):
                if key.upper().startswith("PG"):
                    del os.environ[key]
            os.environ.update(saved)

    def check_loopback(self):
        error = None
        for _ in range(10):
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=1):
                    pass
                admin = self.connect()
                try:
                    with admin.cursor() as cursor:
                        cursor.execute("SELECT inet_client_addr()::text, inet_server_addr()::text, version()")
                        client, server, version = cursor.fetchone()
                        if not ipaddress.ip_interface(server).ip.is_loopback:
                            raise RuntimeError(f"server TCP address is not loopback: {server}")
                finally:
                    admin.close()
                self.record("windows_loopback_tcp", True, "Windows psycopg2 authenticated over loopback TCP",
                            client_address=client, server_address=server, version=version)
                return
            except (OSError, self.psycopg2.OperationalError) as exc:
                error = str(exc)
                time.sleep(0.2)
        evidence = {"windows_error": self.redact(error)}
        for name, command in (
            ("wsl_pg_ctl_status", [f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data", "status"]),
            ("wsl_listeners", ["ss", "-ltn"]),
        ):
            result = self.wsl(command, check=False)
            evidence[name] = {"exit_code": result.returncode, "output": result.stdout + result.stderr}
        self.record("windows_loopback_tcp", False, "Windows to WSL loopback TCP/SCRAM is unavailable", evidence=evidence)
        raise RuntimeError("loopback interoperability failed; original errors and WSL listener evidence are in the JSON report")

    def setup_database(self):
        from psycopg2 import sql
        admin = self.connect()
        try:
            admin.autocommit = True
            with admin.cursor() as cursor:
                cursor.execute(sql.SQL("CREATE ROLE {} LOGIN PASSWORD %s").format(sql.Identifier(self.role)), (self.passwords[1],))
                cursor.execute(sql.SQL("CREATE DATABASE {} OWNER {}").format(sql.Identifier(self.database), sql.Identifier(self.role)))
                cursor.execute("SELECT rolpassword LIKE 'SCRAM-SHA-256$%%' FROM pg_authid WHERE rolname=%s", (self.role,))
                if cursor.fetchone() != (True,):
                    raise RuntimeError("private application role password was not stored as SCRAM")
                cursor.execute("SELECT auth_method FROM pg_hba_file_rules WHERE type LIKE 'host%%'")
                methods = [row[0] for row in cursor.fetchall()]
                if not methods or set(methods) != {"scram-sha-256"}:
                    raise RuntimeError(f"unexpected host authentication methods: {methods}")
        finally:
            admin.close()
        app = self.connect(database=self.database, user=self.role, password=self.passwords[1])
        try:
            with app.cursor() as cursor:
                cursor.execute("SELECT current_user, rolsuper FROM pg_roles WHERE rolname=current_user")
                if cursor.fetchone() != (self.role, False):
                    raise RuntimeError("fixture role is not the expected non-superuser")
                cursor.execute("CREATE TABLE acceptance_records (id integer PRIMARY KEY, payload text NOT NULL, nullable_value text, empty_value text)")
            app.commit()
        finally:
            app.close()
        try:
            rejected = self.connect(database=self.database, user=self.role, password=self.passwords[2])
        except self.psycopg2.OperationalError as exc:
            if "password authentication failed" not in str(exc):
                raise RuntimeError(f"bad-password check failed for another reason: {exc}") from None
        else:
            rejected.close()
            raise RuntimeError("SCRAM accepted an incorrect password")
        self.record("scram_application_role", True, "non-superuser login accepted correct password and rejected incorrect password; host rules require SCRAM")

    def fixture_environment(self, **extra):
        common = f"host=127.0.0.1 port={self.port} dbname={self.database} user={self.role} sslmode=disable connect_timeout=3 password="
        return {**self.env, "ZAN_PG_ACCEPTANCE_CONNINFO": common + self.passwords[1],
                "ZAN_PG_ACCEPTANCE_BAD_CONNINFO": common + self.passwords[2], **extra,
                "ZAN_RT_HARD": "1"}

    def run_fixture(self, executable):
        result = self.run([executable], env=self.fixture_environment(), timeout=self.args.fixture_timeout, check=False, stage="zan_fixture_execute")
        self.log("fixture.stdout", result.stdout)
        self.log("fixture.stderr", result.stderr)
        # Preserve individual failures even when the fixture returned nonzero.
        for item in fixture_records(result.stdout):
            self.record(item["case"], item["ok"], item["detail"], source="zan")
        runtime_failure = re.search(r"memory leak detected|runtime error:|Unhandled exception|ARC guard", result.stdout + result.stderr, re.I)
        self.record("fixture_process", result.returncode == 0 and runtime_failure is None,
                    f"exit_code={result.returncode}; leak/runtime diagnostic={bool(runtime_failure)}")

    def launch_child(self, executable, stage, env):
        self.current_stage = stage
        stdout = tempfile.TemporaryFile(mode="w+b", dir=self.scratch)
        stderr = tempfile.TemporaryFile(mode="w+b", dir=self.scratch)
        try:
            process = self.start_owned_process([executable], env=env,
                                       stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr)
            child = {"process": process, "stdout": stdout, "stderr": stderr,
                     "stage": stage, "started": time.monotonic(), "collected": False}
            self.children.append(child)
            self.command_event("child_start", stage=stage, pid=process.pid)
            return child
        except BaseException as error:
            if 'process' in locals():
                try:
                    self.terminate_tree(process, stage)
                    process.communicate(timeout=5)
                except BaseException as cleanup_error:
                    error.add_note(f"child launch cleanup failed: {cleanup_error!r}")
            stdout.close()
            stderr.close()
            raise

    def collect_child(self, child, *, kill=False):
        if child["collected"]:
            raise RuntimeError("child output already collected")
        process = child["process"]
        outputs = []
        try:
            if kill:
                self.terminate_tree(process, child["stage"])
            process.communicate(timeout=5)
        except BaseException as error:
            try:
                self.terminate_tree(process, child["stage"])
                process.communicate(timeout=5)
            except BaseException as cleanup_error:
                error.add_note(f"child cleanup failed: {cleanup_error!r}")
            raise
        finally:
            try:
                # Covers exited parents whose descendants still own redirected file handles.
                self.terminate_tree(process, child["stage"])
            finally:
                for key in ("stdout", "stderr"):
                    stream = child[key]
                    try:
                        stream.seek(0)
                        text = stream.read().decode("utf-8", "replace")
                        outputs.append(text)
                        self.log(child["stage"] + "." + key, text)
                    finally:
                        stream.close()
                child["collected"] = True
        elapsed = time.monotonic() - child["started"]
        self.command_event("child_end", stage=child["stage"], pid=process.pid,
                           exit_code=process.returncode, killed=kill, elapsed_seconds=round(elapsed, 3))
        return process.returncode, outputs[0], outputs[1], elapsed

    def validate_child(self, child, expected):
        code, stdout, stderr, elapsed = self.collect_child(child)
        items = fixture_records(stdout, expected)
        for item in items:
            extra = {key: value for key, value in item.items() if key not in ("case", "ok", "detail")}
            self.record(item["case"], item["ok"], item["detail"], source="zan", stage=child["stage"], **extra)
        runtime_failure = re.search(r"memory leak detected|runtime error:|Unhandled exception|ARC guard", stdout + stderr, re.I)
        if code != 0 or runtime_failure:
            raise RuntimeError(f"stage={child['stage']}: exit={code}, runtime/leak diagnostic={bool(runtime_failure)}")
        if not all(item["ok"] for item in items):
            raise RuntimeError(f"stage={child['stage']}: fixture contract failed")
        return items, elapsed

    def run_stress(self, executable):
        workers, loops = self.args.stress_workers, self.args.stress_loops
        observer = self.connect(database=self.database, user=self.role, password=self.passwords[1])
        try:
            with observer.cursor() as cursor:
                cursor.execute("CREATE TABLE acceptance_stress (worker_id integer NOT NULL, iteration integer NOT NULL, payload text NOT NULL, nullable_value text, empty_value text, PRIMARY KEY(worker_id,iteration))")
            observer.commit()
        finally:
            observer.close()
        self.record("stress_configuration", True, "each worker is an independent Zan process and owns its connection/pool; shared-pool thread safety is not tested",
                    workers=workers, loops_per_worker=loops, expected_per_worker=stress_counts(loops),
                    total_attempted=workers * loops, deadline_seconds=self.args.stress_timeout)
        children = []
        directory = self.scratch / "stress-checkpoints"
        directory.mkdir()
        deadline = time.monotonic() + self.args.stress_timeout
        try:
            for worker in range(workers):
                env = self.fixture_environment(ZAN_PG_ACCEPTANCE_MODE="stress",
                    ZAN_PG_ACCEPTANCE_WORKER=str(worker), ZAN_PG_ACCEPTANCE_LOOPS=str(loops),
                    ZAN_PG_ACCEPTANCE_CHECKPOINT=str(directory))
                children.append((worker, self.launch_child(executable, f"stress_worker_{worker}", env)))
            for worker, child in children:
                self.wait_checkpoint(child, directory, f"ready_{worker}", min(deadline, time.monotonic() + 30))
            (directory / "go").write_text("go", encoding="ascii")
            self.record("stress_start_barrier", True, "all requested Zan worker processes were alive at a shared start barrier",
                        worker_pids=[child["process"].pid for _, child in children])
            while any(child["process"].poll() is None for _, child in children):
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"stress workers exceeded shared {self.args.stress_timeout}s deadline")
                time.sleep(0.05)
            for worker, child in children:
                items, elapsed = self.validate_child(child, ("stress_worker",))
                validate_stress_worker(items[0], worker, loops)
                self.record(f"stress_worker_{worker}_counts", True, "exact transaction counters match requested loops",
                            worker=worker, **stress_counts(loops), elapsed_seconds=round(elapsed, 3))
        finally:
            for _, child in children:
                if not child["collected"]:
                    self.collect_child(child, kill=True)
        observer = self.connect(database=self.database)
        try:
            with observer.cursor() as cursor:
                cursor.execute("SELECT worker_id,iteration,payload,nullable_value,empty_value FROM acceptance_stress ORDER BY worker_id,iteration")
                actual = cursor.fetchall()
                expected = [(worker, iteration, PAYLOAD, None, "") for worker in range(workers)
                            for iteration in range(loops) if iteration % 3 != 0]
                if actual != expected:
                    raise RuntimeError(f"stress persistence differs: expected {len(expected)} rows, actual {len(actual)}; exact worker/iteration/payload/NULL/empty rows must match")
                cursor.execute("SELECT worker_id,count(*) FROM acceptance_stress GROUP BY worker_id ORDER BY worker_id")
                counts = cursor.fetchall()
        finally:
            observer.close()
        self.record("stress_independent_persistence", True, "psycopg2 independently matched the exact committed row set and absence of every rolled-back key",
                    workers=workers, loops_per_worker=loops, committed_rows=len(expected),
                    rolled_back_transactions=workers * stress_counts(loops)["rolled_back"], per_worker_rows=counts)

    def wait_checkpoint(self, child, directory, name, deadline):
        self.current_stage = "restart_wait_" + name
        self.command_event("checkpoint_wait", stage=self.current_stage, pid=child["process"].pid)
        while True:
            checkpoint = directory / name
            if checkpoint.is_file() and checkpoint.stat().st_size > 0:
                break
            if child["process"].poll() is not None:
                code, _, stderr, _ = self.collect_child(child)
                raise RuntimeError(f"restart child exited {code} before checkpoint {name}: {stderr}")
            if time.monotonic() >= deadline:
                raise RuntimeError(f"restart checkpoint {name} exceeded overall {self.args.restart_timeout}s deadline")
            time.sleep(0.025)
        self.command_event("checkpoint_reached", stage=self.current_stage, pid=child["process"].pid)

    def run_restart(self, executable):
        observer = self.connect(database=self.database, user=self.role, password=self.passwords[1])
        try:
            with observer.cursor() as cursor:
                cursor.execute("CREATE TABLE acceptance_restart (id integer PRIMARY KEY, payload text NOT NULL)")
            observer.commit()
        finally:
            observer.close()
        directory = self.scratch / "restart-checkpoints"
        directory.mkdir()
        child = self.launch_child(executable, "restart_same_pool",
            self.fixture_environment(ZAN_PG_ACCEPTANCE_MODE="restart", ZAN_PG_ACCEPTANCE_CHECKPOINT=str(directory)))
        deadline = time.monotonic() + self.args.restart_timeout
        try:
            self.wait_checkpoint(child, directory, "ready", deadline)
            backend = int((directory / "ready").read_text(encoding="utf-8").strip())
            (directory / "begin_query").write_text("go", encoding="ascii")
            # Do not stop until the exact backend is inside pg_sleep, proving an in-flight interruption.
            monitor = self.connect(database=self.database)
            observed = False
            query_deadline = min(deadline, time.monotonic() + 5)
            try:
                monitor.autocommit = True
                with monitor.cursor() as cursor:
                    while time.monotonic() < query_deadline:
                        cursor.execute("SELECT state,query FROM pg_stat_activity WHERE pid=%s AND usename=%s", (backend, self.role))
                        row = cursor.fetchone()
                        if row and row[0] == "active" and row[1] == "SELECT pg_sleep(30)":
                            observed = True
                            break
                        time.sleep(0.025)
            finally:
                monitor.close()
            if not observed:
                raise RuntimeError("restart backend did not enter the expected pg_sleep within 5 seconds")
            self.record("restart_inflight_observed", True, "psycopg2 observed the exact pooled backend executing pg_sleep before shutdown",
                        zan_pid=child["process"].pid, backend_pid=backend)
            self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data", "-m", "fast", "-w", "-t", "15", "stop"],
                     timeout=min(20, max(1, deadline - time.monotonic())), stage="restart_private_pg_stop")
            status = self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data", "status"],
                              check=False, timeout=min(5, max(1, deadline - time.monotonic())), stage="restart_private_pg_stopped_status")
            if status.returncode != 3:
                raise RuntimeError("restart did not confirm the private instance stopped")
            self.wait_checkpoint(child, directory, "interrupted", deadline)
            interrupted_ms = int((directory / "interrupted").read_text(encoding="utf-8").strip())
            if not 0 <= interrupted_ms < 20000:
                raise RuntimeError(f"restart query interruption took {interrupted_ms}ms; bound is 20000ms")
            self.record("restart_interruption_bounded", True, "in-flight query returned failure and same pool evicted the disconnected backend before restart",
                        elapsed_ms=interrupted_ms, bound_ms=20000)
            self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data", "-l", f"{self.linux_root}/server.log",
                      "-w", "-t", "20", "start"], timeout=min(25, max(1, deadline - time.monotonic())), stage="restart_private_pg_start")
            # The same Windows process is still alive, parked with the same pool instance.
            if child["process"].poll() is not None:
                raise RuntimeError("restart child exited before server restart handshake")
            (directory / "restarted").write_text("ready", encoding="ascii")
            while child["process"].poll() is None:
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"restart same-pool process exceeded {self.args.restart_timeout}s deadline")
                time.sleep(0.025)
            items, elapsed = self.validate_child(child, ("restart_same_pool",))
            item = items[0]
            if (item.get("original_backend") != str(backend)
                    or item.get("replacement_backend") == str(backend)
                    or type(item.get("interrupted_ms")) is not int
                    or item["interrupted_ms"] != interrupted_ms
                    or type(item.get("reconnect_attempts")) is not int
                    or not 1 <= item["reconnect_attempts"] <= 4):
                raise RuntimeError("restart result counters/backend identity mismatch")
            observer = self.connect(database=self.database)
            try:
                with observer.cursor() as cursor:
                    cursor.execute("SELECT id,payload FROM acceptance_restart ORDER BY id")
                    if cursor.fetchall() != [(1, PAYLOAD)]:
                        raise RuntimeError("restart independent parameterized write verification failed")
            finally:
                observer.close()
            self.record("restart_independent_persistence", True, "same Zan process and same pool reconnected after stop/start; independent psycopg2 matched exact recovery write",
                        zan_pid=child["process"].pid, elapsed_seconds=round(elapsed, 3),
                        instance_data=f"{self.linux_root}/data", pool_scope="same process, same pool instance")
        finally:
            if not child["collected"]:
                self.collect_child(child, kill=True)

    def independent_persistence(self):
        observer = self.connect(database=self.database)
        try:
            with observer.cursor() as cursor:
                cursor.execute("SELECT id,payload,nullable_value,empty_value FROM acceptance_records ORDER BY id")
                actual = cursor.fetchall()
                expected = [
                    (100, PAYLOAD, None, ""), (101, PAYLOAD, None, ""),
                    (201, "committed", None, None), (202, "committed", None, None),
                    (301, "before savepoint", None, None), (303, "after savepoint", None, None),
                    (311, "before savepoint", None, None), (313, "after savepoint", None, None),
                ]
                if actual != expected:
                    raise RuntimeError(f"independent persistence mismatch: expected ids {[r[0] for r in expected]}, actual ids {[r[0] for r in actual]}; payload/NULL/empty values must match too")
        finally:
            observer.close()
        self.record("independent_persistence", True, "psycopg2 independently verified exact rows, payloads, NULL/empty and absence of rolled-back rows",
                    persisted_ids=[100, 101, 201, 202, 301, 303, 311, 313])

    def no_sessions(self):
        admin = self.connect()
        try:
            admin.autocommit = True
            with admin.cursor() as cursor:
                for _ in range(10):
                    cursor.execute("SELECT count(*) FROM pg_stat_activity WHERE usename=%s", (self.role,))
                    count = cursor.fetchone()[0]
                    if count == 0:
                        break
                    time.sleep(0.1)
                if count != 0:
                    raise RuntimeError(f"fixture left {count} application sessions connected")
        finally:
            admin.close()
        self.record("application_sessions_closed", True, "server reports zero remaining fixture sessions")

    def cleanup(self):
        # Reap all bounded fixture processes before stopping the owned server/removing checkpoints.
        for child in self.children:
            if not child["collected"]:
                try:
                    self.collect_child(child, kill=True)
                    self.record("child_cleanup_" + child["stage"], True, "reaped unfinished fixture child")
                except Exception as exc:
                    self.record("child_cleanup_" + child["stage"], False, exc)
        safe_to_remove = not self.start_attempted
        if self.linux_root and self.start_attempted:
            try:
                stopped = self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data",
                                    "-m", "fast", "-w", "-t", "15", "stop"], timeout=25, check=False, stage="private_pg_ctl_stop_fast")
                self.log("stop", stopped.stdout + stopped.stderr)
                status = self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data", "status"], check=False)
                if status.returncode == 0:
                    forced = self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data",
                                       "-m", "immediate", "-w", "-t", "10", "stop"], timeout=20, check=False, stage="private_pg_ctl_stop_immediate")
                    self.log("stop_immediate", forced.stdout + forced.stderr)
                    status = self.wsl([f"{self.args.pg_bin}/pg_ctl", "-D", f"{self.linux_root}/data", "status"], check=False)
                safe_to_remove = status.returncode == 3  # pg_ctl: no server running
                self.record("private_cluster_stop", safe_to_remove,
                            "confirmed private server stopped" if safe_to_remove else status.stdout + status.stderr)
            except Exception as exc:
                self.record("private_cluster_stop", False, exc)
        if self.linux_root:
            try:
                logs = self.wsl(["sh", "-c", 'if test -f "$1/server.log"; then cat "$1/server.log"; fi', "sh", self.linux_root], check=False)
                self.log("server", logs.stdout + logs.stderr)
                if not safe_to_remove:
                    raise RuntimeError(f"could not confirm server stop; retained owned directory {self.linux_root}")
                # Only an internally generated, validated mktemp path can reach this deletion.
                self.wsl(["rm", "-rf", "--", self.linux_root])
                self.wsl(["sh", "-c", 'test ! -e "$1"', "sh", self.linux_root])
                self.record("private_cluster_cleanup", True, "removed private Linux data and password files")
            except Exception as exc:
                self.record("private_cluster_cleanup", False, exc)
        if self.scratch:
            try:
                shutil.rmtree(self.scratch)
                self.record("scratch_cleanup", True, "removed Windows scratch directory")
            except Exception as exc:
                self.record("scratch_cleanup", False, exc)

    def selected_mode(self):
        if self.args.preflight_only:
            return "preflight"
        selected = ["full"]
        if self.args.stress_workers:
            selected.append("stress")
        if self.args.restart:
            selected.append("restart")
        return "+".join(selected)

    def completeness(self):
        expected = {"dependencies", "private_cluster_init", "private_cluster_start", "windows_loopback_tcp",
                    "scram_application_role", "application_sessions_closed", "private_cluster_stop",
                    "private_cluster_cleanup", "scratch_cleanup"}
        if not self.args.preflight_only:
            expected.update(CASES)
            expected.update(("fixture_build", "fixture_process", "independent_persistence"))
        if self.args.stress_workers:
            expected.update(("stress_configuration", "stress_start_barrier", "stress_independent_persistence"))
            expected.update(f"stress_worker_{worker}_counts" for worker in range(self.args.stress_workers))
        if self.args.restart:
            expected.update(("restart_inflight_observed", "restart_interruption_bounded", "restart_same_pool", "restart_independent_persistence"))
        present = {item["case"] for item in self.results}
        missing = sorted(expected - present)
        stress_items = [item for item in self.results if item["case"] == "stress_worker"]
        worker_ids = [item.get("worker") for item in stress_items]
        workers_ok = (len(worker_ids) == self.args.stress_workers
                      and sorted(worker_ids, key=str) == sorted(range(self.args.stress_workers), key=str))
        unique_expected = all(sum(item["case"] == name for item in self.results) == 1 for name in expected)
        self.record("result_completeness", not missing and workers_ok and unique_expected,
                    "all requested fixture modes, worker results, independent observations and cleanup results must be present exactly once",
                    missing=missing, expected_stress_workers=self.args.stress_workers,
                    reported_stress_worker_ids=worker_ids, unique_expected_results=unique_expected)

    def execute(self):
        try:
            self.command_event("runner_start", mode=self.selected_mode(),
                               report=str(self.report_path), command_journal=str(self.journal_path))
            self.current_stage = "dependencies"
            self.dependencies()
            executable = None if self.args.preflight_only else self.compile_fixture()
            self.initialize()
            self.start()
            self.current_stage = "windows_loopback_tcp"
            self.check_loopback()
            self.current_stage = "database_scram_setup"
            self.setup_database()
            if not self.args.preflight_only:
                self.run_fixture(executable)
                self.current_stage = "independent_persistence"
                self.independent_persistence()
                if self.args.stress_workers:
                    self.current_stage = "stress"
                    self.run_stress(executable)
                if self.args.restart:
                    self.current_stage = "restart"
                    self.run_restart(executable)
            self.current_stage = "application_sessions_closed"
            self.no_sessions()
        except (Exception, KeyboardInterrupt) as exc:
            self.record("runner", False, str(exc) or "interrupted", stage=self.current_stage)
        finally:
            self.cleanup()
        self.completeness()
        ok = bool(self.results) and all(item["ok"] for item in self.results)
        report = {"suite": "postgres_acceptance", "mode": self.selected_mode(),
                  "args": {"stress_workers": self.args.stress_workers, "stress_loops": self.args.stress_loops,
                           "restart": self.args.restart, "stress_timeout": self.args.stress_timeout,
                           "restart_timeout": self.args.restart_timeout, "fixture_timeout": self.args.fixture_timeout,
                           "skip_build": self.args.skip_build},
                  "run_id": self.run_id, "command_journal": str(self.journal_path),
                  "last_command_stage": self.current_stage,
                  "ok": ok, "zan_executed": any(item.get("source") == "zan" for item in self.results),
                  "results": self.results}
        report_text = self.redact(json.dumps(report, ensure_ascii=False, indent=2)) + "\n"
        self.report_path.write_text(report_text, encoding="utf-8")
        # Retain the stable discovery path while preserving every run's evidence.
        self.latest_report.write_text(report_text, encoding="utf-8")
        print(json.dumps({"suite": "postgres_acceptance", "mode": report["mode"], "ok": ok,
                          "zan_executed": report["zan_executed"], "report": str(self.report_path),
                          "command_journal": str(self.journal_path)}), flush=True)
        return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--distro", default="Ubuntu")
    parser.add_argument("--wsl-user", default="qq")
    parser.add_argument("--pg-bin", default="/usr/lib/postgresql/16/bin")
    parser.add_argument("--compiler", help="zanc.exe path; default build/zanc.exe")
    parser.add_argument("--skip-build", action="store_true", help="run an already-built fixture")
    parser.add_argument("--preflight-only", action="store_true", help="test private PG lifecycle/TCP/SCRAM without compiling or running Zan")
    parser.add_argument("--stress-workers", type=int, default=0, help="independent Zan processes (0 disables; maximum 8)")
    parser.add_argument("--stress-loops", type=int, default=100, help="transactions per stress process (maximum 10000)")
    parser.add_argument("--stress-timeout", type=int, default=180, help="shared finite deadline for all stress workers")
    parser.add_argument("--restart", action="store_true", help="interrupt an active query, restart the same private instance, recover in the same Zan process/pool")
    parser.add_argument("--restart-timeout", type=int, default=90, help="finite overall deadline for same-process/pool restart handshake")
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--fixture-timeout", type=int, default=120)
    parser.add_argument("--wsl-start-timeout", type=int, default=120, help="finite timeout for first WSL id probe/cold startup")
    args = parser.parse_args()
    if min(args.compile_timeout, args.fixture_timeout, args.wsl_start_timeout, args.stress_timeout, args.restart_timeout) <= 0:
        parser.error("timeouts must be positive")
    if not 0 <= args.stress_workers <= 8 or not 1 <= args.stress_loops <= 10000:
        parser.error("stress bounds: workers 0..8, loops 1..10000")
    if args.preflight_only and (args.stress_workers or args.restart):
        parser.error("--preflight-only cannot run Zan stress/restart modes")
    if not args.pg_bin.startswith("/"):
        parser.error("--pg-bin must be an absolute Linux directory")
    return Acceptance(args).execute()


if __name__ == "__main__":
    raise SystemExit(main())

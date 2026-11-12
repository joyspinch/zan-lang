#!/usr/bin/env python3
"""Real MariaDB acceptance. Run with Python -I under the shared build gate.

    python -I tests/integration/mysql_acceptance.py --compile --distro Ubuntu

No installation, service control, existing database access, or source copying.
Exit 0 = every item passed; 1 = failure; 77 = explicit prerequisite skip.
Without --compile the existing build/cases/mysql_acceptance.exe is required.
Optional: --stress-workers 4 --stress-loops 100 --restart
Stress runs bounded concurrent workers with independent processes and pools.
All workers are spawned before waiting; no start barrier or transaction-overlap measurement.
Restart retains one Zan process and one pool across owned-server stop/start.
"""
from __future__ import annotations

import argparse
import ast
import json
import os
from pathlib import Path
import queue
import secrets
import signal
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid

CASES = (
    "native_auth", "auth_reject", "parameters", "prepared_cache",
    "commit", "rollback", "savepoint", "statement_recovery",
    "pool_cycle", "pool_fault_recovery", "pool_transaction_cleanup",
)
ROOT = Path(__file__).resolve().parents[2]

# Executed only by the explicitly selected WSL distro's isolated Python.
# The worker owns mariadbd as a direct child and stays alive until host finally.
WSL_WORKER = r'''
import json, os, pathlib, secrets, shutil, signal, socket, subprocess, sys, time, uuid
owned = None
server = None
handle = None
worker_secrets = []

def scrub(value):
    if isinstance(value, str):
        for secret in worker_secrets:
            if secret:
                value = value.replace(secret, '[redacted]')
        return value
    if isinstance(value, dict):
        return {scrub(key): scrub(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [scrub(item) for item in value]
    return value

def emit(value):
    # Includes bootstrap stderr and server logs, before anything crosses WSL stdout.
    print(json.dumps(scrub(value)), flush=True)

def stop():
    global server, handle
    if server is not None:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=20)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=10)
        server = None
    if handle is not None:
        handle.close()
        handle = None

def interrupted(signum, frame):
    raise KeyboardInterrupt('worker signal')

signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)
try:
    required = ['mariadb-install-db', 'mariadb', 'mariadb-admin']
    missing = [name for name in required if shutil.which(name) is None]
    if not os.access('/usr/sbin/mariadbd', os.X_OK):
        missing.append('/usr/sbin/mariadbd')
    if missing:
        emit({'status': 'skip', 'reason': 'missing WSL tools: ' + ', '.join(missing)})
    else:
        config = json.loads(sys.stdin.readline())
        worker_secrets.extend([config['user'], config['password']])
        # mkdir is exclusive; cleanup ownership is published only after it succeeds.
        candidate = pathlib.Path('/tmp') / ('zan-mysql-' + str(uuid.uuid4()))
        candidate.mkdir(mode=0o700)
        owned = candidate
        datadir = owned / 'data'
        socket_path = str(owned / 'mysql.sock')
        emit({'status': 'stage', 'stage': 'initialize_owned_datadir', 'owned_datadir': str(owned)})
        result = subprocess.run([
            shutil.which('mariadb-install-db'), '--no-defaults',
            '--datadir=' + str(datadir), '--auth-root-authentication-method=normal',
            '--skip-test-db'], capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError('mariadb-install-db: ' + result.stdout + result.stderr)
        root_password = secrets.token_hex(24)
        worker_secrets.append(root_password)
        client_config = owned / 'client.cnf'
        client_config.write_text('[client]\nuser=root\npassword=' + root_password +
                                 '\nprotocol=socket\nsocket=' + socket_path + '\n')
        os.chmod(client_config, 0o600)

        def sql(text, authenticated=True):
            argv = [shutil.which('mariadb')]
            if authenticated:
                argv += ['--defaults-file=' + str(client_config)]
            else:
                argv += ['--no-defaults', '--protocol=socket', '--socket=' + socket_path, '-uroot']
            return subprocess.run(argv + ['--batch', '--skip-column-names'],
                                  input=text, capture_output=True, text=True, timeout=8)

        def start(port=None, authenticated=True):
            global server, handle
            handle = (owned / 'server.log').open('a')
            argv = ['/usr/sbin/mariadbd', '--no-defaults', '--datadir=' + str(datadir),
                    '--socket=' + socket_path, '--pid-file=' + str(owned / 'mysql.pid'),
                    '--log-error=' + str(owned / 'error.log'), '--skip-log-bin',
                    '--skip-name-resolve', '--max-connections=16', '--innodb-buffer-pool-size=32M',
                    '--character-set-server=utf8mb4', '--collation-server=utf8mb4_unicode_ci']
            if os.getuid() == 0:
                argv += ['--user=root']
            if port is None:
                argv += ['--skip-networking']
            else:
                argv += ['--bind-address=127.0.0.1', '--port=' + str(port)]
            server = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=handle, stderr=handle)
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError('owned mariadbd exited during startup')
                if os.path.exists(socket_path):
                    try:
                        probe = sql('SELECT 1;', authenticated)
                        if probe.returncode == 0 and probe.stdout.strip() == '1':
                            return
                    except subprocess.TimeoutExpired:
                        pass
                time.sleep(0.15)
            raise TimeoutError('owned mariadbd readiness timeout')

        # Uncredentialed bootstrap is socket-only inside our mode-0700 directory.
        emit({'status': 'stage', 'stage': 'bootstrap_socket_only'})
        start(authenticated=False)
        user, password = config['user'], config['password']
        # Host generated hex tokens only; they cannot introduce SQL syntax.
        if not user.startswith('zan_') or any(c not in '0123456789abcdef' for c in user[4:] + password):
            raise ValueError('invalid generated credentials')
        bootstrap = ("CREATE DATABASE zan_acceptance CHARACTER SET utf8mb4;"
            "CREATE USER '" + user + "'@'127.0.0.1' IDENTIFIED VIA mysql_native_password USING PASSWORD('" + password + "');"
            "GRANT ALL ON zan_acceptance.* TO '" + user + "'@'127.0.0.1';"
            "ALTER USER 'root'@'localhost' IDENTIFIED BY '" + root_password + "';")
        result = sql(bootstrap, False)
        if result.returncode:
            raise RuntimeError('isolated user provisioning failed: ' + result.stderr)
        result = sql("SELECT plugin FROM mysql.user WHERE User='" + user + "' AND Host='127.0.0.1';")
        if result.returncode or result.stdout.strip() != 'mysql_native_password':
            raise RuntimeError('native authentication plugin was not provisioned')
        stop()
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        emit({'status': 'stage', 'stage': 'start_owned_loopback', 'port': port})
        start(port)
        native_config = owned / 'native.cnf'
        native_config.write_text('[client]\nuser=' + user + '\npassword=' + password +
            '\nprotocol=tcp\nhost=127.0.0.1\nport=' + str(port) + '\ndatabase=zan_acceptance\n')
        os.chmod(native_config, 0o600)
        native = subprocess.run([shutil.which('mariadb'), '--defaults-file=' + str(native_config),
            '--batch', '--skip-column-names'], input='SELECT VERSION(), CURRENT_USER(), 42;',
            capture_output=True, text=True, timeout=8)
        fields = native.stdout.strip().split('\t')
        if native.returncode or len(fields) != 3 or fields[1:] != [user + '@127.0.0.1', '42']:
            raise RuntimeError('native TCP CLI verification failed: ' + native.stderr)
        emit({'status': 'ready', 'port': port, 'datadir': str(owned),
              'auth_plugin': 'mysql_native_password', 'version': fields[0],
              'native_cli_verified': True})
        def native_sql(text):
            result = subprocess.run([shutil.which('mariadb'), '--defaults-file=' + str(native_config),
                '--default-character-set=utf8mb4', '--batch', '--raw', '--skip-column-names'], input=text, capture_output=True,
                text=True, encoding='utf-8', timeout=15)
            if result.returncode:
                raise RuntimeError('owned native CLI command failed: ' + result.stderr)
            return result.stdout.strip()

        while True:
            line = sys.stdin.readline()
            if not line:
                break
            request = json.loads(line)
            operation = request.get('op')
            if operation == 'stop':
                break
            if operation == 'stress_setup':
                native_sql('CREATE TABLE acceptance_stress (worker INT NOT NULL, iteration INT NOT NULL, '
                    'amount INT NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(worker, iteration)) '
                    'ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;')
                emit({'status': 'ok', 'op': operation})
            elif operation == 'stress_verify':
                workers, loops = request['workers'], request['loops']
                if type(workers) is not int or type(loops) is not int or not 1 <= workers <= 16 or not 1 <= loops <= 10000:
                    raise ValueError('invalid stress verification bounds')
                output = native_sql('SELECT worker, iteration, amount, HEX(payload) '
                    'FROM acceptance_stress ORDER BY worker, iteration;')
                actual = [line.split('\t') for line in output.splitlines()]
                expected = []
                for worker_id in range(workers):
                    for iteration in range(0, loops, 2):
                        payload = "压力 café 😀 '); -- worker=" + str(worker_id) + ';iteration=' + str(iteration)
                        expected.append([str(worker_id), str(iteration), str(worker_id * 100000 + iteration),
                                         payload.encode('utf-8').hex().upper()])
                if actual != expected:
                    first = next((i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1]),
                                 min(len(actual), len(expected)))
                    raise AssertionError('native CLI stress rows differ: actual=' + str(len(actual)) +
                        ', expected=' + str(len(expected)) + ', first_mismatch=' + str(first))
                emit({'status': 'ok', 'op': operation, 'rows': len(actual),
                      'commits': workers * ((loops + 1) // 2), 'rollbacks': workers * (loops // 2),
                      'amount_sum': sum(int(row[2]) for row in actual),
                      'verified': 'every primary key, amount and exact UTF-8 payload via native CLI'})
            elif operation == 'restart_setup':
                native_sql('CREATE TABLE acceptance_restart (id INT PRIMARY KEY, payload TEXT NOT NULL) '
                    'ENGINE=InnoDB DEFAULT CHARSET=utf8mb4; '
                    "INSERT INTO acceptance_restart VALUES (1, '重启 café 😀');")
                emit({'status': 'ok', 'op': operation})
            elif operation == 'server_down':
                stop()
                emit({'status': 'ok', 'op': operation, 'owned_server_stopped': True})
            elif operation == 'server_up':
                start(port)
                identity = native_sql('SELECT CURRENT_USER();')
                if identity != user + '@127.0.0.1':
                    raise AssertionError('native reauthentication identity mismatch')
                emit({'status': 'ok', 'op': operation, 'port': port, 'native_cli_verified': True})
            elif operation == 'restart_verify':
                actual = native_sql('SELECT id, HEX(payload) FROM acceptance_restart ORDER BY id;')
                if actual != '1\t' + '重启 café 😀'.encode('utf-8').hex().upper():
                    raise AssertionError('native CLI restart persistence mismatch')
                emit({'status': 'ok', 'op': operation, 'rows': 1, 'native_cli_verified': True})
            else:
                raise ValueError('unknown worker command')
except BaseException as exc:
    # Process boundary: preserve failure and own-server diagnostics, redact secrets on host.
    detail = ''
    if owned is not None:
        for name in ('error.log', 'server.log'):
            path = owned / name
            if path.exists():
                detail += path.read_text(errors='replace')[-16000:]
    emit({'status': 'fail', 'reason': str(exc), 'diagnostic': detail})
finally:
    try:
        stop()
        if owned is not None:
            shutil.rmtree(owned)
        emit({'status': 'cleaned', 'removed': str(owned) if owned else None})
    except BaseException as exc:
        emit({'status': 'cleanup_failed', 'reason': str(exc)})
        sys.exit(1)
'''


def read_event(process, timeout):
    replies = queue.Queue()

    def reader():
        try:
            replies.put(process.stdout.readline())
        except BaseException as exc:
            replies.put(exc)

    threading.Thread(target=reader, daemon=True).start()
    try:
        value = replies.get(timeout=timeout)
    except queue.Empty as exc:
        raise TimeoutError('WSL worker response timeout') from exc
    if isinstance(value, BaseException):
        raise value
    if not value:
        raise RuntimeError('WSL worker closed its output')
    return json.loads(value)


def receive_exact(peer, length, deadline):
    data = bytearray()
    while len(data) < length:
        peer.settimeout(max(0.01, deadline - time.monotonic()))
        if time.monotonic() >= deadline:
            raise TimeoutError('MariaDB greeting deadline exceeded')
        part = peer.recv(length - len(data))
        if not part:
            raise EOFError('MariaDB closed before complete greeting')
        data.extend(part)
    return bytes(data)


def verify_loopback(port, sql_version):
    deadline = time.monotonic() + 30
    while True:
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=5) as peer:
                header = receive_exact(peer, 4, deadline)
                length = int.from_bytes(header[:3], 'little')
                if header[3] != 0 or not 20 <= length <= 4096:
                    raise ValueError('invalid greeting packet header: ' + header.hex())
                payload = receive_exact(peer, length, deadline)
                terminator = payload.find(b'\x00', 1)
                if payload[0] != 10 or terminator <= 1 or terminator + 14 > length:
                    raise ValueError('invalid protocol-10 MariaDB greeting')
                wire_version = payload[1:terminator].decode('ascii')
                allowed = [sql_version]
                if 'MariaDB' in sql_version:
                    allowed.append('5.5.5-' + sql_version)
                if wire_version not in allowed:
                    raise ValueError('Windows loopback server greeting mismatch: wire=' +
                                     repr(wire_version) + ', SQL=' + repr(sql_version))
                return wire_version
        except (ConnectionError, TimeoutError, EOFError):
            # WSL localhost forwarding may become available shortly after Linux CLI readiness.
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)


def worker_command(worker, operation, timeout=45, **parameters):
    worker.stdin.write(json.dumps(dict(op=operation, **parameters)) + '\n')
    worker.stdin.flush()
    response = read_event(worker, timeout)
    if response.get('status') != 'ok' or response.get('op') != operation:
        raise RuntimeError('owned worker command ' + operation + ' failed: ' + json.dumps(response))
    return response


def scrub_report(value, credentials):
    if isinstance(value, str):
        for secret in credentials:
            if secret:
                value = value.replace(secret, '[redacted]')
        return value
    if isinstance(value, dict):
        return {scrub_report(key, credentials): scrub_report(item, credentials)
                for key, item in value.items()}
    if isinstance(value, (tuple, list)):
        return [scrub_report(item, credentials) for item in value]
    return value


class WindowsJob:
    """Own descendants even if the initial process exits before pipe EOF."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        class BasicLimits(ctypes.Structure):
            _fields_ = [('ProcessTime', ctypes.c_longlong), ('JobTime', ctypes.c_longlong),
                ('Flags', wintypes.DWORD), ('MinWorkingSet', ctypes.c_size_t),
                ('MaxWorkingSet', ctypes.c_size_t), ('ActiveProcesses', wintypes.DWORD),
                ('Affinity', ctypes.c_size_t), ('Priority', wintypes.DWORD), ('Scheduling', wintypes.DWORD)]
        class IoCounters(ctypes.Structure):
            _fields_ = [(name, ctypes.c_ulonglong) for name in
                ('ReadOps', 'WriteOps', 'OtherOps', 'ReadBytes', 'WriteBytes', 'OtherBytes')]
        class ExtendedLimits(ctypes.Structure):
            _fields_ = [('Basic', BasicLimits), ('Io', IoCounters),
                ('ProcessMemory', ctypes.c_size_t), ('JobMemory', ctypes.c_size_t),
                ('PeakProcessMemory', ctypes.c_size_t), ('PeakJobMemory', ctypes.c_size_t)]
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.kernel.SetInformationJobObject.restype = wintypes.BOOL
        self.kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.kernel.AssignProcessToJobObject.restype = wintypes.BOOL
        self.kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        self.kernel.TerminateJobObject.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.handle = self.kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedLimits()
        limits.Basic.Flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)

    def assign_and_resume(self, process):
        if not self.kernel.AssignProcessToJobObject(self.handle, int(process._handle)):
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        ntdll = self.ctypes.WinDLL('ntdll')
        ntdll.NtResumeProcess.argtypes = [self.ctypes.c_void_p]
        ntdll.NtResumeProcess.restype = self.ctypes.c_long
        status = ntdll.NtResumeProcess(int(process._handle))
        if status < 0:
            raise OSError('NtResumeProcess failed: ' + str(status))

    def terminate(self):
        if self.handle and not self.kernel.TerminateJobObject(self.handle, 1):
            raise self.ctypes.WinError(self.ctypes.get_last_error())

    def close(self):
        if self.handle:
            if not self.kernel.CloseHandle(self.handle):
                raise self.ctypes.WinError(self.ctypes.get_last_error())
            self.handle = None


def owned_popen(command, **kwargs):
    job = WindowsJob() if os.name == 'nt' else None
    process = None
    try:
        if job:
            kwargs['creationflags'] = subprocess.CREATE_NEW_PROCESS_GROUP | 0x4  # CREATE_SUSPENDED
        else:
            kwargs['start_new_session'] = True
        process = subprocess.Popen(command, **kwargs)
        process._zan_job = job
        if job:
            job.assign_and_resume(process)
        return process
    except BaseException:
        if process is not None:
            process.kill()  # Assignment may have failed while the child is still suspended.
            process.wait(timeout=20)
        if job:
            job.close()
        raise


def close_process_tree(process):
    job = getattr(process, '_zan_job', None)
    if job:
        job.close()  # Kills every remaining descendant, including parent-exited cases.
    elif os.name != 'nt':
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The owned session has already exited.


def kill_process_tree(process):
    job = getattr(process, '_zan_job', None)
    if job:
        job.terminate()
    elif os.name == 'nt':
        raise RuntimeError('owned process is missing its Windows Job Object')
    else:
        close_process_tree(process)


def compile_fixture(command, cwd, timeout, env=None):
    # Also used for leaf fixtures: every timed-out compiler/fixture owns a group.
    process = owned_popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        kill_process_tree(process)
        stdout, stderr = process.communicate(timeout=20)
    except BaseException:
        # Cleanup before propagating an interrupted harness operation.
        kill_process_tree(process)
        process.communicate(timeout=20)
        raise
    finally:
        close_process_tree(process)
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr), timed_out


def fatal_runtime_diagnostic(text):
    normalized = text.lower().replace('_', ' ').replace('-', ' ')
    return any(marker in normalized for marker in (
        'memory leak detected', 'runtime error', 'arc guard', 'unhandled exception'))


def fixture_result(raw, returncode, name):
    text = raw.decode('utf-8', errors='replace')
    events = []
    for line in text.splitlines():
        try:
            event = json.loads(line)
            if isinstance(event, dict) and event.get('case') == name:
                events.append(event)
        except ValueError:
            pass  # Keep non-JSON runtime diagnostics in the process log.
    passed = (returncode == 0 and len(events) == 1 and events[0].get('status') == 'pass'
              and not fatal_runtime_diagnostic(text))
    return passed, events


def wait_checkpoint(process, root, name, timeout):
    deadline = time.monotonic() + timeout
    while not (root / (name + '.ready')).is_file():
        if process.poll() is not None:
            raise RuntimeError('restart fixture exited before checkpoint ' + name)
        if time.monotonic() >= deadline:
            raise TimeoutError('restart checkpoint deadline: ' + name)
        time.sleep(0.02)


def run_restart(exe, environment, worker, checkpoint, output, timeout, version, port, record, redact):
    logfile = output / 'mysql_acceptance.restart_same_pool.log'
    process = None
    success = False
    stage_error = None
    started = time.monotonic()
    try:
        worker_command(worker, 'restart_setup')
        with logfile.open('wb') as stream:
            process = owned_popen([str(exe)], cwd=ROOT,
                env=dict(environment, ZAN_RT_HARD='1', ZAN_MYSQL_CASE='restart_same_pool',
                    ZAN_MYSQL_CHECKPOINT=str(checkpoint), ZAN_MYSQL_CHECKPOINT_MS=str(timeout * 1000)),
                stdout=stream, stderr=subprocess.STDOUT)
        wait_checkpoint(process, checkpoint, 'connected', timeout)
        stopped = worker_command(worker, 'server_down', timeout=45)
        record('restart_down', 'pass', owned_server=stopped, fixture_pid=process.pid)
        (checkpoint / 'connected.go').write_text('go', encoding='ascii')
        wait_checkpoint(process, checkpoint, 'unavailable', timeout)
        record('restart_unavailable', 'pass', fixture_pid=process.pid,
               evidence='same pool observed dead lease and failed physical connection before checkpoint')
        resumed = worker_command(worker, 'server_up', timeout=60)
        wire = verify_loopback(port, version)
        record('restart_up', 'pass', owned_server=resumed, wire_version=wire, fixture_pid=process.pid)
        (checkpoint / 'unavailable.go').write_text('go', encoding='ascii')
        process.wait(timeout=timeout)
        success = True
    except Exception as exc:
        # Harness boundary: preserve the checkpoint failure, then reap our process.
        stage_error = str(exc)
    finally:
        if process is not None and process.poll() is None:
            kill_process_tree(process)
        if process is not None:
            try:
                process.wait(timeout=20)
            finally:
                close_process_tree(process)
        raw = logfile.read_bytes() if logfile.exists() else b''
        logfile.write_text(redact(raw.decode('utf-8', errors='replace')), encoding='utf-8')
        passed, events = fixture_result(raw, process.returncode if process else -1, 'restart_same_pool')
        passed = (success and passed and events[0].get('process_id') == process.pid
                  and events[0].get('recovery_scope') == 'same_process_same_pool_new_physical_connection')
        record('restart_same_pool', 'pass' if passed else 'fail', fixture=events,
               fixture_pid=process.pid if process else None, reason=stage_error, log=logfile.name,
               elapsed_seconds=round(time.monotonic() - started, 3),
               recovery_scope='same_process_same_pool_new_physical_connection')
    if success:
        verified = worker_command(worker, 'restart_verify')
        record('restart_native_verify', 'pass', verification=verified)
    return success and passed


def run_stress(exe, environment, workers, loops, output, timeout, record, redact):
    processes = []
    started = time.monotonic()
    try:
        # Start every independent process before waiting for any of them.
        for worker_id in range(workers):
            path = output / ('mysql_acceptance.stress_worker_' + str(worker_id) + '.log')
            stream = path.open('wb')
            try:
                process = owned_popen([str(exe)], cwd=ROOT,
                    env=dict(environment, ZAN_RT_HARD='1', ZAN_MYSQL_CASE='stress',
                        ZAN_MYSQL_STRESS_WORKER=str(worker_id), ZAN_MYSQL_STRESS_LOOPS=str(loops)),
                    stdout=stream, stderr=subprocess.STDOUT)
            finally:
                stream.close()  # Popen owns the inherited output handle.
            processes.append((worker_id, process, path))
        deadline = started + timeout
        for _, process, _ in processes:
            try:
                process.wait(timeout=max(0.01, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                break  # Finally terminates and reaps every still-running owned process.
    finally:
        for _, process, _ in processes:
            if process.poll() is None:
                kill_process_tree(process)
        for worker_id, process, path in processes:
            try:
                process.wait(timeout=10)
            finally:
                close_process_tree(process)
            raw = path.read_bytes()
            path.write_text(redact(raw.decode('utf-8', errors='replace')), encoding='utf-8')
            passed, events = fixture_result(raw, process.returncode, 'stress')
            expected = dict(worker=worker_id, loops=loops, commits=(loops + 1) // 2, rollbacks=loops // 2)
            passed = passed and all(events[0].get(key) == value for key, value in expected.items())
            record('stress_worker_' + str(worker_id), 'pass' if passed else 'fail',
                exit_code=process.returncode, fixture=events, expected=expected, log=path.name,
                concurrency_scope='independent_process_and_pool',
                scheduling='bounded_concurrent_workers', start_barrier=False,
                transaction_overlap='not_measured',
                batch_elapsed_seconds=round(time.monotonic() - started, 3), timeout_seconds=timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--distro', default='Ubuntu')
    parser.add_argument('--compile', action='store_true')
    parser.add_argument('--case-timeout', type=int, default=60)
    parser.add_argument('--case', dest='selected_cases', action='append', choices=CASES,
                        help='run only this fixture case; repeat option to select more than one')
    parser.add_argument('--case-repeats', type=int, default=1,
                        help='bounded repetitions per selected case (1..16; requires --case when >1)')
    parser.add_argument('--stress-workers', type=int, default=0,
                        help='optional independent Zan processes, each with its own pool (1..16; default disabled)')
    parser.add_argument('--stress-loops', type=int, default=100,
                        help='transactions per stress worker, alternating commit/rollback (1..10000)')
    parser.add_argument('--stress-timeout', type=int, default=300,
                        help='wall-clock deadline for the entire concurrent stress batch')
    parser.add_argument('--restart', action='store_true',
                        help='stop/restart only the owned server; retain the same Zan process and pool across restart')
    parser.add_argument('--preflight-only', action='store_true',
                        help='own-server lifecycle, native CLI login and Windows loopback handshake only; no compile or fixture')
    parser.add_argument('--static-check', action='store_true',
                        help='parse this runner and embedded worker only; no WSL or compiler')
    options = parser.parse_args()
    if not 1 <= options.case_repeats <= 16:
        parser.error('--case-repeats must be 1..16')
    if options.case_repeats > 1 and not options.selected_cases:
        parser.error('--case-repeats >1 requires --case to bound the fixture scope')
    if options.selected_cases and (options.preflight_only or options.stress_workers or options.restart):
        parser.error('--case cannot combine with preflight, stress or restart modes')
    selected_cases = tuple(dict.fromkeys(options.selected_cases or CASES))
    if options.preflight_only and (options.compile or options.stress_workers or options.restart):
        parser.error('--preflight-only cannot combine with --compile, --stress-workers or --restart')
    if not 0 <= options.stress_workers <= 16 or not 1 <= options.stress_loops <= 10000:
        parser.error('--stress-workers must be 0..16 and --stress-loops must be 1..10000')
    if options.case_timeout < 1 or options.stress_timeout < 1:
        parser.error('fixture and stress timeouts must be positive')
    if options.static_check:
        ast.parse(Path(__file__).read_text(encoding='utf-8'))
        ast.parse(WSL_WORKER)
        print(json.dumps({'status': 'pass', 'scope': 'Python syntax only'}))
        return 0
    run_id = 'mysql_acceptance-' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + '-' + uuid.uuid4().hex[:12]
    output = ROOT / 'build' / 'test-out' / run_id
    output.mkdir(parents=True, exist_ok=False)
    cases_dir = ROOT / 'build' / 'cases'
    cases_dir.mkdir(parents=True, exist_ok=True)
    report_path = output / ('mysql_acceptance.preflight.json' if options.preflight_only else 'mysql_acceptance.json')
    exe = cases_dir / 'mysql_acceptance.exe'
    records = []
    fixture_runs = [(name, name if options.case_repeats == 1 else name + '_' + str(iteration))
                    for name in selected_cases for iteration in range(1, options.case_repeats + 1)]
    expected_cases = [label for _, label in fixture_runs]
    if options.stress_workers:
        expected_cases += ['stress_worker_' + str(i) for i in range(options.stress_workers)]
        expected_cases.append('stress_native_verify')
    if options.restart:
        expected_cases += ['restart_down', 'restart_unavailable', 'restart_up',
                           'restart_same_pool', 'restart_native_verify']
        if options.stress_workers:
            expected_cases.append('stress_native_verify_after_restart')
    worker = None
    scratch = None
    skipped = False
    credentials = [secrets.token_hex(24), 'zan_' + secrets.token_hex(8)]

    def redact(text):
        return scrub_report(text, credentials)

    def record(name, status, **detail):
        entry = scrub_report(dict(case=name, status=status, **detail), credentials)
        records.append(entry)
        print(json.dumps(entry, ensure_ascii=False), flush=True)

    try:
        if not shutil.which('wsl.exe'):
            skipped = True
            record('prerequisites', 'skip', reason='wsl.exe unavailable; no service was started')
        else:
            # Probe only: no service or datadir mutation until worker is launched.
            probe = subprocess.run(['wsl.exe', '-d', options.distro, '--exec', 'sh', '-c',
                'test -x /usr/sbin/mariadbd && command -v mariadb-install-db && '
                'command -v mariadb && command -v mariadb-admin && command -v python3'],
                capture_output=True, timeout=120)
            if probe.returncode:
                skipped = True
                record('prerequisites', 'skip', reason='selected WSL distro or installed MariaDB tools unavailable',
                       diagnostic=probe.stderr.decode('utf-8', errors='replace'))
        if not skipped:
            if options.case_timeout < 1:
                raise ValueError('--case-timeout must be positive')
            if options.compile:
                scratch_root = ROOT / '_scratch'
                scratch_root.mkdir(exist_ok=True)
                scratch = Path(tempfile.mkdtemp(prefix='mysql-acceptance-', dir=scratch_root))
                command = [str(ROOT / 'build' / 'zanc.exe'),
                    str(ROOT / 'tests' / 'integration' / 'mysql_acceptance.zan'),
                    '--auto-stdlib', '--stdlib-path', str(ROOT / 'stdlib'),
                    '--package-project', str(ROOT), '-g', '--check-leaks', '--strict-runtime',
                    '-o', str(exe)]
                result, timed_out = compile_fixture(command, scratch, timeout=180)
                text = redact((result.stdout + result.stderr).decode('utf-8', errors='replace'))
                (output / 'mysql_acceptance.compile.log').write_text(text, encoding='utf-8')
                record('compile', 'pass' if result.returncode == 0 and not timed_out else 'fail',
                       exit_code=result.returncode, timed_out=timed_out)
                if timed_out or result.returncode:
                    raise RuntimeError('fixture compilation failed or timed out; see compile log')
            if not options.preflight_only and not exe.is_file():
                raise FileNotFoundError('fixture missing; use --compile under the shared build gate')
            worker = subprocess.Popen(['wsl.exe', '-d', options.distro, '--exec',
                'python3', '-I', '-u', '-c', WSL_WORKER], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8')
            worker.stdin.write(json.dumps(dict(user=credentials[1], password=credentials[0])) + '\n')
            worker.stdin.flush()
            startup_deadline = time.monotonic() + 300
            ready = read_event(worker, 300)
            while ready.get('status') == 'stage':
                record('worker_stage', 'info', **{k: v for k, v in ready.items() if k != 'status'})
                ready = read_event(worker, max(1, startup_deadline - time.monotonic()))
            if ready['status'] == 'skip':
                skipped = True
                record('service', 'skip', reason=ready['reason'])
            elif ready['status'] != 'ready':
                raise RuntimeError(redact(json.dumps(ready)))
            else:
                record('service', 'pass', port=ready['port'], owned_datadir=ready['datadir'],
                       auth_plugin=ready['auth_plugin'], version=ready['version'],
                       native_cli_verified=ready['native_cli_verified'])
                # Keep loopback-only routing and validate the entire framed greeting.
                # MariaDB prefixes its wire version with 5.5.5- for old MySQL clients.
                wire_version = verify_loopback(ready['port'], ready['version'])
                record('windows_loopback', 'pass', sql_version=ready['version'], wire_version=wire_version)
                environment = dict(os.environ, ZAN_RT_HARD='1', ZAN_MYSQL_HOST='127.0.0.1',
                    ZAN_MYSQL_PORT=str(ready['port']), ZAN_MYSQL_DATABASE='zan_acceptance',
                    ZAN_MYSQL_USER=credentials[1], ZAN_MYSQL_PASSWORD=credentials[0])
                for name, label in (() if options.preflight_only else fixture_runs):
                    environment['ZAN_MYSQL_CASE'] = name
                    try:
                        result, timed_out = compile_fixture([str(exe)], ROOT,
                            timeout=options.case_timeout, env=environment)
                        if timed_out:
                            raise subprocess.TimeoutExpired([str(exe)], options.case_timeout,
                                output=result.stdout, stderr=result.stderr)
                        text = redact((result.stdout + result.stderr).decode('utf-8', errors='replace'))
                        (output / ('mysql_acceptance.' + label + '.log')).write_text(text, encoding='utf-8')
                        events = []
                        for line in result.stdout.decode('utf-8', errors='replace').splitlines():
                            try:
                                event = json.loads(line)
                                if isinstance(event, dict) and event.get('case') == name:
                                    events.append(event)
                            except ValueError:
                                pass  # Non-report runtime diagnostics stay in the per-item log.
                        passed = (result.returncode == 0 and len(events) == 1 and
                                  events[0].get('status') == 'pass' and
                                  not fatal_runtime_diagnostic(text))
                        record(label, 'pass' if passed else 'fail', fixture_case=name, exit_code=result.returncode,
                               fixture=events, log='mysql_acceptance.' + label + '.log')
                    except subprocess.TimeoutExpired as exc:
                        text = redact(((exc.stdout or b'') + (exc.stderr or b'')).decode('utf-8', errors='replace'))
                        (output / ('mysql_acceptance.' + label + '.log')).write_text(text, encoding='utf-8')
                        record(label, 'fail', fixture_case=name, reason='fixture timeout', timeout_seconds=options.case_timeout)
                if options.stress_workers:
                    worker_command(worker, 'stress_setup')
                    run_stress(exe, environment, options.stress_workers, options.stress_loops,
                               output, options.stress_timeout, record, redact)
                    verified = worker_command(worker, 'stress_verify', workers=options.stress_workers,
                                              loops=options.stress_loops)
                    record('stress_native_verify', 'pass', verification=verified,
                           concurrency_scope='independent_process_and_pool')
                if options.restart:
                    if scratch is None:
                        scratch_root = ROOT / '_scratch'
                        scratch_root.mkdir(exist_ok=True)
                        scratch = Path(tempfile.mkdtemp(prefix='mysql-acceptance-', dir=scratch_root))
                    checkpoint = scratch / 'restart-checkpoint'
                    checkpoint.mkdir()
                    recovered = run_restart(exe, environment, worker, checkpoint, output,
                        max(options.case_timeout, 120), ready['version'], ready['port'], record, redact)
                    if not recovered:
                        raise RuntimeError('same-process same-pool restart failed; see checkpoint and fixture records')
                    if options.stress_workers:
                        verified = worker_command(worker, 'stress_verify', workers=options.stress_workers,
                                                  loops=options.stress_loops)
                        record('stress_native_verify_after_restart', 'pass', verification=verified)
    except Exception as exc:
        # Process boundary: infrastructure failure must produce a nonzero report.
        record('runner', 'fail', reason=redact(str(exc)))
    finally:
        if worker is not None:
            try:
                try:
                    if worker.poll() is None:
                        worker.stdin.write('{"op":"stop"}\n')
                        worker.stdin.flush()
                except (BrokenPipeError, OSError):
                    pass  # Worker may have completed skip/failure cleanup already.
                finally:
                    try:
                        worker.stdin.close()
                    except (BrokenPipeError, OSError):
                        pass  # EOF still reaches the owned worker when it is alive.
                # EOF also runs worker finally, even when startup or a fixture failed.
                events = []
                while True:
                    event = read_event(worker, 45)
                    events.append(event)
                    if event.get('status') in ('cleaned', 'cleanup_failed'):
                        break
                worker.wait(timeout=15)
                good = (events[-1].get('status') == 'cleaned' and worker.returncode == 0
                        and not any(e.get('status') == 'fail' for e in events))
                record('cleanup', 'pass' if good else 'fail', worker_events=events)
                diagnostic = redact(worker.stderr.read())
                (output / 'mysql_acceptance.worker.log').write_text(diagnostic, encoding='utf-8')
            except Exception as exc:
                record('cleanup', 'fail', reason=redact(str(exc)))
                # Never kill a distro or service. The worker retains ownership of its child
                # and receives EOF above; an unconfirmed cleanup cannot count as passing.
        if scratch is not None:
            try:
                shutil.rmtree(scratch)
                record('scratch_cleanup', 'pass', removed=str(scratch), confirmed_absent=not scratch.exists())
            except OSError as exc:
                record('scratch_cleanup', 'fail', reason=str(exc))
        present = {entry['case'] for entry in records}
        for name in (() if options.preflight_only else expected_cases):
            if name not in present:
                record(name, 'skip' if skipped else 'not_run', reason='prerequisite or runner did not reach fixture')
        failed = any(entry['status'] in ('fail', 'not_run') for entry in records)
        status = 'fail' if failed else ('skip' if skipped else 'pass')
        exit_code = 1 if failed else (77 if skipped else 0)
        report_path.write_text(json.dumps(dict(status=status, exit_code=exit_code,
            scope='environment_preflight' if options.preflight_only else 'mysql_package_acceptance',
            args=vars(options), restart_scope='same_process_same_pool_new_physical_connection' if options.restart else None,
            results=records), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(dict(status=status, exit_code=exit_code, report=str(report_path))), flush=True)
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())

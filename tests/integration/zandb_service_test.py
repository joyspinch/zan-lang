#!/usr/bin/env python3
"""Exercise a prebuilt ZanDB owner and real client processes; never build it.

Run from the repository with:
    python tests/integration/zandb_service_test.py --exe _scratch/zandb_server.exe
Only the Python standard library is required. All temporary files are removed.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import time

MAX_PAYLOAD = 8 * 1024 * 1024
LEAK_PREFIX = "zan: memory leak detected:"
WRITE_ACTIONS = {
    "insert", "upsert", "update", "delete", "batch", "ensure_index",
    "ensure_text_index", "ensure_vector_index", "checkpoint_search", "rebuild_search",
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False,
                      separators=(",", ":")).encode("utf-8")


def frame(value):
    payload = json_bytes(value)
    require(0 < len(payload) <= MAX_PAYLOAD, "test request exceeds framing limit")
    return struct.pack("!I", len(payload)) + payload


def receive_exact(peer, count, deadline):
    data = bytearray()
    while len(data) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("reply deadline exceeded")
        peer.settimeout(remaining)
        part = peer.recv(count - len(data))
        if not part:
            raise EOFError("owner closed during reply")
        data.extend(part)
    return bytes(data)


def receive_frame(peer, timeout=10.0):
    deadline = time.monotonic() + timeout
    length, = struct.unpack("!I", receive_exact(peer, 4, deadline))
    require(0 < length <= MAX_PAYLOAD, "invalid response length")
    value = json.loads(receive_exact(peer, length, deadline).decode("utf-8"))
    require(isinstance(value, dict) and value.get("v") == 1, "invalid reply envelope")
    require(type(value.get("ok")) is bool, "reply lacks boolean ok")
    require(type(value.get("revision")) is int and value["revision"] >= 0,
            "reply lacks committed revision")
    return value


def success(reply):
    require(reply.get("ok") is True, "request failed: " + repr(reply))
    return reply["result"]


def error(reply, code):
    require(reply.get("ok") is False and reply.get("error", {}).get("code") == code,
            "expected " + code + ": " + repr(reply))


class Client:
    def __init__(self, config, client_id):
        self.host = config["host"]
        self.port = config["port"]
        self.token = config["token"]
        self.client_id = client_id
        self.counter = 0

    def connect(self):
        return socket.create_connection((self.host, self.port), timeout=5)

    def envelope(self, body, request_id=None):
        if request_id is None:
            require(body["action"] not in WRITE_ACTIONS, "write ID must be explicit")
            self.counter += 1
            request_id = "read-" + str(self.counter)
        return dict(body, v=1, token=self.token, client=self.client_id, id=request_id)

    def request(self, body, request_id=None):
        request = self.envelope(body, request_id)
        with self.connect() as peer:
            peer.sendall(frame(request))
            reply = receive_frame(peer)
        require(reply.get("id") == request["id"], "response ID mismatch")
        return reply

    def ping(self):
        reply = self.request({"action": "ping"})
        require(success(reply)["service"] == "zandb", "wrong service")
        return reply

    def query(self, tag):
        return success(self.request({"action": "query", "collection": "docs",
                                     "filter": {"field": "tag", "eq": tag},
                                     "limit": 1000}))


def document(title, tag, vector=None, **extra):
    return dict(title=title, tag=tag, vector=vector or [1, 0],
                note="quote \"; slash \\; line\n; UTF-8 数据", **extra)


def upsert(doc_id, doc):
    return {"action": "upsert", "collection": "docs",
            "document_id": doc_id, "document": doc}


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, allow_nan=False), encoding="utf-8")


def output_objects(text):
    values = []
    for line in text.splitlines():
        try:
            value = json.loads(line)
        except (ValueError, TypeError):
            continue
        if isinstance(value, dict):
            values.append(value)
    return values


def finish_process(process, timeout=20):
    try:
        stdout, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, _ = process.communicate(timeout=10)
        raise AssertionError("child process did not exit: " + stdout[-4000:])
    return stdout


class Owner:
    def __init__(self, exe, config_path, config, sandbox, boot, cwd):
        self.config = config
        self.stop_file = Path(config["stop_file"])
        self.stop_file.unlink(missing_ok=True)
        self.log_path = sandbox / ("owner-" + str(boot) + ".log")
        self.log = self.log_path.open("wb")
        try:
            self.process = subprocess.Popen([str(exe), str(config_path)], cwd=cwd,
                                            stdout=self.log, stderr=subprocess.STDOUT)
        except BaseException:
            self.log.close()
            raise

    def diagnostics(self):
        self.log.flush()
        return self.log_path.read_text(encoding="utf-8", errors="replace")[-6000:]

    def ready(self, timeout):
        client = Client(self.config, "startup")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError("owner exited at startup: " + self.diagnostics())
            try:
                client.ping()
                return
            except (OSError, EOFError, TimeoutError):
                time.sleep(0.025)
        raise AssertionError("owner did not become ready: " + self.diagnostics())

    def stop(self):
        self.stop_file.write_text("stop\n", encoding="utf-8")
        try:
            self.process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            raise AssertionError("graceful stop did not drain: " + self.diagnostics())
        self.log.flush()
        output = self.log_path.read_text(encoding="utf-8", errors="replace")
        require(self.process.returncode == 0,
                "owner exited unsuccessfully during graceful stop: " + output[-6000:])
        require(any(value.get("stopped") is True for value in output_objects(output)),
                "owner did not report graceful close: " + output[-6000:])
        require(LEAK_PREFIX not in output, "owner leaked on graceful shutdown: " + output[-6000:])
        self.log.close()

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=10)
        self.log.close()


class Suite:
    def __init__(self, exe, sandbox, cwd, startup_timeout):
        self.exe = exe
        self.sandbox = sandbox
        # Instrumented children may append zan_leaks.log; keep every process in
        # the task sandbox. Executable/config/database paths are all absolute.
        self.cwd = sandbox
        self.startup_timeout = startup_timeout
        self.boot = 0
        self.owner = None
        self.children = []
        self.files = []
        self.checks = 0
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.config = {"path": str(sandbox / "owner.zdb"), "host": "127.0.0.1",
                       "port": port, "token": secrets.token_hex(24), "timeout_ms": 2000,
                       "max_connections": 64, "stop_file": str(sandbox / "stop")}
        self.config_path = sandbox / "owner.json"
        write_json(self.config_path, self.config)
        self.client = Client(self.config, "integration")

    def check(self, name):
        self.checks += 1
        print("ok " + str(self.checks) + " - " + name, flush=True)

    def start(self):
        self.boot += 1
        self.owner = Owner(self.exe, self.config_path, self.config, self.sandbox,
                           self.boot, self.cwd)
        self.owner.ready(self.startup_timeout)

    def zan_client(self, body, client_id, name):
        config = dict(self.config, client_id=client_id)
        config_path = self.sandbox / (name + "-config.json")
        request_path = self.sandbox / (name + "-request.json")
        write_json(config_path, config)
        write_json(request_path, body)
        process = subprocess.Popen([str(self.exe), "--request", str(config_path), str(request_path)],
                                   cwd=self.cwd, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   encoding="utf-8", errors="replace")
        self.children.append(process)
        return process

    def zan_reply(self, process):
        output = finish_process(process)
        require(process.returncode == 0, "ZanDbClient process exited unsuccessfully: " + output[-4000:])
        require(LEAK_PREFIX not in output, "ZanDbClient process leaked: " + output[-6000:])
        replies = [v for v in output_objects(output) if "ok" in v]
        require(len(replies) == 1, "ZanDbClient process returned no unique reply: " + output[-4000:])
        return replies[0]

    def setup(self):
        operations = [upsert(1, document("alpha alpha alpha", "blocked", [1, 0], rank=0)),
                      upsert(2, document("alpha eligible", "allowed", [0.9, 0.1], rank=1)),
                      upsert(3, document("alpha eligible", "allowed", [0.8, 0.2], rank=1)),
                      upsert(4, document("gamma", "blocked", [0, 1], rank=0))]
        reply = self.client.request({"action": "batch", "operations": operations}, "fixtures")
        require(success(reply) == [1, 2, 3, 4] and reply["revision"] == 1,
                "fixture batch must commit once")
        for action, field, options in [("ensure_index", "tag", None),
                                       ("ensure_text_index", "title", None),
                                       ("ensure_vector_index", "vector", {"dimensions": 2, "metric": 0})]:
            body = {"action": action, "collection": "docs", "field": field}
            if options is not None:
                body["options"] = options
            reply = self.client.request(body, "create-" + field)
            success(reply)
            before = self.client.ping()["revision"]
            require(self.client.request(body, "create-" + field) == reply,
                    "index retry changed response")
            require(self.client.ping()["revision"] == before, "index retry changed revision")
        indexes = success(self.client.request({"action": "list_indexes", "collection": "docs"}))
        require({(v["field"], v["type"]) for v in indexes} ==
                {("tag", "field"), ("title", "text"), ("vector", "vector")}, "index inventory mismatch")
        self.check("CRUD batch and retry-safe index creation")

    def clients(self):
        body = {"action": "insert", "collection": "docs", "id": "same-concurrent-id",
                "document": document("alpha concurrent", "zan-clients")}
        before = self.client.ping()["revision"]
        first = self.zan_client(body, "zan-clients", "zan-first")
        second = self.zan_client(body, "zan-clients", "zan-second")
        a, b = self.zan_reply(first), self.zan_reply(second)
        success(a)
        require(a == b and a["revision"] == before + 1, "concurrent same-ID writes were duplicated")
        require(len(self.client.query("zan-clients")) == 1, "concurrent insert allocated two records")
        conflict = dict(body, document=document("changed", "zan-clients"))
        error(self.zan_reply(self.zan_client(conflict, "zan-clients", "zan-conflict")), "id_conflict")
        fetched = success(self.client.request({"action": "get", "collection": "docs", "document_id": a["result"]}))
        require(fetched["note"] == body["document"]["note"], "UTF-8 or JSON escaping was altered")
        self.check("two real ZanDbClient processes share one durable request ID")

    def filters(self):
        for action in ["search_text", "search_vector", "search_vector_exact", "search_hybrid"]:
            body = {"action": action, "collection": "docs", "k": 100, "ef": 100}
            if action == "search_text":
                body.update(field="title", query="alpha")
            elif action == "search_hybrid":
                body.update(text_field="title", query="alpha", vector_field="vector", vector=[1, 0])
            else:
                body.update(field="vector", vector=[1, 0])
            baseline = success(self.client.request(body))
            require(len(baseline) >= 2, "retrieval fixture insufficient: " + action)
            if action in ("search_vector", "search_vector_exact"):
                require(all(type(hit.get("distance")) in (int, float) and "score" not in hit
                            for hit in baseline), "vector reply lost its metric distance: " + action)
                require(all(baseline[i]["distance"] <= baseline[i + 1]["distance"]
                            for i in range(len(baseline) - 1)), "vector distances are not ordered: " + action)
            target = baseline[-1]["id"]
            filtered = dict(body, k=1, filter={"field": "_id", "eq": target})
            hits = success(self.client.request(filtered))
            require(len(hits) == 1 and hits[0]["id"] == target,
                    "filter must run before top-k selection: " + action)
            hits = success(self.client.request(dict(body, filter={"field": "tag", "eq": "allowed"})))
            require(len(hits) == 2 and {h["id"] for h in hits} == {2, 3},
                    "string equality filter mismatch: " + action)
            error(self.client.request(dict(body, filter={"field": "rank", "eq": True})), "invalid_request")
        self.check("text, graph, exact and hybrid filters run before top k")

    def rollback(self):
        before = self.client.ping()["revision"]
        body = {"action": "batch", "operations": [
            {"action": "insert", "collection": "docs", "document": document("phantom", "phantom")},
            {"action": "update", "collection": "docs", "document_id": 999999,
             "document": document("missing", "phantom")}]}
        reply = self.client.request(body, "rollback-id")
        error(reply, "not_found")
        require(reply["revision"] == before and self.client.query("phantom") == [], "partial batch survived rollback")
        reply = self.client.request({"action": "insert", "collection": "docs",
                                     "document": document("after rollback", "rollback-reused")}, "rollback-id")
        success(reply)
        require(reply["revision"] == before + 1, "failed request reserved its ID or incremented revision")
        self.check("failed CRUD batch rolls back and does not reserve request ID")

    def workers(self, rounds):
        gate = self.sandbox / "workers-go"
        processes = []
        ready = []
        before = self.client.ping()["revision"]
        for index in range(2):
            marker = self.sandbox / ("worker-" + str(index) + ".ready")
            ready.append(marker)
            log = (self.sandbox / ("worker-" + str(index) + ".log")).open("wb")
            self.files.append(log)
            process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                                        "--worker", str(self.config_path), str(gate),
                                        str(marker), str(index), str(rounds)],
                                       cwd=self.cwd, stdout=log, stderr=subprocess.STDOUT)
            processes.append(process)
            self.children.append(process)
        deadline = time.monotonic() + 20 + rounds
        while not all(path.exists() for path in ready):
            require(time.monotonic() < deadline and all(p.poll() is None for p in processes),
                    "client workers did not reach concurrency barrier")
            time.sleep(0.01)
        gate.write_text("go", encoding="utf-8")
        observations = 0
        last_revision = before
        while any(p.poll() is None for p in processes):
            require(time.monotonic() < deadline, "concurrent workers stalled")
            for index in range(2):
                rows = self.client.query("pair-" + str(index))
                require(len(rows) in (0, 2), "reader observed a partial batch")
                if rows:
                    require(rows[0]["generation"] == rows[1]["generation"], "reader observed mixed generations")
                observations += 1
            revision = self.client.ping()["revision"]
            require(revision >= last_revision, "committed revision moved backwards")
            last_revision = revision
        for index, process in enumerate(processes):
            process.wait(timeout=5)
            log_path = self.sandbox / ("worker-" + str(index) + ".log")
            diagnostics = log_path.read_text(encoding="utf-8", errors="replace")
            require(process.returncode == 0, "client worker failed: " + diagnostics[-6000:])
            rows = self.client.query("pair-" + str(index))
            require(len(rows) == 2 and all(row["generation"] == rounds - 1 for row in rows),
                    "last concurrent batch missing")
        require(observations > 0 and self.client.ping()["revision"] == before + 2 * rounds,
                "concurrent batch revision/dedup count mismatch")
        self.check("two client processes write atomic pairs while another connection reads")

    def owner_lock(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        config = dict(self.config, port=port, stop_file=str(self.sandbox / "second-stop"))
        path = self.sandbox / "second-owner.json"
        write_json(path, config)
        process = subprocess.Popen([str(self.exe), str(path)], cwd=self.cwd,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        self.children.append(process)
        output = finish_process(process, timeout=10)
        startup = output_objects(output)
        require(any(value.get("ready") is False and
                    ("locked by another writer" in value.get("error", "") or
                     "store is locked or its lock file cannot be opened" in value.get("error", ""))
                    for value in startup),
                "second owner did not reject the file lock: " + output[-4000:])
        require(LEAK_PREFIX not in output, "rejected owner process leaked: " + output[-6000:])
        self.client.ping()
        self.check("a second owner process rejects the exclusive file lock")

    def assert_closed(self, peer, timeout=5):
        peer.settimeout(timeout)
        try:
            require(peer.recv(1) == b"", "rejected connection still sends data")
        except (ConnectionResetError, ConnectionAbortedError):
            pass

    def framing(self):
        one = self.client.envelope({"action": "ping"}, "fragmented")
        encoded = frame(one)
        with self.client.connect() as peer:
            for offset in range(4):
                peer.sendall(encoded[offset:offset + 1])
                time.sleep(0.001)
            for offset in range(4, len(encoded), 7):
                peer.sendall(encoded[offset:offset + 7])
            require(receive_frame(peer)["id"] == "fragmented", "fragmented frame lost correlation")
            a = self.client.envelope({"action": "ping"}, "coalesced-a")
            b = self.client.envelope({"action": "ping"}, "coalesced-b")
            peer.sendall(frame(a) + frame(b))
            require(receive_frame(peer)["id"] == "coalesced-a" and
                    receive_frame(peer)["id"] == "coalesced-b", "frames crossed connection boundaries")
        for length in [0, MAX_PAYLOAD + 1, 0xFFFFFFFF]:
            with self.client.connect() as peer:
                peer.sendall(struct.pack("!I", length))
                error(receive_frame(peer), "frame_too_large")
                self.assert_closed(peer)
        for raw in [b"\xff", b"{}\x00"]:
            with self.client.connect() as peer:
                peer.sendall(struct.pack("!I", len(raw)) + raw)
                error(receive_frame(peer), "invalid_request")
        duplicate = json_bytes(one)[:-1] + b',"action":"delete"}'
        with self.client.connect() as peer:
            peer.sendall(struct.pack("!I", len(duplicate)) + duplicate)
            error(receive_frame(peer), "invalid_request")
        for partial in [encoded[:2], encoded[:-3]]:
            with self.client.connect() as peer:
                peer.sendall(partial)
                peer.shutdown(socket.SHUT_WR)
                self.assert_closed(peer)
        with self.client.connect() as peer:
            peer.sendall(b"\x00")
            self.assert_closed(peer, self.config["timeout_ms"] / 1000 + 5)
        denied = dict(one, token="incorrect-token-2026")
        with self.client.connect() as peer:
            peer.sendall(frame(denied))
            error(receive_frame(peer), "unauthorized")
        error(self.client.request({"action": "sql", "command": "DROP ALL"}), "unknown_action")
        self.client.ping()
        self.check("fragmented, coalesced, malformed, truncated and timed-out frames")

    def crash_recovery(self):
        lost_body = {"action": "insert", "collection": "docs",
                     "document": document("alpha lost reply", "lost-reply")}
        lost = self.client.envelope(lost_body, "lost-reply")
        before = self.client.ping()["revision"]
        with self.client.connect() as peer:
            peer.sendall(frame(lost))
            peer.shutdown(socket.SHUT_WR)
            # Leave the reply unread while observing durable completion elsewhere;
            # an immediate reset before dispatch would not prove lost-reply replay.
            deadline = time.monotonic() + 10
            while not self.client.query("lost-reply"):
                require(time.monotonic() < deadline, "unacknowledged insert did not commit")
                time.sleep(0.01)
        original = self.client.request(lost_body, "lost-reply")
        success(original)
        require(original["revision"] == before + 1, "lost-reply retry executed a new insert")
        crash_body = {"action": "batch", "operations": [
            upsert(2000, document("alpha crash", "crash-pair", generation=1)),
            upsert(2001, document("alpha crash", "crash-pair", generation=1))]}
        with self.client.connect() as peer:
            peer.sendall(frame(self.client.envelope(crash_body, "crash-batch")))
            self.owner.kill()
        self.start()
        require(self.client.request(lost_body, "lost-reply") == original and
                len(self.client.query("lost-reply")) == 1, "durable dedup did not survive owner kill")
        rows = self.client.query("crash-pair")
        require(len(rows) in (0, 2), "crash recovery published a partial transaction")
        if rows:
            require(all(row["generation"] == 1 for row in rows), "crash batch generation mismatch")
        recovered = self.client.request(crash_body, "crash-batch")
        require(success(recovered) == [2000, 2001] and
                self.client.request(crash_body, "crash-batch") == recovered,
                "uncertain batch outcome was not resolved by ID")
        for index in range(2):
            require(len(self.client.query("pair-" + str(index))) == 2, "committed worker batch lost after kill")
        self.filters()
        checkpoint = {"action": "checkpoint_search", "collection": "docs"}
        reply = self.client.request(checkpoint, "checkpoint")
        success(reply)
        revision = self.client.ping()["revision"]
        require(self.client.request(checkpoint, "checkpoint") == reply and
                self.client.ping()["revision"] == revision, "checkpoint replay created another revision")
        self.check("kill/restart preserves WAL, indexes and uncertain write replay")
        self.owner.stop()
        self.start()
        rebuild = {"action": "rebuild_search", "collection": "docs"}
        before = self.client.ping()["revision"]
        rebuilt = self.client.request(rebuild, "source-rebuild")
        require(success(rebuilt) is True and rebuilt["revision"] > before,
                "source rebuild did not complete its final request-record commit")
        require(self.client.request(rebuild, "source-rebuild") == rebuilt and
                self.client.ping()["revision"] == rebuilt["revision"],
                "source rebuild retry repeated maintenance")
        require(len(success(self.client.request({"action": "list_indexes", "collection": "docs"}))) == 3,
                "source rebuild lost definitions")
        self.filters()
        self.check("source rebuild through the owner preserves retrieval and durable replay")

    def graceful(self):
        peers = []
        try:
            for _ in range(4):
                peer = self.client.connect()
                peers.append(peer)
                peer.sendall(b"\x00")
            time.sleep(0.05)
            self.owner.stop()
            for peer in peers:
                self.assert_closed(peer)
            # Reopening proves the owner handle and listener were released.
            self.start()
            require(len(self.client.query("crash-pair")) == 2, "graceful stop lost completed writes")
            self.owner.stop()
        finally:
            for peer in peers:
                peer.close()
        self.check("graceful stop drains connections and releases the owner lock")

    def cleanup(self):
        for process in self.children:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=10)
            if process.stdout is not None:
                process.stdout.close()
        if self.owner is not None and self.owner.process.poll() is None:
            self.owner.kill()
        elif self.owner is not None and not self.owner.log.closed:
            self.owner.log.close()
        for stream in self.files:
            stream.close()


def worker(config_path, gate_path, marker_path, index, rounds):
    config = json.loads(Path(config_path).read_text(encoding="utf-8"))
    client = Client(config, "worker-" + str(index))
    Path(marker_path).write_text("ready", encoding="utf-8")
    deadline = time.monotonic() + 20
    while not Path(gate_path).exists():
        require(time.monotonic() < deadline, "worker start barrier timed out")
        time.sleep(0.01)
    for generation in range(rounds):
        operations = [upsert(1000 + 2 * index + offset,
                             document("alpha worker", "pair-" + str(index), generation=generation))
                      for offset in range(2)]
        body = {"action": "batch", "operations": operations}
        request_id = "pair-" + str(generation)
        reply = client.request(body, request_id)
        require(success(reply) == [1000 + 2 * index, 1001 + 2 * index], "wrong atomic batch results")
        require(client.request(body, request_id) == reply, "worker retry changed response")
        if generation % 3 == 0:
            changed = json.loads(json.dumps(body))
            changed["operations"][0]["document"]["generation"] = generation + 999
            error(client.request(changed, request_id), "id_conflict")
        time.sleep(0.01)
    print(json.dumps({"worker": index, "rounds": rounds}), flush=True)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--worker":
        require(len(sys.argv) == 7, "invalid worker arguments")
        worker(sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), int(sys.argv[6]))
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="already built examples/db/zandb_server.zan executable")
    parser.add_argument("--rounds", type=int, default=12, help="atomic batches per client process (1..100)")
    parser.add_argument("--startup-timeout", type=float, default=20, help="owner startup deadline in seconds")
    args = parser.parse_args()
    require(1 <= args.rounds <= 100 and args.startup_timeout > 0, "invalid test limits")
    exe = args.exe.resolve()
    require(exe.is_file(), "build the owner example before invoking this script: " + str(exe))
    repository = Path(__file__).resolve().parents[2]
    scratch = repository / "_scratch"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zandb-service-", dir=scratch) as temporary:
        suite = Suite(exe, Path(temporary), repository, args.startup_timeout)
        try:
            suite.start()
            suite.setup()
            suite.clients()
            suite.filters()
            suite.rollback()
            suite.workers(args.rounds)
            suite.owner_lock()
            suite.framing()
            suite.crash_recovery()
            suite.graceful()
            print("passed " + str(suite.checks) + " multiprocess checks", flush=True)
        except BaseException:
            if suite.owner is not None and not suite.owner.log.closed:
                print(suite.owner.diagnostics(), file=sys.stderr)
            raise
        finally:
            suite.cleanup()


if __name__ == "__main__":
    main()

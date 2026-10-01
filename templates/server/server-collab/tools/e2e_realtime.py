"""End-to-end e2e for the collab realtime path (A327-05) — PRODUCTION config.

Unlike e2e_im.py / e2e_collab.py (which boot with ZAN_NO_BG=1 while A321 is
open), this script boots the server WITH background coroutines
(CalendarRemind / MessageRelay / CollabEventRelay) and the default 4 workers,
then asserts the realtime contract:

  1. SSE subscribe (ApiAuth): HTTP 200 + text/event-stream + collab.hello frame
  2. online delivery: A sends -> B receives `event: collab` frame carrying
     message.created within the 1s relay beat (+ slack)
  3. transport cleanliness: a burst of API calls while relays run must produce
     zero corrupted responses (this is the A321 probe; anomalies are reported,
     never retried away)
  4. disconnect + catch-up: B drops, A sends 2, B reconnects and pulls
     events?after=<cursor> -> exactly the 2 missed message.created events
  5. online frame == replay frame for the same eventId (Policy.Frame shape)
  6. server restart: the cursor survives (DB-backed), the catch-up window is
     unchanged, SSE reconnects cleanly

  python tools/e2e_realtime.py [--exe PATH]
Stdlib http.client/socket/subprocess only.
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
from http.client import HTTPException
import http.client
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                      # templates/server/server-collab
REPO = os.path.dirname(os.path.dirname(os.path.dirname(ROOT)))
BASE = "http://127.0.0.1:8090"
PORT = 8090

fails = []
checks = 0
def ok(cond, label):
    global checks
    checks += 1
    print(("PASS " if cond else "FAIL ") + label)
    if not cond:
        fails.append(label)

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw): return None
opener = urllib.request.build_opener(NoRedirect)

anomalies = []

def call(method, path, data=None, token=None):
    """{code,msg,data} envelope -> (status, dict-or-None). One attempt only:
    transport anomalies must be VISIBLE here (A321 probe), never retried away.
    Returns ("__anomaly__", reason) on transport failure."""
    req = urllib.request.Request(BASE + path, method=method)
    if token:
        req.add_header("Authorization", "Bearer " + token)
    body = None
    if data is not None:
        body = urllib.parse.urlencode(data).encode()
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = urllib.request.urlopen(req, body, timeout=10)
        st = resp.status
        raw = resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        st = e.code
        raw = e.read().decode("utf-8", "replace")
    except (urllib.error.URLError, OSError, HTTPException) as e:
        anomalies.append("%s %s: %r" % (method, path, e))
        return 0, None
    if "\x00" in raw:
        anomalies.append("%s %s: NUL bytes in response (A321 fingerprint)"
                         % (method, path))
        return st, None
    try:
        return st, json.loads(raw)
    except Exception:
        anomalies.append("%s %s: unparseable body %r" % (method, path, raw[:80]))
        return st, None

def data_of(env):
    return (env or {}).get("data") or {}

class Sse:
    """Minimal SSE reader: Bearer-authed GET, frame = lines up to a blank line."""

    def __init__(self, path, token):
        self.conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=15)
        self.conn.connect()
        self.sock = self.conn.sock          # getresponse() 后 conn.sock 会被置空
        self.conn.request("GET", path, headers={
            "Authorization": "Bearer " + token, "Accept": "text/event-stream"})
        self.resp = self.conn.getresponse()
        self.buf = b""
        self.event = ""
        self.data = ""

    def status(self):
        return self.resp.status

    def content_type(self):
        return self.resp.getheader("Content-Type") or ""

    def read_frame(self, timeout_s):
        """Next complete SSE frame -> (event, data) strings, or None on close,
        or ("__timeout__", "") when nothing arrived in time."""
        try:
            self.sock.settimeout(timeout_s)
        except OSError:
            return None
        deadline = time.time() + timeout_s
        while b"\n\n" not in self.buf:
            remaining = max(0.1, deadline - time.time())
            if remaining <= 0.05:
                return ("__timeout__", "")
            try:
                chunk = self.resp.read1(4096)
            except (OSError, TimeoutError):
                return ("__timeout__", "")
            if not chunk:
                return None
            self.buf += chunk
        frame, self.buf = self.buf.split(b"\n\n", 1)
        ev, data = "", ""
        for line in frame.decode("utf-8", "replace").splitlines():
            if line.startswith("event:"):
                ev = line[len("event:"):].strip()
            elif line.startswith("data:"):
                data += line[len("data:"):].strip()
        return (ev, data)

    def close(self):
        try:
            self.conn.close()
        except OSError:
            pass

def wait_port(timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", PORT), timeout=2)
            s.close()
            return True
        except OSError:
            time.sleep(0.5)
    return False

def wait_gone(timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", PORT), timeout=1)
            s.close()
            time.sleep(0.3)
        except OSError:
            return True
    return False

def reset_db():
    for name in ("app.db", "app.db-shm", "app.db-wal", "metrics.db",
                 "metrics.db-shm", "metrics.db-wal"):
        p = os.path.join(ROOT, "data", name)
        if os.path.exists(p): os.remove(p)

def start_server(exe):
    """生产配置起服：不注入 ZAN_NO_BG —— 后台协程（CalendarRemind/MessageRelay/
    CollabEventRelay）全部开启，默认 4 worker。传输层异常在上面 call() 里
    记账，绝不重试掩盖（这就是 A321 的探针口径）。"""
    os.makedirs(os.path.join(ROOT, "data"), exist_ok=True)
    log = open(os.path.join(ROOT, "data", "e2e_realtime_server.log"), "ab")
    env = dict(os.environ)
    env.pop("ZAN_NO_BG", None)
    proc = subprocess.Popen([exe], cwd=ROOT, stdout=log, stderr=log, env=env)
    if not wait_port():
        raise SystemExit("server did not listen on %d; see data/e2e_realtime_server.log" % PORT)
    time.sleep(1.5)
    return proc

def stop_server(proc):
    # master + worker 树一起杀（孤儿 worker 攥监听端口，见 e2e_im.stop_server）。
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       capture_output=True)
    else:
        proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    wait_gone()
    time.sleep(0.5)

TS = str(int(time.time()))[-6:]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "_scratch", "collab_server.exe"))
    args = ap.parse_args()
    if not os.path.exists(args.exe):
        raise SystemExit("server exe not found: " + args.exe)

    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=2)
        s.close()
        raise SystemExit("port %d already in use - stop the running server-collab first" % PORT)
    except OSError:
        pass

    reset_db()
    proc = start_server(args.exe)
    cursor = 0
    try:
        cursor = run_flow()
    finally:
        stop_server(proc)

    # ---- restart: cursor survives (DB-backed), frontier is exact ----------
    proc = start_server(args.exe)
    try:
        st, env = call("GET", "/api/collab/events?after=%d" % cursor, token=TOKENS["b"])
        ok(st == 200 and len(data_of(env).get("events") or []) == 0,
           "after restart the frontier cursor has zero pending events (no gaps)")
        sse = Sse("/api/collab/listen", TOKENS["b"])
        ok(sse.status() == 200 and "text/event-stream" in sse.content_type(),
           "SSE reconnects after restart")
        fr = sse.read_frame(5)
        sse.close()
        ok(fr is not None and fr[0] == "collab.hello", "hello frame after restart")
        st, env = call("POST", "/api/collab/send",
                       {"conversationId": str(CONVS["id"]),
                        "content": "重启后补偿-" + TS,
                        "clientRequestId": "rt-%s-m5" % TS}, token=TOKENS["a"])
        ok(st == 200, "send after restart")
        st, env = call("GET", "/api/collab/events?after=%d" % cursor, token=TOKENS["b"])
        evs = data_of(env).get("events") or []
        ok(len(evs) == 1 and evs[0].get("event") == "message.created"
           and "重启后补偿-" + TS in (evs[0].get("excerpt") or ""),
           "post-restart event flows through the same cursor")
    finally:
        stop_server(proc)

    print()
    if anomalies:
        print("TRANSPORT ANOMALIES (%d) — A321 fingerprint:" % len(anomalies))
        for a in anomalies:
            print("  " + a)
    else:
        print("transport clean: %d API calls with background coroutines on,"
              " zero corrupted responses" % CLEAN_CALLS[0])
    print()
    if fails or anomalies:
        print("FAILED: %d/%d -> %s" % (len(fails), checks, fails))
        sys.exit(1)
    print("ALL PASS checks=%d" % checks)

TOKENS = {}
CONVS = {}
CLEAN_CALLS = [0]

def run_flow():
    crid = lambda tag: "rt-%s-%s" % (TS, tag)

    # ---- admin + account B -------------------------------------------------
    st, _, setc = http_form("/admin/login", {"user": "admin", "pass": "admin-bootstrap-2026"})
    cookie = "".join(x.split(";")[0] + "; " for x in setc)
    ok(st in (200, 302) and cookie != "", "admin cookie login")
    user_b = "rt_b_" + TS
    st, j, _ = http_form("/admin/system/users/save", {
        "username": user_b, "nickname": "实时" + TS,
        "email": user_b + "@demo.local", "mobile": "1370000" + TS[-4:],
        "departmentId": "0", "status": "1", "password": "rtpass2026"},
        cookie=cookie)
    try:
        ok(json.loads(j).get("code") == "0000", "create account B")
    except Exception:
        ok(False, "create account B")
    st, j, _ = http_form("/admin/system/users?kw=" + urllib.parse.quote(user_b),
                         cookie=cookie)
    m = re.search(r'<tr data-id="(\d+)"[\s\S]{0,600}?' + re.escape(user_b), j)
    bid = int(m.group(1)) if m else 0
    ok(bid > 0, "B id resolved")
    st, j, _ = http_form("/admin/system/users/rolessave",
                         # 包契约：admin.js 把 checkbox 组合成 roleIds=1,2
                         [("id", str(bid)), ("roleIds", "1,2")],
                         cookie=cookie)
    try:
        ok(json.loads(j).get("code") == "0000", "assign roles to B")
    except Exception:
        ok(False, "assign roles to B")

    da = call("POST", "/api/auth/login", {"user": "admin", "pass": "admin-bootstrap-2026"})
    TOKENS["a"] = data_of(da[1]).get("token", "")
    db_ = call("POST", "/api/auth/login", {"user": user_b, "pass": "rtpass2026"})
    TOKENS["b"] = data_of(db_[1]).get("token", "")
    ok(TOKENS["a"] != "" and TOKENS["b"] != "", "A+B token login")
    ta, tb = TOKENS["a"], TOKENS["b"]

    st, env = call("POST", "/api/collab/create",
                   {"title": "实时组" + TS, "ids": str(bid),
                    "clientRequestId": crid("c0")}, token=ta)
    conv = int(data_of(env).get("id", "0"))
    ok(st == 200 and conv > 0, "group created")
    CONVS["id"] = conv

    # ---- 1. SSE subscribe --------------------------------------------------
    sse = Sse("/api/collab/listen", tb)
    ok(sse.status() == 200 and "text/event-stream" in sse.content_type(),
       "SSE subscribe: 200 + text/event-stream")
    fr = sse.read_frame(5)
    ok(fr is not None and fr[0] == "collab.hello", "SSE first frame is collab.hello")

    # cursor right after join: no events yet beyond creation
    st, env = call("GET", "/api/collab/events?after=0", token=tb)
    evs = data_of(env).get("events") or []
    ok(st == 200 and len(evs) >= 1, "events replay available to member")

    # ---- 2. online delivery within relay beat ------------------------------
    online = []
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "在线帧-" + TS,
                    "clientRequestId": crid("m1")}, token=ta)
    mid1 = int(data_of(env).get("id", "0"))
    ok(st == 200 and mid1 > 0, "A sends message")
    got = None
    deadline = time.time() + 8
    while time.time() < deadline:
        fr = sse.read_frame(3)
        if fr is None or fr[0] == "__timeout__":
            break
        if fr[0] != "collab":
            continue
        try:
            o = json.loads(fr[1])
        except Exception:
            continue
        # 订阅前落库的事件会在订阅后的首个心跳一并推来（在线投递只管增量，
        # 不回放历史），所以要排空到目标 message.created，而不是假设首帧即它。
        if o.get("event") == "message.created" and o.get("messageId") == str(mid1):
            got = o
            break
    online = [got] if got else []
    ok(got is not None and "在线帧-" + TS in (got.get("excerpt") or ""),
       "online frame is message.created with matching excerpt")
    cursor = int(online[0].get("eventId", "0")) if online else 0

    # ---- 3. transport-cleanliness burst (A321 probe) ------------------------
    burst_ok = True
    for i in range(15):
        path = ["/api/collab/conversations",
                "/api/collab/members?conversationId=%d" % conv,
                "/api/collab/messages?conversationId=%d" % conv,
                "/api/collab/events?after=0"][i % 4]
        st, env = call("GET", path, token=tb)
        # 包 ApiController 信封 code=0（int），与 "0000" 同为成功
        if st != 200 or env is None or (env or {}).get("code") not in (0, "0", "0000"):
            burst_ok = False
        CLEAN_CALLS[0] += 1
    ok(burst_ok, "15-call burst under background coroutines: all envelopes valid")

    # ---- 4. disconnect -> 2 offline sends -> reconnect + catch-up -----------
    sse.close()
    time.sleep(0.5)
    missed_ids = []
    for i, tag in ((0, "m2"), (1, "m3")):
        st, env = call("POST", "/api/collab/send",
                       {"conversationId": str(conv),
                        "content": "离线补偿%d-" % (i + 1) + TS,
                        "clientRequestId": crid(tag)}, token=ta)
        ok(st == 200, "offline send #%d" % (i + 1))
        missed_ids.append(int(data_of(env).get("id", "0")))
    st, env = call("GET", "/api/collab/events?after=%d" % cursor, token=tb)
    evs = data_of(env).get("events") or []
    ok(st == 200 and len(evs) == 2
       and [int(e.get("messageId", "0")) for e in evs] == missed_ids
       and all(e.get("event") == "message.created" for e in evs),
       "catch-up after disconnect returns exactly the 2 missed events")
    ev_ids = [int(e["eventId"]) for e in evs]
    ok(ev_ids == sorted(ev_ids), "catch-up events ascending")

    # ---- 5. online frame == replay frame for the same eventId ---------------
    st, env = call("GET", "/api/collab/events?after=%d" % (cursor - 1 if cursor > 1 else 0),
                   token=tb)
    evs = data_of(env).get("events") or []
    same = [e for e in evs if int(e.get("eventId", "0")) == cursor]
    ok(len(same) == 1 and set(same[0].keys()) == set(online[0].keys()),
       "online and replayed frames share the exact same shape")

    # SSE still works alongside (reconnect during the same server run)
    sse2 = Sse("/api/collab/listen", tb)
    fr = sse2.read_frame(5)
    ok(sse2.status() == 200 and fr is not None and fr[0] == "collab.hello",
       "SSE reconnects within the same run")
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "重连后在线-" + TS,
                    "clientRequestId": crid("m4")}, token=ta)
    ok(st == 200, "send after reconnect")
    fr = sse2.read_frame(6)
    ok(fr is not None and fr[0] == "collab", "online delivery after reconnect")
    sse2.close()

    # frontier cursor at end of run (restart section continues from here)
    st, env = call("GET", "/api/collab/events?after=0", token=tb)
    return int(data_of(env).get("next", "0"))

def http_form(path, data=None, cookie=None):
    req = urllib.request.Request(BASE + path)
    if cookie: req.add_header("Cookie", cookie)
    req.add_header("X-Fragment", "1")
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    if body is not None:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = opener.open(req, body, timeout=10)
        return resp.status, resp.read().decode("utf-8", "replace"), resp.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), e.headers.get_all("Set-Cookie") or []

if __name__ == "__main__":
    main()

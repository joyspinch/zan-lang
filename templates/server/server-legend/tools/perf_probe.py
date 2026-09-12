#!/usr/bin/env python3
"""Performance matrix for the server-game template.

Run against a live instance (no other load). Creates perf00..perf07 accounts
with characters entered.

P1 authed HTTP op latency (state, 8 accounts @ ~10/s, 200 samples)
P2 web page GET throughput (8 concurrent clients, 8s)
P3 authed HTTP op sustained throughput (8 clients paced, 10s)
P4 login E2E throughput (8 loopback source IPs, 10s) -- 预期被 anon 反爆破
   限流钳制：5/5s/IP/worker，keep-alive 把连接钉在单 worker → ~1/s/IP
P5 TCP op RTT (own TCP login+enter session, 200 samples)
P6 GM announce push fanout latency (8 attached bots)

Output: PERF lines + PERF_DONE.
"""
import json

import socket
import statistics
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

BASE = "http://127.0.0.1:8099"
GAME = ("127.0.0.1", 7100)

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):
        return None

opener = urllib.request.build_opener(NoRedirect)

def http(path, data=None, cookie=None):
    req = urllib.request.Request(BASE + path)
    if cookie:
        req.add_header("Cookie", cookie)
    body = None
    if data is not None:
        body = urllib.parse.urlencode(data).encode()
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = opener.open(req, body, timeout=15)
        return resp.status, resp.read().decode("utf-8", "replace"), \
            resp.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as e:
        try:
            return e.code, e.read().decode("utf-8", "replace"), []
        except Exception:
            return e.code, "", []

def api(path, obj, timeout=15):
    req = urllib.request.Request(BASE + path)
    req.add_header("Content-Type", "application/json")
    try:
        resp = opener.open(req, json.dumps(obj).encode(), timeout=timeout)
        return resp.status, json.loads(resp.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8", "replace"))
        except Exception:
            return e.code, {}
    except Exception:
        return 0, {}

def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(len(xs) * p / 100))]

def report(name, lat_ms, extra=""):
    line = (f"PERF {name} n={len(lat_ms)} p50={pct(lat_ms, 50):.1f}ms "
            f"p95={pct(lat_ms, 95):.1f}ms max={max(lat_ms):.1f}ms")
    if extra:
        line += f" {extra}"
    print(line)

def tcp_client():
    s = socket.socket()
    s.connect(GAME)
    s.settimeout(10)
    s.recv(4096)  # banner
    return s

def tcp_op(s, obj, timeout=10):
    s.sendall((json.dumps(obj) + "\n").encode())
    s.settimeout(timeout)
    buf = b""
    while b"\n" not in buf:
        d = s.recv(65536)
        if not d:
            return None
        buf += d
    return json.loads(buf.split(b"\n")[0])

# ---------- setup ----------
toks = []
for i in range(8):
    u = f"perf{i:02d}"
    api("/api/game/register", {"user": u, "pass": "pass123"})
    time.sleep(0.3)
    st, j = api("/api/game/login", {"user": u, "pass": "pass123"})
    tok = j.get("token", "")
    if not tok:
        time.sleep(2)
        tok = api("/api/game/login", {"user": u, "pass": "pass123"})[1] \
            .get("token", "")
    time.sleep(0.3)
    api("/api/game/create", {"op": "create", "token": tok, "realm": 1,
                             "name": u, "cls": 1})
    time.sleep(0.3)
    st, j = api("/api/game/enter", {"op": "enter", "token": tok, "realm": 1})
    toks.append(tok)
print(f"SETUP {len(toks)} accounts ready")

# ---------- P1 ----------
lat = []
for i in range(200):
    t0 = time.perf_counter()
    st, j = api("/api/game/state", {"op": "state", "token": toks[i % 8]})
    dt = (time.perf_counter() - t0) * 1000
    if j.get("ok") == 1:
        lat.append(dt)
    time.sleep(0.1)
report("P1 http-authed-state", lat)

# ---------- P2 ----------
stop = threading.Event()
lat2, errs2 = [], [0]
def page_worker():
    loc = []
    while not stop.is_set():
        t0 = time.perf_counter()
        st, html, _ = http("/")
        dt = (time.perf_counter() - t0) * 1000
        if st == 200:
            loc.append(dt)
        else:
            errs2[0] += 1
    lat2.extend(loc)
ths = [threading.Thread(target=page_worker) for _ in range(8)]
t0 = time.perf_counter()
for t in ths:
    t.start()
time.sleep(8)
stop.set()
for t in ths:
    t.join()
dur = time.perf_counter() - t0
report("P2 http-page-get", lat2, f"throughput={len(lat2)/dur:.0f}/s errs={errs2[0]}")

# ---------- P3 ----------
stop3 = threading.Event()
cnt3, errs3 = [0], [0]
def state_worker(idx):
    n = e = 0
    while not stop3.is_set():
        st, j = api("/api/game/state", {"op": "state", "token": toks[idx]})
        if j.get("ok") == 1:
            n += 1
        elif st == 429:
            e += 1
            time.sleep(0.05)
        else:
            e += 1
        time.sleep(0.04)
    cnt3[0] += n
    errs3[0] += e
ths = [threading.Thread(target=state_worker, args=(i,)) for i in range(8)]
t0 = time.perf_counter()
for t in ths:
    t.start()
time.sleep(10)
stop3.set()
for t in ths:
    t.join()
dur = time.perf_counter() - t0
print(f"PERF P3 http-authed-throughput n={cnt3[0]} "
      f"throughput={cnt3[0]/dur:.0f}/s errs={errs3[0]} dur={dur:.0f}s")

# ---------- P4 ----------
stop4 = threading.Event()
cnt4, lim4 = [0], [0]
def login_worker(idx):
    n = 0
    s = None
    while not stop4.is_set():
        try:
            if s is None:
                s = socket.socket()
                s.bind((f"127.0.0.{2 + idx % 8}", 0))
                s.connect(("127.0.0.1", 8099))
                s.settimeout(10)
            payload = json.dumps({"user": f"perf{idx:02d}", "pass": "pass123"})
            s.sendall(("POST /api/game/login HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                       "Content-Type: application/json\r\n"
                       f"Content-Length: {len(payload)}\r\n"
                       "Connection: keep-alive\r\n\r\n" + payload).encode())
            d = s.recv(8192)
            if not d:
                s.close()
                s = None
                continue
            head = d.split(b"\r\n")[0]
            if b"200" in head and b'"ok":1' in d:
                n += 1
            elif b"429" in head:
                time.sleep(0.3)
        except OSError:
            if s:
                try:
                    s.close()
                except OSError:
                    pass
            s = None
            time.sleep(0.05)
    if s:
        try:
            s.close()
        except OSError:
            pass
    cnt4[0] += n
ths = [threading.Thread(target=login_worker, args=(i,)) for i in range(8)]
t0 = time.perf_counter()
for t in ths:
    t.start()
time.sleep(10)
stop4.set()
for t in ths:
    t.join()
dur = time.perf_counter() - t0
print(f"PERF P4 http-login-throughput n={cnt4[0]} "
      f"throughput={cnt4[0]/dur:.0f}/s dur={dur:.0f}s (anon 反爆破限流钳制)")

# ---------- P5 ----------
s = tcp_client()
tcp_op(s, {"op": "login", "user": "perf07", "pass": "pass123"})
tcp_op(s, {"op": "enter", "realm": 1})
lat5 = []
for i in range(200):
    t0 = time.perf_counter()
    r = tcp_op(s, {"op": "state"})
    dt = (time.perf_counter() - t0) * 1000
    if r is not None and r.get("ok") == 1:
        lat5.append(dt)
    time.sleep(0.02)
s.close()
report("P5 tcp-op-state", lat5)

# ---------- P6 ----------
# fanout 触发用同区聊天 say（推送管线同路，无管理面依赖）。
# P4 的登录风暴会把账号会话顶成未 enter 态——先把 8 个账号全部
# 重新 login+enter（login 是 anon 限流 op，需 pace），再 attach 8 条
# 推送通道，perf00 发言后全部应收到 ev chat。
for i in range(8):
    st, j = api("/api/game/login", {"user": f"perf{i:02d}", "pass": "pass123"})
    toks[i] = j.get("token", toks[i])
    time.sleep(1.2)
for i in range(8):
    api("/api/game/enter", {"op": "enter", "token": toks[i], "realm": 1})
time.sleep(0.5)
bots = []
for i in range(8):
    b = tcp_client()
    tcp_op(b, {"op": "attach", "token": toks[i]})
    bots.append(b)
time.sleep(0.5)
for b in bots:  # 清空 attach ack
    b.settimeout(0.05)
    try:
        while b.recv(65536):
            pass
    except OSError:
        pass
t0 = time.perf_counter()
st, j = api("/api/game/say", {"op": "say", "token": toks[0], "text": "perf-fanout"})
lat6 = []
for b in bots:
    deadline = t0 + 8
    b.settimeout(max(0.1, deadline - time.perf_counter()))
    got = None
    frag = b""
    while time.perf_counter() < deadline and got is None:
        try:
            d = b.recv(65536)
        except (socket.timeout, OSError):
            break
        if not d:
            break
        frag += d
        if b'"chat"' in frag and b"perf-fanout" in frag:
            got = (time.perf_counter() - t0) * 1000
    if got is not None:
        lat6.append(got)
for b in bots:
    b.close()
extra = (f"mean={statistics.mean(lat6):.0f}ms max={max(lat6):.0f}ms"
         if lat6 else f"say={json.dumps(j, ensure_ascii=False)[:60]}")
print(f"PERF P6 chat-push-fanout n={len(lat6)}/{len(bots)} {extra}")

print("PERF_DONE")

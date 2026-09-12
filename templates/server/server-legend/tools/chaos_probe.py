#!/usr/bin/env python3
"""Stability chaos probe for the server-game template.

Run against a live instance. Duration defaults to 600s (override: argv[1]
seconds). Mixes hostile traffic with normal clients and watches resources:

- 3 garbler threads: random bytes / 300KB oversized frame / split frames,
  reconnect loop on port 7100
- 3 churn threads: connect-login-attach then RST close (SO_LINGER 0), ~1/s
- 2 normal bots: login/enter/state loop at ~1 op/s, error + latency tracked
- monitor: VmRSS + fd count of every server-game pid every 5s
  (appended to /tmp/chaos_mon.jsonl)

Ends with: bot error count, latency median, realms liveness, fresh-account
full flow (register/login/create/enter). Exit 1 if bots errored or the
post-check failed.
"""
import json
import os
import socket
import statistics
import struct
import sys
import threading
import time
import urllib.error
import urllib.request

DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 600
stop = threading.Event()
lock = threading.Lock()
bot_err = [0]
bot_lat = []
bot_ops = [0]

# churn 与 bot 必须用不同账号：churn 的高频 relogin 会不断顶号踢线，
# 若撞上 bot 的账号，bot 的报错只会反映「被顶号」这一正确业务语义。
CHURN_USER = "chaos_churn"

def api(path, obj, timeout=10):
    req = urllib.request.Request("http://127.0.0.1:8099" + path)
    req.add_header("Content-Type", "application/json")
    try:
        r = urllib.request.urlopen(req, json.dumps(obj).encode(), timeout=timeout)
        return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode())
        except Exception:
            return e.code, {}
    except Exception:
        return 0, {}

api("/api/game/register", {"user": CHURN_USER, "pass": "pass123"})

def garbler(mode):
    while not stop.is_set():
        try:
            s = socket.socket()
            s.settimeout(2)
            s.connect(("127.0.0.1", 7100))
            try:
                s.recv(4096)
            except OSError:
                pass
            if mode == 0:
                s.sendall(os.urandom(4096))
            elif mode == 1:
                s.sendall(b'{"op":"hb","pad":"' + b"A" * 300000 + b'"}\n')
            else:
                s.sendall(b'{"op":"say","text":"hel')
                time.sleep(0.2)
                s.sendall(b'lo"}\n{"op":"state"}\n')
            time.sleep(0.3)
            s.close()
        except OSError:
            pass
        time.sleep(0.4)

def churn():
    while not stop.is_set():
        try:
            s = socket.socket()
            s.settimeout(3)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                         struct.pack("ii", 1, 0))
            s.connect(("127.0.0.1", 7100))
            s.recv(4096)
            s.sendall((f'{{"op":"login","user":"{CHURN_USER}",'
                       f'"pass":"pass123"}}\n').encode())
            s.recv(65536)
            s.close()  # RST
        except OSError:
            pass
        time.sleep(1)

def bot(idx):
    while not stop.is_set():
        try:
            st, j = api("/api/game/login",
                        {"user": f"perf{idx:02d}", "pass": "pass123"})
            tok = j.get("token", "")
            if not tok:
                with lock:
                    bot_err[0] += 1
                time.sleep(1)
                continue
            api("/api/game/enter", {"op": "enter", "token": tok, "realm": 1})
            for _ in range(5):
                t0 = time.perf_counter()
                st, j = api("/api/game/state", {"op": "state", "token": tok})
                dt = (time.perf_counter() - t0) * 1000
                if j.get("ok") != 1:
                    with lock:
                        bot_err[0] += 1
                else:
                    with lock:
                        bot_ops[0] += 1
                        bot_lat.append(dt)
                time.sleep(1)
        except Exception:
            with lock:
                bot_err[0] += 1
            time.sleep(1)

def monitor():
    while not stop.is_set():
        row = {"t": time.time()}
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                with open(f"/proc/{pid}/comm") as f:
                    if f.read().strip() != "server-game":
                        continue
                rss = 0
                with open(f"/proc/{pid}/status") as f:
                    for line in f:
                        if line.startswith("VmRSS:"):
                            rss = int(line.split()[1])
                row[pid] = (rss, len(os.listdir(f"/proc/{pid}/fd")))
            except OSError:
                continue
        with open("/tmp/chaos_mon.jsonl", "a") as f:
            f.write(json.dumps(row) + "\n")
        time.sleep(5)

threads = [threading.Thread(target=garbler, args=(i,), daemon=True)
           for i in range(3)]
threads += [threading.Thread(target=churn, daemon=True) for _ in range(3)]
threads += [threading.Thread(target=bot, args=(i,), daemon=True)
            for i in range(2)]
mon = threading.Thread(target=monitor, daemon=True)
t0 = time.time()
mon.start()
for t in threads:
    t.start()
while time.time() - t0 < DUR:
    time.sleep(5)
stop.set()
time.sleep(2)

print(f"CHAOS dur={time.time() - t0:.0f}s bot_ops={bot_ops[0]} "
      f"bot_err={bot_err[0]}")
if bot_lat:
    print(f"CHAOS bot p50={statistics.median(bot_lat):.0f}ms n={len(bot_lat)}")
st, j = api("/api/game/realms", {})
alive = st == 200 and j.get("ok") == 1
print(f"CHAOS post-check realms alive={alive}")
u = f"chaos_{int(time.time()) % 100000}"
api("/api/game/register", {"user": u, "pass": "pass123"})
st, j = api("/api/game/login", {"user": u, "pass": "pass123"})
tok = j.get("token", "")
api("/api/game/create", {"op": "create", "token": tok, "realm": 1,
                         "name": u, "cls": 1})
st, j = api("/api/game/enter", {"op": "enter", "token": tok, "realm": 1})
flow = j.get("ok") == 1
print(f"CHAOS post-check fresh flow enter ok={flow}")
sys.exit(0 if (alive and flow and bot_err[0] == 0) else 1)

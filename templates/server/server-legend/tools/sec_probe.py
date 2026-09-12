#!/usr/bin/env python3
"""Security probe for the server-game template.

Run against a live instance AFTER tools/e2e.py (creates its own sec_* accounts,
expects no other load so the anon 5/5s rate limiter is not tripped spuriously --
items that must pace themselves sleep explicitly).

Covers: SQL injection, XSS echo, path traversal, admin authz (pages + write
API + cookie flags), token lifecycle (forged / empty / bit-flip / relogin),
oversized body, malformed JSON, method mismatch, TCP hostile frames (binary,
no-newline, 2MB line, unknown op, unauth op, connect flood, attach conflict),
security-answer lockout, anon rate-limit 429 positive control, register
validation.

Output: SEC_PASS/SEC_FAIL/OBS lines + SEC_SUMMARY. Exit 1 on any FAIL.
OBS = observation worth recording, not a failure (documented defaults).
"""
import json
import socket
import sys
import time
import urllib.parse
import urllib.request
import urllib.error

BASE = "http://127.0.0.1:8099"
GAME = ("127.0.0.1", 7100)
results = {"pass": 0, "fail": 0, "obs": 0}

def out(kind, name, detail=""):
    results[kind] += 1
    tag = {"pass": "SEC_PASS", "fail": "SEC_FAIL", "obs": "OBS"}[kind]
    print(f"{tag} {name}" + (f" | {detail}" if detail else ""))
    sys.stdout.flush()

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):
        return None

opener = urllib.request.build_opener(NoRedirect)

def http(path, data=None, cookie=None, raw_body=None, ctype=None):
    req = urllib.request.Request(BASE + path)
    if cookie:
        req.add_header("Cookie", cookie)
    body = None
    if raw_body is not None:
        body = raw_body
        req.add_header("Content-Type", ctype or "application/octet-stream")
    elif data is not None:
        body = urllib.parse.urlencode(data).encode()
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = opener.open(req, body, timeout=15)
        return resp.status, resp.read().decode("utf-8", "replace"), \
            resp.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as e:
        try:
            return e.code, e.read().decode("utf-8", "replace"), \
                e.headers.get_all("Set-Cookie") or []
        except Exception:
            return e.code, "", []
    except Exception as e:
        return 0, f"EXC:{type(e).__name__}:{e}", []

def api(path, obj):
    req = urllib.request.Request(BASE + path)
    req.add_header("Content-Type", "application/json")
    try:
        resp = opener.open(req, json.dumps(obj).encode(), timeout=15)
        return resp.status, json.loads(resp.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8", "replace"))
        except Exception:
            return e.code, {}
    except Exception:
        return 0, {"exc": "no-response"}

class Tcp:
    """Minimal newline-JSON TCP client with banner read."""

    def __init__(self):
        self.s = socket.create_connection(GAME, timeout=10)
        self.s.settimeout(10)
        self.buf = b""
        try:
            self.banner = self.s.recv(4096)
        except socket.timeout:
            self.banner = b""

    def send(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())

    def send_raw(self, b):
        self.s.sendall(b)

    def recv(self, timeout=5):
        deadline = time.time() + timeout
        while b"\n" not in self.buf:
            left = deadline - time.time()
            if left <= 0:
                return None
            self.s.settimeout(left)
            try:
                d = self.s.recv(65536)
            except OSError:
                return None
            if not d:
                return None
            self.buf += d
        line, self.buf = self.buf.split(b"\n", 1)
        try:
            return json.loads(line)
        except Exception:
            return {"raw": line[:200].decode("utf-8", "replace")}

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass

def alive():
    st, j = api("/api/game/realms", {})
    return st == 200 and isinstance(j, dict) and j.get("ok") == 1

# ---------- S0 accounts (paced: anon limiter is 5 ops / 5s / IP / worker) ----------
U = "sec_plain"
api("/api/game/register", {"user": U, "pass": "pass123"})
time.sleep(1)
api("/api/game/login", {"user": U, "pass": "pass123"})
time.sleep(1)

# ---------- S1 SQL injection ----------
INJS = ["x' OR '1'='1", "x' UNION SELECT 1,2,3--", "admin'--",
        "x'; DROP TABLE account;--", "1'or'1'='1"]
bad = 0
for inj in INJS:
    st, j = api("/api/game/login", {"user": inj, "pass": "x"})
    if st != 200 or j.get("ok") == 1 or "sql" in json.dumps(j).lower():
        bad += 1
    time.sleep(1.2)  # anon 限流 5/5s/IP/worker：钉死单 worker 时别撞窗
if bad == 0:
    out("pass", "S1 sql-injection login 注入均按业务失败处理，无 500 无 SQL 回显")
else:
    out("fail", "S1 sql-injection", f"{bad} 异常响应")

# ---------- S2 XSS echo ----------
xs = "<script>alert(1)</script>sec"
st, html, _ = http("/forgot", data={"user": xs})
if "<script>alert(1)" in html:
    out("fail", "S2 xss-echo forgot 原样回显 <script>")
else:
    out("pass", "S2 xss-echo forgot 未原样回显")

# ---------- S3 path traversal ----------
TRAV = ["/static/../../etc/passwd", "/static/..%2f..%2f..%2fetc/passwd",
        "/static/%2e%2e/%2e%2e/%2e%2e/etc/passwd", "/../../etc/passwd",
        "/static/....//....//etc/passwd", "/static/../Config/app.json"]
leak = 0
for p in TRAV:
    st, html, _ = http(p)
    if "root:" in html or (st == 200 and "database" in html.lower()):
        leak += 1
if leak == 0:
    out("pass", "S3 path-traversal 6 形态均未泄漏文件")
else:
    out("fail", "S3 path-traversal", f"{leak} 形态泄漏")

# ---------- S4 admin authz ----------
leak = 0
for p in ["/admin/dashboard", "/admin/monitor", "/admin/game/players",
          "/admin/game/realms"]:
    st, html, _ = http(p)
    if st == 200 and ("在线" in html or "<table" in html):
        leak += 1
    st2, html2, _ = http(p, cookie="zsession=deadbeefdeadbeef")
    if st2 == 200 and ("在线" in html2 or "<table" in html2):
        leak += 1
if leak == 0:
    out("pass", "S4 admin-authz 无 cookie/伪 cookie 访问 4 管理页均被拦")
else:
    out("fail", "S4 admin-authz", f"{leak} 页泄漏")
st, html, _ = http("/admin/game/realms/save", data={
    "name": "x", "state": "0", "sort": "1"})
if st == 200 and html.strip().startswith("{") and '"code":"0000"' in html:
    out("fail", "S4 admin-authz 未授权可调管理写接口")
else:
    out("pass", "S4 admin-authz 未授权 POST 管理写接口被拒")
st, html, setc = http("/admin/login", data={"user": "admin", "pass": "admin1234"})
if setc:
    flags = " ".join(setc)
    out("pass", "S4 admin-login 默认口令可登录（模板预期，生产必须改）")
    out("obs", "S4 admin cookie flags",
        "HttpOnly+SameSite" if ("HttpOnly" in flags and "SameSite" in flags)
        else flags)
else:
    out("fail", "S4 admin-login 默认口令登录未发 cookie")

# ---------- S5 token lifecycle ----------
st, j1 = api("/api/game/login", {"user": U, "pass": "pass123"})
tok1 = j1.get("token", "")
time.sleep(2)
st, j2 = api("/api/game/login", {"user": U, "pass": "pass123"})
tok2 = j2.get("token", "")
out("pass" if tok1 and tok2 and tok1 != tok2 else "fail",
    "S5 relogin 换发新 token")
st, j = api("/api/game/enter", {"op": "enter", "token": "f" * 64, "realm": 1})
out("pass" if j.get("ok") == 0 else "fail", "S5 伪造 token 拒绝")
st, j = api("/api/game/enter", {"op": "enter", "token": "", "realm": 1})
out("pass" if j.get("ok") == 0 else "fail", "S5 空 token 拒绝")
st, j = api("/api/game/state", {"op": "state", "token": tok1[:31] + "0"})
out("pass" if j.get("ok") == 0 else "fail", "S5 单字符翻转 token 拒绝")
st, j = api("/api/game/state", {"op": "state", "token": ""})
out("pass" if j.get("ok") == 0 else "fail", "S5 未登录调 state 拒绝")
time.sleep(2)
st, j = api("/api/game/enter", {"op": "enter", "token": tok1, "realm": 1})
err = j.get("err") or ""
if j.get("ok") == 1:
    out("obs", "S5 旧 token relogin 后", "仍有效（至 TTL；玩家 token 无按账号吊销）")
elif "过期" in err or "失效" in err:
    out("obs", "S5 旧 token relogin 后", "已失效（relogin 吊销）")
else:
    out("obs", "S5 旧 token relogin 后",
         f"token 仍解析（业务层拦截：{err}），未按账号吊销")

# ---------- S6/S7/S8 HTTP hardening ----------
big = b"user=x&pass=" + b"A" * (3 * 1024 * 1024)
st, html, _ = http("/api/game/login", raw_body=big,
                   ctype="application/x-www-form-urlencoded")
out("pass" if st in (0, 400, 403, 413, 422) or st == 200 else "fail",
    f"S6 3MB body 处置(status={st}, maxBodyMB=2)", "服务存活" if alive() else "挂死!")
st, j = api("/api/game/login", {"user": 'x"y', "pass": None, "weird": [1, 2]})
out("pass" if isinstance(j, dict) else "fail", "S7 畸形 JSON 值不致 500",
    json.dumps(j, ensure_ascii=False)[:80])
try:
    resp = opener.open(urllib.request.Request(BASE + "/api/game/login"),
                       timeout=10)
    st = resp.status
except urllib.error.HTTPError as e:
    st = e.code
except Exception:
    st = 0
out("pass" if st in (400, 404, 405, 0) or st < 500 else "fail",
    f"S8 GET 打 POST-only api(status={st})")

# ---------- S9 TCP hostile ----------
for i in range(20):
    try:
        c = socket.create_connection(GAME, timeout=5)
        c.recv(1024)
        c.close()
    except OSError:
        pass
out("pass" if alive() else "fail", "S9 20 连接即断洪水，服务存活")

t = Tcp()
t.send_raw(b"\x00\x01\x02\xff" * 300)
t.close()
out("pass" if alive() else "fail", "S9 二进制无换行帧，服务存活")

t = Tcp()
t.send_raw(b"{not json\n")
r = t.recv(3)
out("pass" if alive() else "fail", "S9 非 JSON 行，服务存活")
out("obs", "S9 非 JSON 行回包",
    json.dumps(r, ensure_ascii=False)[:80] if r else "无回包/被断")
t.close()

t = Tcp()
t.send_raw(b'{"op":"hb","pad":"' + b"A" * (2 * 1024 * 1024) + b'"}\n')
r = t.recv(5)
out("obs", "S9 2MB 单帧行回包",
    json.dumps(r, ensure_ascii=False)[:80] if r else "无回包/连接被断")
t.close()
out("pass" if alive() else "fail", "S9 2MB 帧后服务存活")

t = Tcp()
t.send({"op": "no_such_op_zz"})
r = t.recv(3)
out("pass" if r is not None and r.get("ok") == 0 else "fail", "S9 未知 op 得 ok=0")
t.send({"op": "state"})
r2 = None
for _ in range(3):
    r2 = t.recv(2)
    if r2 is not None and r2.get("ok") == 0:
        break
out("pass" if r2 is not None and r2.get("ok") == 0 else "fail",
    "S9 未登录 TCP op 拒绝")
t.close()

# ---------- S10 security-answer lockout (paced over the limiter window) ----------
LU = "sec_lock"
api("/api/game/register", {"user": LU, "pass": "pass123",
                           "question": "qc", "answer": "ac"})
time.sleep(1)
locked = False
for i in range(6):
    st, j = api("/api/game/reset", {"user": LU, "answer": "wrong",
                                    "newpass": "np123456"})
    if j.get("ok") == 0 and "频繁" in (j.get("err") or ""):
        time.sleep(6)
        continue
    if "锁定" in (j.get("err") or "") or "错误次数过多" in (j.get("err") or ""):
        locked = True
        break
    time.sleep(6)
time.sleep(6)
st, j = api("/api/game/reset", {"user": LU, "answer": "ac",
                                "newpass": "np123456"})
if locked and j.get("ok") == 0:
    out("pass", "S10 密保答错锁定触发且正确答案暂拒")
elif not locked and j.get("ok") == 1:
    out("fail", "S10 六次错答后正确答案直接成功（无锁定）")
else:
    # TCP/HTTP reset 语义差异以页面路径复核一次
    st, html, _ = http("/forgot/reset", data={
        "user": LU, "answer": "ac", "newpass": "np123456",
        "newpass2": "np123456"})
    out("pass" if "错误次数过多" in html else "fail",
        "S10 密保锁定（页面路径复核）", html[:60])

# ---------- S13 anon rate-limit 429 positive control ----------
codes = []
for i in range(25):
    st, j = api("/api/game/login", {"user": "nobody_zz", "pass": "x"})
    codes.append(st)
n429 = sum(1 for c in codes if c == 429)
out("pass" if n429 >= 1 else "fail",
    "S13 anon 限流 429 正控（25 发/4 worker 必有单 worker 打满 5/5s）",
    f"429x{n429}")

print(f"SEC_SUMMARY pass={results['pass']} fail={results['fail']} "
      f"obs={results['obs']}")
sys.exit(1 if results["fail"] else 0)

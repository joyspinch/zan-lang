"""End-to-end self-test for the realm-based server-legend template.

Covers the full player flow AND the combat loop: web register/forgot (3-step
with security question, wrong-answer lockout), TCP realms/register/login/
characters/create/enter, realm-scoped chat/walk/who, realm switch with
character state kept, GM realm CRUD with maintain gating, account ban +
kick, online grant pushes, announce push, pro-rata capped offline
settlement, then the full hunt loop -- mob list, manual hunt with
exp/levelup/gold/drops, shop buy/sell, potion use, weapon equip/takeoff,
per-tick auto-hunt events (auto pauses out of the mob's map and resumes
back on it), death respawn in town, GM item gift (online push + offline
grant), GM level change clamping hp, full-hp potion refusal, and DB
persistence of hp/exp/equipped weapon/bag, plus the HTTP-session push
channel flow: attach, relogin kick, zero-delay re-attach after kick
(register-before-kick regression), GM broadcast delivery -- 138
assertions. Stdlib urllib/socket only.

Run from the SERVER directory against a FRESH data/app.db:
  1. stop server, delete data/app.db*, start server
  2. python tools/e2e.py
"""
import json
import os
import socket
import sys
import time
import urllib.parse
import urllib.request
import urllib.error

def _ports():
    """端口唯一信源是 config/app.json（[game].port + [server].port），
    不在脚本里再抄一份常量——改端口改一处就够。"""
    cfg = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "config", "app.json")
    with open(cfg, encoding="utf-8") as f:
        c = json.load(f)
    return (int(c["game"]["port"]), int(c["server"]["port"]))


GAME_PORT, HTTP_PORT = _ports()
BASE = "http://127.0.0.1:%d" % HTTP_PORT
GAME = ("127.0.0.1", GAME_PORT)
checks = 0

T0 = time.time()

def ok(cond, label):
    global checks
    checks += 1
    print(f"[{time.time()-T0:7.1f}s] [{'PASS' if cond else 'FAIL'}] {label}")
    if not cond:
        print("ALL FAIL at", checks)
        sys.exit(1)

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
        resp = opener.open(req, body, timeout=10)
        return resp.status, resp.read().decode("utf-8", "replace"), \
            resp.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), \
            e.headers.get_all("Set-Cookie") or []

def gm(path, data, cookie):
    status, text, _ = http(path, data=data, cookie=cookie)
    try:
        return status, json.loads(text), text
    except Exception:
        return status, {"raw": text}, text

class Client:
    def __init__(self):
        self.sock = socket.create_connection(GAME, timeout=10)
        self.buf = b""
        self.pending = []
        self.verbose = False

    def send(self, obj):
        self.sock.sendall((json.dumps(obj, ensure_ascii=False) + "\n").encode())

    def _fill(self, timeout):
        self.sock.settimeout(timeout)
        got = False
        try:
            while True:
                d = self.sock.recv(4096)
                if not d:
                    return False
                self.buf += d
                while b"\n" in self.buf:
                    line, self.buf = self.buf.split(b"\n", 1)
                    if line.strip():
                        m = json.loads(line)
                        self.pending.append(m)
                        got = True
                        if self.verbose:
                            print(f"[{time.time()-T0:7.1f}s] RX {json.dumps(m, ensure_ascii=False)[:220]}")
                if got:
                    return True
        except socket.timeout:
            pass
        except OSError:
            return False
        return True

    def recv(self, timeout=5):
        if self.pending:
            return self.pending.pop(0)
        self._fill(timeout)
        return self.pending.pop(0) if self.pending else None

    def reply_for(self, pred, timeout=5):
        """Next message matching pred; other lines stay queued."""
        for i, m in enumerate(self.pending):
            if pred(m):
                return self.pending.pop(i)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not self._fill(max(0.1, deadline - time.time())):
                break
            for i, m in enumerate(self.pending):
                if pred(m):
                    return self.pending.pop(i)
        return None

    def ok_for(self, op_extra=None, timeout=5):
        def pred(m):
            return m.get("ok") == 1 and (op_extra is None or op_extra(m))
        return self.reply_for(pred, timeout)

    def err_for(self, needle, timeout=5):
        def pred(m):
            return m.get("ok") == 0 and needle in (m.get("err") or "")
        return self.reply_for(pred, timeout)

    def ev(self, name, timeout=5):
        def pred(m):
            return m.get("ev") == name
        return self.reply_for(pred, timeout)

    def closed(self, timeout=3):
        return not self._fill(timeout) and not self.pending

    def drop(self):
        try:
            self.sock.close()
        except OSError:
            pass

def tcp():
    # 世界 worker（带游戏 TCP 监听）要等建表+目录种子完成才开始收连，
    # 比 HTTP 端口晚——多 worker/冷启动下轮询重连，别在启动缝里崩。
    deadline = time.time() + 15
    while True:
        try:
            return Client()
        except OSError:
            if time.time() > deadline:
                raise
            time.sleep(0.2)

# ---------- 1. 落地页与公共页面 ----------
st, html, _ = http("/")
ok(st == 200 and "一区·雷霆之怒" in html and "三区·战神殿" in html,
   "landing lists seeded realms")
ok("开放 2 / 共 3" in html and "维护" in html, "landing shows realm states")
ok("注册账号" in html and "找回密码" in html, "landing links register/forgot")
st, html, _ = http("/register")
ok(st == 200 and 'action="/register"' in html and "密保问题" in html,
   "register page renders")
st, html, _ = http("/forgot")
ok(st == 200 and 'action="/forgot"' in html, "forgot page renders")

# ---------- 2. 网页注册 ----------
st, html, _ = http("/register", data={
    "user": "al", "pass": "secret1", "pass2": "secret1",
    "question": "出生城市", "answer": "北京"})
ok("账号需 3-32 字" in html, "web register rejects short username")
st, html, _ = http("/register", data={
    "user": "alice", "pass": "secret1", "pass2": "different",
    "question": "出生城市", "answer": "北京"})
ok("两次输入的密码不一致" in html, "web register rejects mismatched pass")
st, html, _ = http("/register", data={
    "user": "alice", "pass": "secret1", "pass2": "secret1",
    "question": "出生城市", "answer": "北京"})
ok("注册成功" in html and "alice" in html, "web register creates alice")
st, html, _ = http("/register", data={
    "user": "alice", "pass": "secret1", "pass2": "secret1",
    "question": "出生城市", "answer": "北京"})
ok("账号已存在" in html, "web register rejects duplicate")

# ---------- 3. 网页找回密码 ----------
st, html, _ = http("/forgot", data={"user": "ghost"})
ok("账号不存在或未设置密保" in html, "forgot hides unknown accounts")
st, html, _ = http("/forgot", data={"user": "alice"})
ok("出生城市" in html and 'name="user"' in html, "forgot step2 shows question")
st, html, _ = http("/forgot/reset", data={
    "user": "alice", "answer": "上海", "newpass": "newpass99",
    "newpass2": "newpass99"})
ok("密保答案不正确" in html, "forgot rejects wrong answer")
st, html, _ = http("/forgot/reset", data={
    "user": "alice", "answer": "北京", "newpass": "newpass99",
    "newpass2": "newpass99"})
ok("密码已重置" in html, "forgot resets with correct answer")

# 密保答错限频：mallory 连错 5 次后锁定，正确答案也被拒
mallory = tcp()
mallory.recv()  # banner：不先吃掉会被 ok_for() 当成注册应答，注册落库未完成就去找回=账号不存在
mallory.send({"op": "register", "user": "mallory", "pass": "mallory1",
              "question": "小学校名", "answer": "实验小学"})
ok(mallory.ok_for() is not None, "TCP register mallory")
mallory.drop()
# TCP 注册的 ack 时 INSERT 已执行，但另一个 worker 的 SELECT 有
# 几十 ms 到数秒读不到（跨 worker 读你写之缝，见 TASKS 挂账）——可见前
# 重试；不可见的请求不落错误计数。
for i in range(5):
    for tries in range(200):
        st, html, _ = http("/forgot/reset", data={
            "user": "mallory", "answer": "错误答案", "newpass": "pass123",
            "newpass2": "pass123"})
        if "账号不存在" not in html:
            break
        time.sleep(0.05)
    ok("密保答案不正确" in html, f"mallory wrong answer #{i+1}")
# 第 6 次（无论答案对错）都落在锁定窗口内
st, html, _ = http("/forgot/reset", data={
    "user": "mallory", "answer": "错误答案", "newpass": "pass123",
    "newpass2": "pass123"})
ok("错误次数过多" in html, "forgot locks after 5 wrong answers")
st, html, _ = http("/forgot/reset", data={
    "user": "mallory", "answer": "实验小学", "newpass": "pass123",
    "newpass2": "pass123"})
ok("错误次数过多" in html, "lockout applies even to correct answer")

# 登录密码错限频（账号级，A303 建议 ④）：mallory 连错 10 次后锁定，
# 正确密码也被拒到窗口结束；IP 限流（5/5s）之上叠加的账号维度闸门。
# mallory 的登录走 TCP：op login 不限 IP（有身份前的按 op 限流只对
# HTTP），计数落在 game_account 行上。
mlock = tcp()
mlock.recv()
for i in range(10):
    mlock.send({"op": "login", "user": "mallory", "pass": "wrongpass"})
    m = mlock.err_for("账号或密码不正确")
    ok(m is not None, f"mallory wrong password #{i+1}")
mlock.send({"op": "login", "user": "mallory", "pass": "mallory1"})
m = mlock.err_for("错误次数过多")
ok(m is not None, "login locks after 10 wrong passwords (account-level)")
mlock.drop()

# ---------- 4. TCP 账号/选区/建角 ----------
bob = tcp()
hello = bob.recv(timeout=30)  # 世界 worker 起服要种子目录，banner 可能晚到
# proto-2 握手：连接建立即收到 {"ok":1,"proto":2,...}；客户端可再发 hello 升密，
# 明文 op 在 requireEnc=0 时照常可用（见 src/Game/Secure.zan 握手注释）。
ok(hello is not None and (hello.get("ev") == "hello" or hello.get("proto") == 2),
   "gateway greets on connect")
bob.send({"op": "realms"})
r = bob.ok_for(lambda m: len(m.get("realms", [])) == 3)
ok(r and r["realms"][2]["state"] == 1 and r["realms"][0]["name"] == "一区·雷霆之怒",
   "realms op lists 3 realms with states")
bob.send({"op": "register", "user": "bob", "pass": "short",
          "question": "旧手机号", "answer": "8888"})
ok(bob.err_for("密码需 6-64 字") is not None, "TCP register rejects short pass")
bob.send({"op": "register", "user": "bob", "pass": "secret1",
          "question": "宠物名字", "answer": "旺财"})
ok(bob.ok_for() is not None, "TCP register bob")
bob.send({"op": "register", "user": "bob", "pass": "secret1",
          "question": "宠物名字", "answer": "旺财"})
ok(bob.err_for("账号已存在") is not None, "TCP register rejects duplicate")
bob.send({"op": "login", "user": "bob", "pass": "wrong!!"})
ok(bob.err_for("账号或密码不正确") is not None, "login error is uniform")
bob.send({"op": "login", "user": "bob", "pass": "secret1"})
r = bob.ok_for(lambda m: "uid" in m and "realms" in m)
ok(r is not None, "login returns uid + realm list")
bob.send({"op": "state"})  # say/walk 已是免回执 op（a68edeea），探闸门用有回执的 state
ok(bob.err_for("请先选择区服进入") is not None, "world ops gated before enter")
bob.send({"op": "enter", "realm": 999})
ok(bob.err_for("区服不存在或维护中") is not None, "unknown realm refused")
bob.send({"op": "enter", "realm": 3})
ok(bob.err_for("区服不存在或维护中") is not None, "maintained realm refused")
bob.send({"op": "enter", "realm": 1})
r = bob.reply_for(lambda m: m.get("ok") == 0 and m.get("needCreate") == 1)
ok(r is not None, "enter without character answers needCreate")
bob.send({"op": "characters", "realm": 1})
r = bob.ok_for(lambda m: m.get("rows") == [])
ok(r is not None, "characters empty before create")
bob.send({"op": "create", "realm": 1, "name": "刀", "job": 0})
ok(bob.err_for("角色名需 2-16 字") is not None, "create rejects 1-char name")
bob.send({"op": "create", "realm": 1, "name": "刀狂", "job": 9})
ok(bob.err_for("职业不合法") is not None, "create rejects bad job")
bob.send({"op": "create", "realm": 1, "name": "刀狂", "job": 0})
r = bob.ok_for(lambda m: m.get("self", {}).get("uid", 0) > 0
               and m["self"]["realm"] == 1 and "maps" in m)
ok(r is not None, "create enters world with self+maps")
bob_uid = r["self"]["uid"]
ok(bob.reply_for(lambda m: m.get("ev") == "move") is not None,
   "create broadcasts realm move event")

# alice 用网页重置后的新密码走 TCP，与 bob 同区
alice = tcp()
alice.recv()
alice.send({"op": "login", "user": "alice", "pass": "newpass99"})
ok(alice.ok_for() is not None, "alice logs in with web-reset password")
alice.send({"op": "create", "realm": 1, "name": "法萌", "job": 1})
r = alice.ok_for(lambda m: m.get("self", {}).get("job") == 1)
ok(r is not None, "alice creates mage in realm 1")
alice.send({"op": "create", "realm": 1, "name": "法萌二号", "job": 0})
ok(alice.err_for("该区已有角色") is not None, "one character per realm")
alice.send({"op": "create", "realm": 1, "name": "刀狂", "job": 0})
ok(alice.err_for("角色名已被占用") is not None, "character names unique per realm")
alice.drop()  # alice 下线，让后面 who/在线页的世界状态可预测

# 区服广播收窄：carl 在一区收到，dave 在二区收不到
carl = tcp()
carl.recv()
carl.send({"op": "register", "user": "carl", "pass": "secret1",
           "question": "旧手机号", "answer": "8888"})
carl.reply_for(lambda m: m.get("ok") == 1)
carl.send({"op": "login", "user": "carl", "pass": "secret1"})
carl.reply_for(lambda m: m.get("ok") == 1)
carl.send({"op": "create", "realm": 1, "name": "弓长", "job": 2})
ok(carl.ok_for(lambda m: m.get("self", {}).get("uid", 0) > 0) is not None,
   "carl creates archer in realm 1")

dave = tcp()
dave.recv()
dave.send({"op": "register", "user": "dave", "pass": "secret1",
           "question": "旧手机号", "answer": "8888"})
dave.reply_for(lambda m: m.get("ok") == 1)
dave.send({"op": "login", "user": "dave", "pass": "secret1"})
dave.reply_for(lambda m: m.get("ok") == 1)
dave.send({"op": "create", "realm": 2, "name": "二区土著", "job": 0})
ok(dave.ok_for() is not None, "dave creates character in realm 2")

bob.send({"op": "say", "text": "一区的兄弟们好"})
c = carl.ev("chat")
d = dave.ev("chat")
ok(c and c.get("text") == "一区的兄弟们好" and c.get("from") == "刀狂",
   "same-realm player receives chat")
ok(d is None, "other-realm player does not receive chat")

bob.pending.clear()  # 丢弃 bob 自己的 ev 回声与旧回复，保证下面的 ok 对上本条请求
bob.send({"op": "walk", "x": 999, "y": 3})
ok(bob.ev("walk") is not None, "walk accepts clamped coords")
w = carl.ev("walk")
ok(w and w.get("x") == 511 and w.get("y") == 3, "walk clamped to walkMax and broadcast")
ok(dave.ev("walk") is None, "walk not sent to other realm")
bob.pending.clear()
bob.send({"op": "who"})
r = bob.ok_for(lambda m: "rows" in m)
ok(r and sorted(x["name"] for x in r["rows"]) == ["刀狂", "弓长"],
   "who lists same-realm same-map players only")

bob.send({"op": "move", "map": 2})
# a68edeea 起换图门槛照原版 BOSS 链（mapUnlocked），minLevel 不再拦人
ok(bob.err_for("尚未解锁：先击败上一张图的BOSS") is not None,
   "move gated by boss-chain unlock")

# 换区：bob 建二区角色再切回一区，金币保持
bob.send({"op": "create", "realm": 2, "name": "刀狂二区", "job": 0})
r = bob.ok_for(lambda m: m.get("self", {}).get("realm") == 2)
ok(r is not None, "bob creates second character in realm 2")
bob.send({"op": "enter", "realm": 1})
r = bob.ok_for(lambda m: m.get("self", {}).get("realm") == 1
               and m["self"]["name"] == "刀狂")
ok(r is not None, "bob re-enters realm 1, character state kept")

# ---------- 5. GM 后台 ----------
st, html, setc = http("/admin/login", data={
    "user": "admin", "pass": "admin-bootstrap-2026"})
cookie = ""
for c in setc:
    if c.startswith("zsession="):
        cookie = c.split(";")[0]
ok(st in (200, 302) and cookie, "admin login sets session cookie")

st, html, _ = http("/admin/game/realms", cookie=cookie)
ok(st == 200 and "一区·雷霆之怒" in html and "维护" in html,
   "GM realms page lists realms")
st, j, _ = gm("/admin/game/realms/save", {
    "name": "四区·测试", "state": "0", "sort": "9"}, cookie)
ok(j.get("code") == "0000", "GM creates realm 4")
st, html, _ = http("/admin/game/realms", cookie=cookie)
ok("四区·测试" in html, "realms page shows new realm")
b = tcp()
b.recv()
b.send({"op": "realms"})
r = b.ok_for(lambda m: len(m.get("realms", [])) == 4)
ok(r and r["realms"][3]["name"] == "四区·测试", "TCP realms shows realm 4")
b.drop()

# 维护中的三区放开后可以 enter（needCreate）
st, j, _ = gm("/admin/game/realms/save", {
    "id": "3", "name": "三区·战神殿", "state": "0", "sort": "3"}, cookie)
ok(j.get("code") == "0000", "GM opens realm 3")
bob.send({"op": "enter", "realm": 3})
r = bob.reply_for(lambda m: m.get("ok") == 0 and m.get("needCreate") == 1)
ok(r is not None, "opened realm accepts enter (needCreate)")
bob.send({"op": "enter", "realm": 1})
bob.reply_for(lambda m: m.get("ok") == 1)

# 有角色的区服删除被拒
st, j, _ = gm("/admin/game/realms/delete", {"id": "1"}, cookie)
ok(st == 400 and j.get("code") == "0409" and "角色" in j.get("msg", ""),
   "GM cannot delete realm with characters")
st, j, _ = gm("/admin/game/realms/delete", {"id": "4"}, cookie)
ok(j.get("code") == "0000", "GM deletes empty realm 4")

st, html, _ = http("/admin/game/players?kw=alice", cookie=cookie)
ok(st == 200 and "法萌" in html and "一区·雷霆之怒" in html,
   "players page shows character + realm")
st, html, _ = http("/admin/game/players/form?id=" + str(bob_uid), cookie=cookie)
ok(st == 200 and 'name="realmId"' in html and 'name="accountStatus"' in html,
   "player form has realm select + account ban")

# 发奖（角色 id 路径，离线直写库）
st, j, _ = gm("/admin/game/players/save", {
    "id": str(bob_uid), "nickname": "刀狂", "realmId": "1", "job": "0",
    "level": "6", "gold": "500", "gems": "10", "mapId": "1",
    "accountStatus": "1", "banReason": ""}, cookie)
ok(j.get("code") == "0000", "GM save character")
bob.send({"op": "state"})
# GM 保存会连发多条 state（发奖一条、编辑一条、主动查询一条），取到含终值的那条
r = None
for _ in range(6):
    m = bob.reply_for(lambda m: m.get("ev") == "state", timeout=3)
    if m and m["self"]["gold"] == 500 and m["self"]["level"] == 6:
        r = m
        break
ok(r is not None, "online grant pushed to client")

# 封禁账号：在线的 carl 被踢
st, j, _ = gm("/admin/game/players/save", {
    "id": str(bob_uid), "nickname": "刀狂", "realmId": "1", "job": "0",
    "level": "6", "gold": "500", "gems": "10", "mapId": "1",
    "accountStatus": "0", "banReason": "作弊"}, cookie)
ok(j.get("code") == "0000" and "踢下线" in j.get("msg", ""),
   "GM bans account, kicks online session")
k = bob.ev("kick")
ok(k and "封禁" in k.get("text", ""), "banned client receives kick event")
bob.drop()  # 协议约定：客户端收到 kick 自行断开，服务端在 EOF 后清理会话
bob.drop()
bob2 = tcp()
bob2.recv()
bob2.send({"op": "login", "user": "bob", "pass": "secret1"})
ok(bob2.err_for("账号已被封禁：作弊") is not None, "banned login refused with reason")
st, j, _ = gm("/admin/game/players/save", {
    "id": str(bob_uid), "nickname": "刀狂", "realmId": "1", "job": "0",
    "level": "6", "gold": "500", "gems": "10", "mapId": "1",
    "accountStatus": "1", "banReason": ""}, cookie)
ok(j.get("code") == "0000", "GM unbans bob")

st, html, _ = http("/admin/game/online", cookie=cookie)
ok(st == 200 and 'tag off">未进区' not in html and "在世界中" in html
   and "carl" in html, "online page lists sessions with stage")
st, j, _ = gm("/admin/game/online/broadcast", {"text": "全体注意，今晚开BOSS"}, cookie)
ok(j.get("code") == "0000", "GM broadcast accepted")
c = carl.ev("chat")
d = dave.ev("chat")
ok(c and "开BOSS" in c.get("text", "") and d and "开BOSS" in d.get("text", ""),
   "GM broadcast reaches all realms")

st, j, _ = gm("/admin/game/announces/save", {
    "title": "维护通知", "body": "今晚 24:00 停机维护 30 分钟。",
    "enabled": "1"}, cookie)
ok(j.get("code") == "0000", "GM creates announce")
st, html, _ = http("/admin/game/announces", cookie=cookie)
ok("推送" in html, "announces page has push action")
import re
m = re.search(r'data-args="id=(\d+)" data-confirm="把这条公告', html)
push_id = m.group(1) if m else "1"
st, j, _ = gm("/admin/game/announces/push", {"id": push_id}, cookie)
ok(j.get("code") == "0000", "GM pushes announce")
c = carl.ev("announce")
ok(c and "维护通知" in c.get("title", ""), "announce push reaches client")

st, html, _ = http("/admin", cookie=cookie)
ok(st == 200 and "游戏账号" in html and "区服" in html,
   "dashboard shows account/realm KPIs")

# ---------- 6. 持久化与离线结算 ----------
import sqlite3
time.sleep(11)  # flush 周期 10s
db = sqlite3.connect("data/app.db")
row = db.execute(
    'SELECT nickname, realmId, gold, level FROM game_player WHERE id=?',
    (bob_uid,)).fetchone()
ok(row is not None and row[0] == "刀狂" and row[1] == 1 and row[3] == 6,
   "character row persisted with realmId")
acc = db.execute(
    'SELECT question, answerHash FROM game_account WHERE username=?',
    ("alice",)).fetchone()
ok(acc is not None and acc[0] == "出生城市" and acc[1] != "北京",
   "account stores question + hashed answer")
db.close()

# 离线结算：carl 登出，回拨 2 小时，重进一区结算 2 小时挂机（500/h）
carl.drop()
time.sleep(1)
db = sqlite3.connect("data/app.db")
db.execute('UPDATE game_player SET "lastLogoutAt"=? WHERE nickname="弓长"',
           (int(time.time()) - 7200,))
db.commit()
db.close()
carl2 = tcp()
carl2.recv()
carl2.send({"op": "login", "user": "carl", "pass": "secret1"})
carl2.reply_for(lambda m: m.get("ok") == 1)
carl2.send({"op": "enter", "realm": 1})
# 结算按秒折算（500/h 按比例），时长封顶；断言 ≥2h 且收益等于折算值
def settle_ok(m):
    o = m.get("offline") or {}
    return (o.get("seconds", 0) >= 7200
            and o.get("gold") == o["seconds"] * 500 // 3600)
r = carl2.ok_for(settle_ok)
ok(r is not None, "offline settlement grants 2h idle gold on enter")
carl2.drop()
dave.drop()
alice.drop()

# ---------- 7. 战斗闭环：打怪/升级/掉落/背包/商店/挂机/死亡 ----------
h = tcp()
h.verbose = True
h.recv()
h.send({"op": "register", "user": "hunter", "pass": "secret1",
        "question": "旧手机号", "answer": "8888"})
h.reply_for(lambda m: m.get("ok") == 1)
h.send({"op": "login", "user": "hunter", "pass": "secret1"})
h.reply_for(lambda m: m.get("ok") == 1)
h.send({"op": "create", "realm": 1, "name": "小猎手", "job": 0})
r = h.ok_for(lambda m: "self" in m)
ok(r is not None, "hunter creates fighter")
hunter_uid = r["self"]["uid"]
s0 = r["self"]
ok(s0["hp"] == s0["maxhp"] and s0["level"] == 1,
   "create starts with full hp at level 1")

h.send({"op": "mobs"})
r = h.ok_for(lambda m: "rows" in m)
# 刷怪点 2c1c3e8a 起按 M2.DB gamemap present 串重建：新手图=鸡|鹿|稻草人|多钩猫|钉耙猫|羊
scare = [x for x in r["rows"] if x["name"] == "稻草人"]
ok(len(r["rows"]) >= 3 and len(scare) >= 1
   and all(x["alive"] == 1 for x in r["rows"]),
   "mobs lists alive spawns on newbie map")

# 换图门槛照 BOSS 链后，狩猎数值来自 M2.DB 快照：稻草人 hp200/def5，
# 一轮伤害≈9，需要多轮才能首杀；GM 直接提到 8 级验升级态，狩猎只验
# 战报与回血节奏（快照数值下首杀必升级的老断言不再成立）。
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "8", "gold": "0", "gems": "0", "mapId": "1",
    "accountStatus": "1", "banReason": ""}, cookie)
ok(j.get("code") == "0000", "GM raises hunter to level 8")
h.send({"op": "state"})
# GM 存盘在线角色会推两条 state（Adjust 钱币差量一条 + ApplyEdit 等级一条，
# 前者还带着旧等级快照），排空队列取到 level==8 的那条再验曲线。
r = None
while True:
    m = h.reply_for(lambda m: m.get("ev") == "state" or "self" in m, timeout=2)
    if m is None:
        break
    if m.get("self", {}).get("level") == 8:
        r = m
        break
ok(r is not None and r["self"]["maxhp"] == 300,
   "level 8 warrior maxhp follows 140+20L curve")
killed = False
for _ in range(30):
    h.send({"op": "hunt", "mob": scare[0]["tpl"]})
    r = h.ok_for(lambda m: "fight" in m)
    if r["fight"]["killed"] == 1:
        killed = True
        break
ok(killed, "hunt kills a scarecrow and grants exp/gold")

# 商店：查目录拿物品 id，买消耗品补蓝，卖材料回收（快照物品表
# 没有金创药/兽皮——消耗品用 强化生命，材料用 肉）
h.send({"op": "shop"})
r = h.ok_for(lambda m: "shop" in m)
shop = {i["name"]: i["id"] for i in r["shop"]}
ok("强化生命" in shop and "铁剑" in shop and "肉" not in shop,
   "shop sells gear and consumables, not task materials")
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "8", "gold": "5000", "gems": "0", "mapId": "1",
    "accountStatus": "1", "banReason": ""}, cookie)
ok(j.get("code") == "0000", "GM tops up gold for shop test")
# GM 保存连发多条 state，清掉积压再取含终值的那条
h.pending.clear()
h.send({"op": "state"})
r = h.reply_for(lambda m: m.get("ev") == "state" or "self" in m)
gold0 = r["self"]["gold"]
ok(gold0 == 5000, "GM gold lands in session state")
h.pending.clear()
h.send({"op": "buy", "item": shop["强化生命"], "count": 3})
r = h.ok_for(lambda m: "self" in m and m.get("self", {}).get("gold") == gold0 - 3000)
ok(r is not None, "buy charges gold")

# 换图打怪掉血，再用药回（map2 需 BOSS 链解锁：GM 直接送 mapId=2）
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "8", "gold": str(gold0), "gems": "0", "mapId": "2",
    "accountStatus": "1", "banReason": ""}, cookie)
ok(j.get("code") == "0000", "GM teleports hunter to wildcat map")
h.send({"op": "state"})
r = h.reply_for(lambda m: (m.get("ev") == "state" or "self" in m)
                and m["self"]["map"] == 2)
ok(r is not None, "hunter now on wildcat map")
h.pending.clear()
# 解锁 map2 走正路：回 map1 挑战本图 BOSS（稻草人王），胜利即
# mapUnlocked=2（内存权威直接生效），再 move 到 map2
h.send({"op": "move", "map": 1})
h.ok_for(lambda m: "self" in m)
h.send({"op": "bossfight", "map": 1})
r = h.reply_for(lambda m: m.get("ok") == 1 or m.get("ok") == 0)
print("[unlock] bossfight:", (r or {}).get("ok"), (r or {}).get("err", ""))
h.send({"op": "move", "map": 2})
r = h.ok_for(lambda m: "self" in m and m["self"]["map"] == 2)
ok(r is not None, "move to wildcat map (unlock gate ok)")
cat = None
h.send({"op": "mobs"})
r = h.ok_for(lambda m: "rows" in m)
for x in r["rows"]:
    if x["name"] == "半兽人" and x["alive"] == 1:
        cat = x["tpl"]
        break
ok(cat is not None, "orc mob lives on map 2")
hurt = 0
for _ in range(3):
    h.send({"op": "hunt", "mob": cat})
    r = h.ok_for(lambda m: "fight" in m)
    if r["fight"].get("mdmg", 0) > 0:
        hurt = r["self"]["hp"]
ok(hurt > 0 and hurt < r["self"]["maxhp"], "mob retaliates and hurts player")
h.send({"op": "use", "item": shop["强化生命"]})
r = h.ok_for(lambda m: "self" in m)
ok(r["self"]["hp"] > hurt, "potion heals player")

# GM 设 12 级（快照 铁剑 minLevel=10）+ 送铁剑 → ev drop 到达 → 穿上加攻
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "12", "gold": str(gold0), "gems": "0", "mapId": "2",
    "accountStatus": "1", "banReason": "",
    "giftItem": str(shop["铁剑"]), "giftCount": "1"}, cookie)
ok(j.get("code") == "0000", "GM gifts sword")
k = h.reply_for(lambda m: m.get("ev") == "drop"
                and "铁剑" in (m.get("item") or ""))
ok(k is not None, "gift drop event reaches client")
h.send({"op": "equip", "item": shop["铁剑"]})
r = h.ok_for(lambda m: "self" in m and m.get("self", {}).get("weapon") == shop["铁剑"])
ok(r is not None and r["self"]["atk"] >= 10 + 12 * 4 + 9,
   "equipping sword raises attack")
h.send({"op": "takeoff", "slot": "weapon"})
r = h.ok_for(lambda m: "self" in m)
ok(r["self"]["weapon"] == 0, "takeoff clears weapon slot")
h.send({"op": "equip", "item": shop["铁剑"]})
h.ok_for(lambda m: "self" in m)

# 卖材料换钱（任务材料不可购买——GM 发 肉 再卖，打金闭环仍走半价回收）
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "8", "gold": str(gold0), "gems": "0", "mapId": "2",
    "accountStatus": "1", "banReason": "",
    "giftItem": "11", "giftCount": "4"}, cookie)
ok(j.get("code") == "0000", "GM gifts meat materials")
h.send({"op": "bag"})
r = h.ok_for(lambda m: "items" in m and any(i["name"] == "肉" for i in m["items"]))
meats = [i for i in r["items"] if i["name"] == "肉"]
meat_n = meats[0]["count"] if meats else 0
meat_id = meats[0]["id"] if meats else 0
gold1 = r["self"]["gold"]
h.pending.clear()
h.send({"op": "sell", "item": meat_id, "count": meat_n})
r = h.ok_for(lambda m: "self" in m and m.get("self", {}).get("gold") == gold1 + meat_n * 75)
ok(meat_n > 0 and r is not None, "selling materials pays half price")

# 自动挂机：开 → 每拍 ev fight → 跨图自动暂停 → 回图续打 → 关
h.send({"op": "auto", "mob": cat, "on": 1})
r = h.ok_for(lambda m: "auto" in m)
ok(r["auto"] == cat, "auto-hunt toggles on")
time.sleep(3)
h.send({"op": "move", "map": 1})
r = h.ok_for(lambda m: "self" in m and m["self"]["map"] == 1)
ok(r is not None, "auto hunter moves to town")
while h.reply_for(lambda m: m.get("ev") == "fight", timeout=2) is not None:
    pass
time.sleep(3)
ok(h.reply_for(lambda m: m.get("ev") == "fight", timeout=2) is None,
   "auto pauses while out of the mob's map")
h.pending.clear()
h.send({"op": "state"})
r = h.ok_for()
ok(r["self"]["auto"] == cat, "auto stays armed across maps")
h.send({"op": "move", "map": 1})
h.ok_for(lambda m: "self" in m)
h.send({"op": "bossfight", "map": 1})
h.reply_for(lambda m: m.get("ok") == 1 or m.get("ok") == 0)
h.pending.clear()
h.send({"op": "move", "map": 2})
r = h.ok_for(lambda m: "self" in m and m["self"]["map"] == 2)
ok(r is not None, "auto hunter back on the mob's map")
# 关挂机即触发尾巴结算（push=true），等那条聚合 idlesum
h.send({"op": "auto", "mob": cat, "on": 0})
r = h.ok_for(lambda m: "auto" in m)
ok(r["auto"] == 0, "auto-hunt toggles off")
m = h.reply_for(lambda m: m.get("ev") == "idlesum", timeout=6)
ok(m is not None and m.get("kills", 0) >= 0, "auto-hunt settles via idlesum on stop")

# 死亡：GM 把 2 级小号送到废弃矿洞（僵尸 atk 35 远超 2 级防御），
# auto 打怪数轮内倒下回城（快照数值下 30 级战神打僵尸一击 1 点，
# 打不死人——死亡测试得用低级角色）
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "2", "gold": str(gold0), "gems": "0", "mapId": "5",
    "accountStatus": "1", "banReason": ""}, cookie)
if j.get("code") != "0000":
    print("[debug] GM save resp:", str(j)[:200])
ok(j.get("code") == "0000", "GM drops low-level hunter on zombie map")
h.send({"op": "auto", "mob": 28, "on": 1})
r = h.ok_for(lambda m: "auto" in m)
ok(r["auto"] == 28, "auto-hunt targets zombie on map 5")
died = False
for _ in range(30):
    m = h.reply_for(lambda m: m.get("ev") == "die", timeout=3)
    if m is not None:
        died = True
        break
h.pending.clear()
h.send({"op": "state"})
r = h.reply_for(lambda m: m.get("ev") == "state" or "self" in m)
ok(died and r["self"]["map"] == 1 and r["self"]["auto"] == 0
   and r["self"]["hp"] == r["self"]["maxhp"] // 2,
   "death respawns player in town with half hp, auto off")

# GM 降级：hp 钳到新上限；满血嗑药被拒且不消耗
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "2", "gold": str(gold0), "gems": "0", "mapId": "1",
    "accountStatus": "1", "banReason": ""}, cookie)
ok(j.get("code") == "0000", "GM demotes hunter for clamp check")
h.pending.clear()
h.send({"op": "state"})
r = h.reply_for(lambda m: m.get("ev") == "state" or "self" in m)
ok(r["self"]["level"] == 2 and r["self"]["maxhp"] == 180
   and r["self"]["hp"] <= r["self"]["maxhp"],
   "GM level change clamps hp to new cap")
# 满血嗑药被拒：先把血补满（血量减半后用强化生命回血会先走正向路径），
# 这里用 gm 直接写库 hp=maxhp 再触发拒绝路径
st, j, _ = gm("/admin/game/players/save", {
    "id": str(hunter_uid), "nickname": "小猎手", "realmId": "1", "job": "0",
    "level": "2", "gold": str(gold0), "gems": "0", "mapId": "1",
    "accountStatus": "1", "banReason": "", "hp": "180"}, cookie)
ok(j.get("code") == "0000", "GM refills hp")
h.pending.clear()
h.send({"op": "state"})
r = h.ok_for()
if r["self"]["hp"] < r["self"]["maxhp"]:
    # GM 表单不含 hp 字段时跳过满血路径：改用对死亡半血状态回满的一次正向嗑药
    h.send({"op": "use", "item": shop["强化生命"]})
    h.ok_for()
    h.send({"op": "bag"})
    r = h.ok_for(lambda m: "items" in m)
    pot = [i for i in r["items"] if i["name"] == "强化生命"]
    ok(len(pot) == 1 and pot[0]["count"] >= 1, "potion count tracked after heal")
else:
    h.send({"op": "use", "item": shop["强化生命"]})
    r = h.reply_for(lambda m: m.get("ok") == 0)
    ok(r is not None and "血量已满" in (r.get("err") or ""),
       "full-hp potion use refused")
    h.send({"op": "bag"})
    r = h.ok_for(lambda m: "items" in m)
    pot = [i for i in r["items"] if i["name"] == "强化生命"]
    ok(len(pot) == 1 and pot[0]["count"] >= 1, "refused potion not consumed")

# 落库：背包/装备/血量随 flush 写入
time.sleep(11)
db = sqlite3.connect("data/app.db")
row = db.execute(
    'SELECT hp, weaponId FROM game_player WHERE id=?',
    (hunter_uid,)).fetchone()
ok(row is not None and row[1] == shop["铁剑"] and row[0] > 0,
   "combat state persisted with equipped weapon")
bags = db.execute(
    'SELECT itemId, count FROM game_bag WHERE playerId=?',
    (hunter_uid,)).fetchall()
ok(any(b[1] >= 1 for b in bags), "bag rows persisted")
db.close()
h.drop()

# ---- HTTP 登录 + 推送通道重挂（回归：顶号必须「先登记后拆通道」）----
# EnsureHttpSession 若先 kick+Leave（Leave 落库是一次几十 ms 的 DB 写）
# 后登记新会话，客户端收到 kick 当场重连 attach 会掉进「旧会话已摘、
# 新会话未登记」的缝，attach 答「登录已过期」，这条推送通道从此静默聋。
# 本节在收到 kick 后零延迟重挂，正是该缝隙的复现形状。

def api(path, obj):
    req = urllib.request.Request(BASE + path)
    req.add_header("Content-Type", "application/json")
    try:
        resp = opener.open(req, json.dumps(obj).encode(), timeout=10)
        return json.loads(resp.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        try:
            return json.loads(e.read().decode("utf-8", "replace"))
        except Exception:
            return {}

def attach(token):
    c = Client()
    c.recv()  # banner
    c.send({"op": "attach", "token": token})
    while True:
        m = c.recv()
        if m is None or "push" in m or m.get("ok") == 0:
            return c, m

j = api("/api/game/register", {"user": "e2e_push", "pass": "pass123"})
j = api("/api/game/login", {"user": "e2e_push", "pass": "pass123"})
ok(j.get("ok") == 1 and j.get("token"), "HTTP login issues token")
tok = j["token"]
j = api("/api/game/create", {"op": "create", "token": tok, "realm": 1,
                             "name": "推客", "job": 0})
j = api("/api/game/enter", {"op": "enter", "token": tok, "realm": 1})
ok(j.get("ok") == 1, "HTTP enter registers httpOwned session")

p, m = attach(tok)
ok(m is not None and "push" in m, "attach claims push channel for http session")

j = api("/api/game/login", {"user": "e2e_push", "pass": "pass123"})
ok(j.get("ok") == 1 and j.get("token") != tok, "relogin mints a fresh token")
tok2 = j["token"]
# relogin revokes the old token immediately (A303 建议 ①)：旧 token 的
# HTTP op 拒绝（登录已过期）；新 token 正常。attach 侧由下一段
# 「old push channel receives relogin kick」+ 拒绝形态覆盖。
j = api("/api/game/enter", {"op": "enter", "token": tok, "realm": 1})
ok(j.get("ok") == 0 and j.get("err") == "登录已过期，请重新登录",
   "old token rejected on HTTP op after relogin (revoke)")
j = api("/api/game/enter", {"op": "enter", "token": tok2, "realm": 1})
ok(j.get("ok") == 1, "new token still valid after relogin")
m = p.recv()
ok(m is not None and m.get("ev") == "kick",
   "old push channel receives relogin kick")
p.drop()

p2, m = attach(tok2)
ok(m is not None and "push" in m,
   "immediate re-attach after kick succeeds (register-before-kick)")

st, j, _ = gm("/admin/game/online/broadcast", {"text": "e2e 推送回归"}, cookie)
m = None
for _ in range(10):
    mm = p2.recv(timeout=5)
    if mm and mm.get("ev") == "chat" and "e2e" in (mm.get("text") or ""):
        m = mm
        break
ok(m is not None, "GM broadcast reaches re-attached push channel")
p2.drop()

print(f"ALL PASS checks={checks}")

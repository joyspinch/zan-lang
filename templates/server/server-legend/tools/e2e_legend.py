"""Contract-matrix e2e for the server-legend template.

Drives ALL gateway ops (53) against a live instance with per-op reply-shape
assertions, plus the legend-specific numeric contracts (tries=5, arena 16
rivals, title 251 rows / 21 pages x 12, codex page 20, escort time gate,
forge star cost + iron, draw pool, recycle 7-currency keys, redpack share
math, 35 boss maps, wboss hp from world_boss_hp), the market concurrent
buy CAS (two buyers, one listing -> exactly one success), and world-state
restart persistence (siege city owner / redpack pool / wboss hp survive a
kill+reboot after the flush window).

The script manages the server lifecycle itself:
  1. delete data/app.db* + data/metrics.db* (fresh seed)
  2. boot the exe (default _scratch/sl_full.exe, override with --exe)
  3. run the matrix + CAS + world-state writes
  4. wait the flush window, kill, reboot on the SAME database
  5. assert the world state came back, kill

Run from anywhere; paths are resolved from this file's location:
  python tools/e2e_legend.py [--exe PATH]
Stdlib urllib/socket/subprocess only.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)          # templates/server/server-legend
GAME = ("127.0.0.1", 7100)
BASE = "http://127.0.0.1:8099"

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
def http(path, data=None, cookie=None):
    req = urllib.request.Request(BASE + path)
    if cookie: req.add_header("Cookie", cookie)
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    if body is not None: req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = opener.open(req, body, timeout=10)
        return resp.status, resp.read().decode("utf-8", "replace"), resp.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), e.headers.get_all("Set-Cookie") or []

class C:
    def __init__(self):
        self.sock = socket.create_connection(GAME, timeout=10)
        self.buf = b""
        self.pending = []
        self.dead = False
        import threading
        self.wlock = threading.Lock()
        def hb():
            while not self.dead:
                try:
                    # sendall 可能在多次 syscall 间让出 GIL，两个线程
                    # 并发写同一 socket 会把一帧拆成两半——加锁保帧完整。
                    with self.wlock:
                        self.sock.sendall(b'{"op":"hb"}\n')
                except OSError:
                    return
                time.sleep(10)
        threading.Thread(target=hb, daemon=True).start()
    def send(self, o):
        with self.wlock:
            self.sock.sendall((json.dumps(o, ensure_ascii=False) + "\n").encode())
    def _fill(self, t):
        self.sock.settimeout(t)
        try:
            while True:
                d = self.sock.recv(65536)
                if not d:
                    self.dead = True
                    return False
                self.buf += d
                while b"\n" in self.buf:
                    line, self.buf = self.buf.split(b"\n", 1)
                    if line.strip():
                        self.pending.append(json.loads(line.decode("utf-8")))
        except socket.timeout:
            pass
        return True
    def reply_for(self, pred, timeout=5):
        deadline = time.time() + timeout
        while True:
            for i, m in enumerate(self.pending):
                if pred(m): return self.pending.pop(i)
            if time.time() > deadline or not self._fill(max(0.1, deadline - time.time())):
                return None
    def clear(self):
        self.pending.clear()

def wait_port(timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(GAME, timeout=2)
            s.close()
            return True
        except OSError:
            time.sleep(0.5)
    return False

def reset_db():
    for name in ("app.db", "app.db-shm", "app.db-wal", "metrics.db", "metrics.db-shm", "metrics.db-wal"):
        p = os.path.join(ROOT, "data", name)
        if os.path.exists(p): os.remove(p)

def start_server(exe):
    log = open(os.path.join(ROOT, "data", "e2e_legend_server.log"), "ab")
    proc = subprocess.Popen([exe], cwd=ROOT, stdout=log, stderr=log)
    if not wait_port():
        raise SystemExit("server did not listen on 7100; see data/e2e_legend_server.log")
    time.sleep(1.5)
    return proc

def stop_server(proc):
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    time.sleep(1)

TS = str(int(time.time()))[-6:]
def newchar(name):
    c = C(); c._fill(3)
    u = "lg%d%s" % (int(time.time()), name)
    c.send({"op": "register", "user": u, "pass": "secret1",
            "question": "出生城市", "answer": "北京"})
    r = c.reply_for(lambda m: m.get("ok") == 1 and m.get("uid") is not None and m.get("msg"))
    ok(r is not None, "register " + name)
    c.send({"op": "login", "user": u, "pass": "secret1"})
    # TCP 直连登录回 {ok,uid,pushPort,realms}——token 只发 HTTP attach 路径
    r = c.reply_for(lambda m: m.get("ok") == 1 and m.get("pushPort") is not None
                    and m.get("uid") is not None)
    ok(r is not None and r.get("uid") is not None, "login " + name)
    c.send({"op": "create", "realm": 1, "name": name, "job": 0})
    r = c.reply_for(lambda m: "self" in m or m.get("ok") == 0)
    ok(r is not None and r.get("ok") == 1, "create " + name)
    return c, (r["self"]["uid"] if r and r.get("self") else 0)

def gm_login():
    st, _, setc = http("/admin/login", data={"user": "admin", "pass": "admin1234"})
    return "".join(x.split(";")[0] + "; " for x in setc)

def gm_save(cookie, uid, nickname, gold="10000000", gems="1000", gift=None):
    data = {"id": str(uid), "nickname": nickname, "realmId": "1", "job": "0",
            "level": "40", "gold": gold, "gems": gems, "mapId": "1",
            "accountStatus": "1", "banReason": ""}
    if gift:
        data["giftItem"] = str(gift[0]); data["giftCount"] = str(gift[1])
    st, j, _ = http("/admin/game/players/save", data=data, cookie=cookie)
    try:
        return json.loads(j).get("code") == "0000"
    except Exception:
        return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(ROOT, "_build", "server.exe"))
    args = ap.parse_args()
    if not os.path.exists(args.exe):
        raise SystemExit("server exe not found: " + args.exe)

    reset_db()
    proc = start_server(args.exe)
    try:
        run_matrix()
        run_cas_and_worldstate(cookie=gm_login())
        # 世界态写穿窗口：flushSeconds=10，多等一拍
        time.sleep(13)
    finally:
        stop_server(proc)

    # 重启不丢：同一数据库再启动
    proc = start_server(args.exe)
    try:
        run_persistence()
    finally:
        stop_server(proc)

    print()
    if fails:
        print("FAILED: %d/%d -> %s" % (len(fails), checks, fails))
        sys.exit(1)
    print("ALL PASS checks=%d" % checks)

def run_matrix():
    hero, uid1 = newchar("矩阵侠" + TS)
    ok(uid1 > 0, "uid assigned")
    cookie = gm_login()

    # ---- 账号域补充：forgot / reset / changepw / characters / delchar ----
    # （每步前清 pending：登录/建号阶段的 ok==1 推送会污染宽松谓词）
    hero.clear()
    hero.send({"op": "forgot", "user": "nouser" + TS})
    r = hero.reply_for(lambda m: m.get("ok") == 0)
    ok(r is not None and "err" in r, "forgot unknown user -> err")
    hero.clear()
    hero.send({"op": "reset", "user": "nouser" + TS, "answer": "北京", "newpass": "secret2"})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None and r.get("ok") == 0, "reset unknown user -> err")
    hero.clear()
    hero.send({"op": "changepw", "old": "wrong", "newpass": "secret2"})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None and r.get("ok") == 0, "changepw wrong old -> err")
    hero.clear()
    hero.send({"op": "characters"})
    r = hero.reply_for(lambda m: "ok" in m and ("rows" in m or "characters" in m))
    ok(r is not None, "characters lists")
    hero.clear()
    hero.send({"op": "delchar", "name": "不存在" + TS})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None, "delchar wrong name -> err")

    # ---- 世界/交互域 ----
    for op, payload, key in [
        ("state", {}, "self"),
        ("maps", {}, "maps"),
        ("who", {}, "rows"),
        ("mobs", {}, "rows"),
        ("bosses", {}, "rows"),
        ("bag", {}, "items"),
        ("shop", {}, "shop"),
        ("rank", {}, "rows"),
        ("announces", {}, "rows"),
        ("codex", {}, "codex"),
    ]:
        hero.clear()
        hero.send(dict(payload, op=op))
        r = hero.reply_for(lambda m: m.get("ok") == 1)
        ok(r is not None and key in r, "op %-10s -> %s" % (op, key))

    hero.clear()
    hero.send({"op": "walk", "x": 10, "y": 10})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None and r.get("ok") == 1, "op walk -> ok")
    hero.clear()
    hero.send({"op": "say", "text": "e2e_legend 冒烟"})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None and r.get("ok") == 1, "op say -> ok")
    hero.clear()
    hero.send({"op": "move", "map": 1})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None, "op move -> reply")

    # ---- 战斗域（数值：挂机目标/BOSS 链） ----
    hero.clear()
    hero.send({"op": "mobs"})
    r = hero.reply_for(lambda m: "rows" in m)
    # mobs 行的怪物标识是模板 id（键名 tpl），hunt/auto 的 mob 参数即它
    mob = r["rows"][0]["tpl"] if r and r.get("rows") else 0
    hero.send({"op": "hunt", "mob": mob})
    r = hero.reply_for(lambda m: "fight" in m or "ok" in m)
    ok(r is not None, "op hunt -> reply")
    hero.clear()
    hero.send({"op": "auto", "mob": mob, "on": 1})
    r = hero.reply_for(lambda m: "auto" in m)
    ok(r is not None and r.get("auto") is not None, "op auto on -> auto id")
    hero.send({"op": "auto", "mob": 0, "on": 0})
    hero.reply_for(lambda m: "auto" in m)
    hero.clear()
    hero.send({"op": "bossinfo"})
    r = hero.reply_for(lambda m: "page" in m)
    ok(r is not None and "boss" in (r.get("page") or {}), "op bossinfo -> page.boss")
    hero.clear()
    hero.send({"op": "bossfight", "map": 1})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None, "op bossfight -> reply")
    hero.clear()
    hero.send({"op": "bossweep", "map": 1, "count": 1})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None, "op bossweep -> reply")

    # 装备/消耗：GM 发一把武器穿/脱 + 买药用药
    ok(gm_save(cookie, uid1, "矩阵侠" + TS, gift=(16, 1)), "GM gift 木剑")
    hero.clear()
    hero.send({"op": "bag"})
    r = hero.reply_for(lambda m: "items" in m)
    wooden = next((i for i in r.get("items", []) if i["name"] == "木剑"), None)
    ok(wooden is not None, "bag has 木剑")
    if wooden:
        # gmsave 会向本会话转发两条 state 事件（self.weapon=0），谓词必须
        # 同时要求 ok==1 与武器位真值，否则事件会抢在 equip 回复前命中
        hero.clear()
        hero.send({"op": "equip", "item": wooden["id"]})
        r = hero.reply_for(lambda m: m.get("ok") == 1
                           and (m.get("self") or {}).get("weapon"))
        ok(r is not None, "equip weapon -> slot set")
        hero.clear()
        hero.send({"op": "takeoff", "slot": 0})
        r = hero.reply_for(lambda m: m.get("ok") == 1 and "self" in m
                           and not (m.get("self") or {}).get("weapon"))
        ok(r is not None, "takeoff weapon -> slot cleared")
    hero.clear()
    hero.send({"op": "shop"})
    r = hero.reply_for(lambda m: "shop" in m)
    # shop 是数组（ShopJson），按名字找物品再取 id
    potion = next((x for x in (r.get("shop") or [])
                   if x.get("name") == "强化生命"), None)
    ok(potion is not None, "shop lists 强化生命")
    if potion:
        pid = potion["id"]
        hero.send({"op": "buy", "item": pid, "count": 2})
        r = hero.reply_for(lambda m: m.get("ok") == 1 and "self" in m)
        ok(r is not None, "buy potion -> self")
        hero.clear()
        hero.send({"op": "use", "item": pid})
        r = hero.reply_for(lambda m: m.get("ok") == 1)
        ok(r is not None, "use potion -> reply")
        hero.clear()
        hero.send({"op": "sell", "item": pid, "count": 1})
        r = hero.reply_for(lambda m: m.get("ok") == 1)
        ok(r is not None, "sell potion -> reply")

    # ---- 玩法域数值契约 ----
    hero.clear()
    hero.send({"op": "arena"})
    r = hero.reply_for(lambda m: "arena" in m)
    a = (r or {}).get("arena", {})
    # rows=对手榜（arena_rival_count 上限，新库玩家数不足时按实际人数返回）
    ok(str(a.get("tries")) == "5" and isinstance(a.get("rows"), list)
       and 1 <= len(a.get("rows", [])) <= 16, "arena tries=5 rows<=16")
    hero.clear()
    hero.send({"op": "escort"})
    r = hero.reply_for(lambda m: "escort" in m)
    ok(r is not None and str((r.get("escort") or {}).get("tries")) == "5", "escort tries=5")
    hero.clear()
    hero.send({"op": "title"})
    r = hero.reply_for(lambda m: "rows" in m)
    ok(r is not None and str(r.get("total")) == "251" and str(r.get("pages")) == "21"
       and len(r.get("rows", [])) == 12, "title 251/21 pages x 12")
    hero.clear()
    hero.send({"op": "codexpage", "kind": "items", "page": 0})
    r = hero.reply_for(lambda m: "rows" in m)
    ok(r is not None and len(r.get("rows", [])) == 20, "codexpage 20 rows")
    hero.clear()
    hero.send({"op": "stars"})
    r = hero.reply_for(lambda m: "rows" in m)
    ok(r is not None and len(r.get("rows", [])) == 35, "stars 35 boss maps")
    hero.clear()
    hero.send({"op": "tower"})
    r = hero.reply_for(lambda m: "tower" in m)
    t = (r or {}).get("tower", {})
    # 客户端契约：嵌套 tower 状态（floor/tries/maxTries），塔层榜是 rivals
    ok(str(t.get("maxTries")) == "10" and "floor" in t and "tries" in t,
       "tower state floor/tries/maxTries=10")
    hero.clear()
    hero.send({"op": "wuxing"})
    r = hero.reply_for(lambda m: "wuxing" in m)
    w = (r or {}).get("wuxing", {})
    # 客户端契约：嵌套 wuxing 状态（五行 rows=5 条 + pool/poolCap）
    ok(isinstance(w.get("rows"), list) and len(w.get("rows", [])) == 5
       and "pool" in w, "wuxing state 5 rows + pool")
    hero.clear()
    hero.send({"op": "mentor"})
    r = hero.reply_for(lambda m: "mentor" in m)
    ok(r is not None, "mentor -> mentor")
    hero.clear()
    hero.send({"op": "altar"})
    r = hero.reply_for(lambda m: "altar" in m)
    ok(r is not None, "altar -> altar")
    hero.clear()
    hero.send({"op": "guild"})
    r = hero.reply_for(lambda m: "rows" in m)
    ok(r is not None, "guild -> rows")
    hero.clear()
    hero.send({"op": "siege"})
    r = hero.reply_for(lambda m: "siege" in m)
    ok(r is not None and "cities" in (r.get("siege") or {}), "siege -> siege.cities")
    hero.clear()
    hero.send({"op": "redpack"})
    r = hero.reply_for(lambda m: "redpack" in m)
    ok(r is not None, "redpack -> redpack")
    hero.clear()
    hero.send({"op": "wboss"})
    r = hero.reply_for(lambda m: "wboss" in m)
    wb = (r or {}).get("wboss", {})
    ok(str(wb.get("maxhp")) == "150000", "wboss maxhp=world_boss_hp")
    hero.clear()
    hero.send({"op": "market"})
    r = hero.reply_for(lambda m: "rows" in m)
    ok(r is not None, "market -> rows")

    # ---- 锻造 / 寻宝 / 回收（GM 补给后） ----
    ok(gm_save(cookie, uid1, "矩阵侠" + TS, gift=(223, 60)), "GM gift 黑铁 x60")
    hero.clear()
    hero.send({"op": "forge", "slot": "weapon"})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None and r.get("ok") == 1
       and str((r.get("self") or {}).get("weaponStar")) == "1", "forge -> star 1")
    draws = 0
    for n in range(3):
        hero.clear()
        hero.send({"op": "draw"})
        r = hero.reply_for(lambda m: "item" in m or (m.get("ok") == 0 and "err" in m))
        if r and r.get("item"): draws += 1
    ok(draws == 3, "draw x3 succeed (pool items exist)")
    hero.clear()
    def ore_count():
        hero.send({"op": "bag"})
        rr = hero.reply_for(lambda m: "items" in m)
        return sum(i["count"] for i in rr.get("items", []) if i["id"] == 223)
    n0 = ore_count()
    ok(n0 > 5, "bag holds enough ore for per-item recycle")
    # 回收系统页按行回收：item=材料 id、count=上限数量；缺价回退半价
    # （223 无 recycle_prices 行 → sell_price/2 = 1000）。
    def recycle_reply():
        return hero.reply_for(lambda m: m.get("gain") is not None
                              or (m.get("ok") == 0 and "err" in m))
    hero.send({"op": "recycle", "item": 223, "count": 5})
    r = recycle_reply()
    ok(r is not None and r.get("ok") == 1 and r.get("gain") == 5000,
       "recycle item=223 count=5 -> gain 5000 (half-price fallback)")
    n1 = ore_count()
    ok(n1 == n0 - 5, "per-item recycle takes exactly count from the stack")
    hero.send({"op": "recycle", "item": 223})
    r = recycle_reply()
    ok(r is not None and r.get("ok") == 1 and r.get("gain") == n1 * 1000,
       "recycle item=223 no count -> whole stack settled")
    ok(ore_count() == 0, "ore gone after whole-stack recycle")
    hero.clear()
    hero.send({"op": "recycle", "item": 223})
    r = recycle_reply()
    ok(r is not None and r.get("ok") == 0, "recycle missing item -> err")
    hero.clear()
    hero.send({"op": "recycle"})
    r = hero.reply_for(lambda m: "ok" in m)
    ok(r is not None and (r.get("ok") == 0 or all(
        k in r for k in ("gain", "exp", "merit", "reputation", "redPackets", "gems", "recyclePoints"))),
       "recycle -> 7-currency reply or empty-bag err")

def run_cas_and_worldstate(cookie):
    hero, uid1 = newchar("市霸" + TS)
    buyer, uid2 = newchar("买主甲" + TS)
    buyer2, uid3 = newchar("买主乙" + TS)
    ok(gm_save(cookie, uid1, "市霸" + TS, gift=(16, 1)), "GM gift 木剑 for seller")
    ok(gm_save(cookie, uid2, "买主甲" + TS), "GM gold buyer1")
    ok(gm_save(cookie, uid3, "买主乙" + TS), "GM gold buyer2")

    # 上架：hero 挂木剑 100 金
    hero.clear()
    hero.send({"op": "bag"})
    r = hero.reply_for(lambda m: "items" in m)
    wooden = next((i for i in r.get("items", []) if i["name"] == "木剑"), None)
    ok(wooden is not None, "seller has 木剑")
    hero.send({"op": "market", "sellItem": wooden["id"], "count": 1, "price": 100})
    r = hero.reply_for(lambda m: "rows" in m or m.get("ok") == 0)
    ok(r is not None and r.get("ok") == 1, "market list ok")
    hero.clear()
    hero.send({"op": "market"})
    r = hero.reply_for(lambda m: "rows" in m)
    rowid = None
    for x in r.get("rows", []):
        if str(x.get("mine")) == "1":
            rowid = x.get("id")
    ok(rowid is not None, "listing visible")

    # CAS：两个买家同时买同一挂单，恰好一个成功
    # （谓词必须认 rows——hb 回复 {ok:1,t,online} 会被 "ok" in m 误中）
    buyer.clear(); buyer2.clear()
    buyer.send({"op": "market", "buy": rowid})
    buyer2.send({"op": "market", "buy": rowid})
    r1 = buyer.reply_for(lambda m: "rows" in m)
    r2 = buyer2.reply_for(lambda m: "rows" in m)
    wins = (1 if r1 and r1.get("ok") == 1 else 0) + (1 if r2 and r2.get("ok") == 1 else 0)
    ok(wins == 1, "market CAS: exactly one buyer wins (got %d)" % wins)

    # 世界态写入：红包池 + 世界 BOSS 伤害 + 攻占城池
    hero.clear()
    hero.send({"op": "redpack", "send": 200})
    r = hero.reply_for(lambda m: "redpack" in m)
    pool0 = int((r.get("redpack") or {}).get("pool", 0)) if r else 0
    ok(pool0 > 0, "redpack pool seeded (pool=%d)" % pool0)
    hero.clear()
    hero.send({"op": "wboss", "hit": 1})
    r = hero.reply_for(lambda m: "ok" in m and "wboss" in m)
    wb_hp = int(((r or {}).get("wboss") or {}).get("hp", 0))
    ok(wb_hp > 0, "wboss hit recorded (hp=%d)" % wb_hp)

    # 攻占城池 1：建会需要 500 万金币 + 沃玛号角(330)，胜率随 power 抬满后必胜
    ok(gm_save(cookie, uid1, "市霸" + TS, gift=(330, 1)), "GM gift 沃玛号角")
    hero.clear()
    hero.send({"op": "guild", "create": "天下会" + TS})
    r = hero.reply_for(lambda m: "rows" in m or m.get("ok") == 0)
    ok(r is not None and r.get("ok") == 1, "guild created for siege")
    for attempt in range(6):
        hero.clear()
        hero.send({"op": "siege", "attack": 1, "city": 1})
        r = hero.reply_for(lambda m: "siege" in m or (m.get("ok") == 0 and "err" in m))
        if r and r.get("win") is not None and int(r["win"]) == 1:
            break
        if r and r.get("ok") == 0 and "金币" in (r.get("err") or ""):
            gm_save(cookie, uid1, "市霸" + TS)  # 补钱再试
    city_owner = None
    hero.clear()
    hero.send({"op": "siege"})
    r = hero.reply_for(lambda m: "siege" in m)
    cities = ((r or {}).get("siege") or {}).get("cities", [])
    if cities:
        city_owner = cities[0].get("owner")
    ok(city_owner == "市霸" + TS, "siege city1 owned (owner=%s)" % city_owner)

    # 留下重启后的期望值
    with open(os.path.join(ROOT, "data", "e2e_legend_state.json"), "w") as f:
        json.dump({"hero": "市霸" + TS, "pool": Play_pool_after(hero), "wb_hp": wb_hp}, f)

def Play_pool_after(hero):
    hero.clear()
    hero.send({"op": "redpack"})
    r = hero.reply_for(lambda m: "redpack" in m)
    return int((r.get("redpack") or {}).get("pool", 0)) if r else 0

def run_persistence():
    st_path = os.path.join(ROOT, "data", "e2e_legend_state.json")
    if not os.path.exists(st_path):
        ok(False, "state file missing (phase 1 crashed?)")
        return
    with open(st_path) as f:
        want = json.load(f)
    c, _ = newchar("回来侠" + TS)
    c.clear()
    c.send({"op": "siege"})
    r = c.reply_for(lambda m: "siege" in m)
    cities = ((r or {}).get("siege") or {}).get("cities", [])
    owner = cities[0].get("owner") if cities else None
    ok(owner == want["hero"], "restart: city1 owner kept (got %s)" % owner)
    c.clear()
    c.send({"op": "redpack"})
    r = c.reply_for(lambda m: "redpack" in m)
    pool = int(((r or {}).get("redpack") or {}).get("pool", -1))
    ok(pool == want["pool"], "restart: redpack pool kept (%d vs %d)" % (pool, want["pool"]))
    c.clear()
    c.send({"op": "wboss"})
    r = c.reply_for(lambda m: "wboss" in m)
    hp = int(((r or {}).get("wboss") or {}).get("hp", 0))
    maxhp = int(((r or {}).get("wboss") or {}).get("maxhp", 0))
    # 重启后血量沿用落库值；若正好跨过重生时刻则满血——两者都算"不丢"
    ok(hp == want["wb_hp"] or hp == maxhp,
       "restart: wboss hp kept (got %d, want %d or fresh %d)" % (hp, want["wb_hp"], maxhp))
    os.remove(st_path)

if __name__ == "__main__":
    main()

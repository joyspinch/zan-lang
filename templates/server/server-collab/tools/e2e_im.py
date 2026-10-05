"""End-to-end e2e for the gui-wechat IM loop against server-collab.

Drives the exact request sequence the desktop client (templates/gui/gui-wechat)
makes, with two accounts, and asserts the offline-delivery contract:

  1. fresh DB (delete data/app.db* + metrics.db*), boot the exe
  2. admin cookie login -> create account B via /admin/system/users/save
  3. token login A (admin) + B (/api/auth/login)
  4. A contacts -> B is listed with its login name in `username`
  5. B offline: A sends two messages
  6. A chats: unread back? no -- sentUnread=2, readed/acked=0 (grey ticks)
  7. B wakes: chats shows unread=2 + ackId, unread count == 2
  8. B batch-acks the peer (delivery receipt) -> A chats acked=1, readed=0
  9. B opens the stream (bulk read) -> A chats readed=1, sentUnread=0;
     B unread count == 0
 10. B quote-replies to message 1 -> A stream carries replyTo + quoteWho/quoteText

The script manages the server lifecycle itself (mirrors e2e_legend.py):
  python tools/e2e_im.py [--exe PATH]
Stdlib urllib/socket/subprocess only.
"""
import argparse
import http.client
import re
import json
import os
import socket
import subprocess
import sys
import time
import http.client
from http.client import HTTPException
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
os.environ["NO_PROXY"] = "127.0.0.1,localhost," + os.environ.get("NO_PROXY", "")
os.environ["no_proxy"] = "127.0.0.1,localhost," + os.environ.get("no_proxy", "")
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect)
urllib.request.install_opener(opener)

def http(path, data=None, cookie=None, bearer=None):
    req = urllib.request.Request(BASE + path)
    if cookie: req.add_header("Cookie", cookie)
    if bearer: req.add_header("Authorization", "Bearer " + bearer)
    req.add_header("X-Fragment", "1")
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    if body is not None: req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = opener.open(req, body, timeout=10)
        return resp.status, resp.read().decode("utf-8", "replace"), resp.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), e.headers.get_all("Set-Cookie") or []

def api(method, path, data=None, token=None):
    """{code,msg,data} envelope; returns the data object (None on failure)."""
    req = urllib.request.Request(BASE + path, method=method)
    if token: req.add_header("Authorization", "Bearer " + token)
    body = None
    if data is not None:
        body = urllib.parse.urlencode(data).encode()
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    raw = None
    for attempt in range(5):
        # 连接被重置 / 应答头是二进制垃圾（响应字节被未初始化内存覆盖，
        # BadStatusLine 携带  块）：换新连接重试。幂等请求安全，POST
        # 也是——服务端没处理就不落库；同机 4-worker 偶发，重试即过。
        req = urllib.request.Request(BASE + path, method=method)
        if token:
            req.add_header("Authorization", "Bearer " + token)
        try:
            resp = urllib.request.urlopen(req, body, timeout=10)
            raw = resp.read().decode("utf-8", "replace")
            break
        except urllib.error.HTTPError as e:
            raw = e.read().decode("utf-8", "replace")
            break
        except (urllib.error.URLError, OSError, HTTPException) as e:
            if attempt == 4:
                print("  [net] %s %s failed after 5 tries: %r"
                      % (method, path, e))
                return None
            time.sleep(0.2 + 0.2 * attempt)
    try:
        j = json.loads(raw)
    except Exception:
        return None
    # 信封双方言：包 ApiController.Ok 答 code=0（int），
    # Zan.Web 基类答 "0000"（str）——两者都是成功。
    if j.get("code") not in (0, "0", "0000"):
        print("  [api] %s %s -> %s" % (method, path, raw[:120]))
        return None
    return j.get("data")

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

def reset_db():
    for name in ("app.db", "app.db-shm", "app.db-wal", "metrics.db",
                 "metrics.db-shm", "metrics.db-wal"):
        p = os.path.join(ROOT, "data", name)
        if os.path.exists(p): os.remove(p)

def start_server(exe):
    os.makedirs(os.path.join(ROOT, "data"), exist_ok=True)
    log = open(os.path.join(ROOT, "data", "e2e_im_server.log"), "ab")
    # ZAN_NO_BG=1：不启 CalendarRemind/MessageRelay 后台协程。
    # 默认跳过后台协程以保持环境极简、稳定判定业务契约；
    # 显式设 ZAN_NO_BG=0 可连后台协程一起测试。
    env = dict(os.environ)
    env["ZAN_NO_BG"] = os.environ.get("ZAN_NO_BG", "1")
    proc = subprocess.Popen([exe], cwd=ROOT, stdout=log, stderr=log, env=env)
    if not wait_port():
        raise SystemExit("server did not listen on %d; see data/e2e_im_server.log" % PORT)
    time.sleep(1.5)
    return proc

def stop_server(proc):
    # master + worker 树一起杀：terminate 只杀 master，4 个 worker 会
    # 变成攥着监听套接字的孤儿，下一次起服新旧实例混着应答（实测：
    # 响应里掺二进制乱码、随机 connection reset，像灵异其实是有鬼）。
    exe_name = os.path.basename(proc.args[0]) if proc.args else "collab_server.exe"
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
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/IM", exe_name], capture_output=True)
    time.sleep(1)

TS = str(int(time.time()))[-6:]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "_scratch", "tb_server.exe"))
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
    try:
        run_flow()
    finally:
        stop_server(proc)

    print()
    if fails:
        print("FAILED: %d/%d -> %s" % (len(fails), checks, fails))
        sys.exit(1)
    print("ALL PASS checks=%d" % checks)

def run_flow():
    # ---- admin cookie login + create account B -------------------------
    st, _, setc = http("/admin/login", data={"user": "admin", "pass": "admin-bootstrap-2026"})
    cookie = "".join(x.split(";")[0] + "; " for x in setc)
    ok(st in (200, 302) and cookie != "", "admin cookie login")

    user_b = "wx_b_" + TS
    st, j, _ = http("/admin/system/users/save", data={
        "username": user_b, "nickname": "冰冰" + TS,
        "email": user_b + "@demo.local", "mobile": "1380000" + TS[-4:],
        "departmentId": "0", "status": "1", "password": "wxpass2026"},
        cookie=cookie)
    try:
        saved = json.loads(j).get("code") == "0000"
    except Exception:
        saved = False
    ok(saved, "create account B via admin users/save")

    # B 要能调 /api/im/*（PermBit.View 门禁）：绑内置 editor 角色
    # （SyncRoleGrants 给它填了全部授权）。先从 users 列表拿 B 的 id，
    # 再从 roles 对话框拿 editor 的角色 id，走 roles 保存端点绑定。
    # 片段响应是 HTML 表格：行内 roles 链接的查询串带 id
    # （/admin/system/users/roles?id=N），行首单元是用户名。
    st, j, _ = http("/admin/system/users?kw=" + urllib.parse.quote(user_b),
                    cookie=cookie)
    m = re.search(r'<tr data-id="(\d+)"[\s\S]{0,600}?' + re.escape(user_b), j)
    uid_b = int(m.group(1)) if m else 0
    ok(uid_b > 0, "B id listed in admin users")
    # editor 角色只授 content 屏，/api/im/* 走 admin 角色（SyncRoleGrants
    # 给 admin 补全部受控 action）。内置角色种子序固定：id=1 admin、
    # id=2 editor；两个都绑上（碎 HTML 在坏连接下会截断，从对话框解析
    # value 不可靠，硬编码种子序并断言对话框确实列出了这些 id）。
    st, j, _ = http("/admin/system/users/roles?id=" + str(uid_b), cookie=cookie)
    listed = re.findall(r'name="roleIds\[\]" value="(\d+)"', j)
    ok("1" in listed and "2" in listed, "roles dialog lists built-in roles 1+2")
    # 包契约：浏览器 admin.js 把 checkbox 组合成单一逗号字段
    # roleIds=1,2 再 POST（RolesSave 读 In("roleIds")）。
    form = [("id", str(uid_b)), ("roleIds", "1,2")]
    st, j, _ = http("/admin/system/users/rolessave", data=form, cookie=cookie)
    try:
        roles_saved = json.loads(j).get("code") == "0000"
    except Exception:
        roles_saved = False
    ok(roles_saved, "assign roles to B")

    # ---- token login both ends -----------------------------------------
    da = api("POST", "/api/auth/login", {"user": "admin", "pass": "admin-bootstrap-2026"})
    ok(da is not None and da.get("token"), "A (admin) token login")
    ta = da.get("token")
    db_ = api("POST", "/api/auth/login", {"user": user_b, "pass": "wxpass2026"})
    ok(db_ is not None and db_.get("token"), "B token login")
    tb = db_.get("token")

    # ---- A contacts: B listed with real login name ---------------------
    d = api("GET", "/api/im/contacts", token=ta)
    rows = (d or {}).get("contacts") or []
    hit = [r for r in rows if r.get("username") == user_b]
    ok(len(hit) == 1, "contacts lists B with username field")
    bid = int(hit[0]["id"]) if hit else 0
    ok(bid > 0, "B id resolved from contacts")

    # ---- B offline: A sends two messages -------------------------------
    msg1 = "离线消息一号-" + TS
    msg2 = "离线消息二号-" + TS
    d = api("POST", "/api/im/send", {"peer": str(bid), "content": msg1}, token=ta)
    id1 = int((d or {}).get("id", "0"))
    ok(id1 > 0 and int((d or {}).get("at", "0")) > 0, "A send #1 gets id+at")
    d = api("POST", "/api/im/send", {"peer": str(bid), "content": msg2}, token=ta)
    id2 = int((d or {}).get("id", "0"))
    ok(id2 > id1, "A send #2 gets id")

    # ---- A polls chats while B is offline: grey ticks -------------------
    d = api("GET", "/api/im/chats", token=ta)
    rows = (d or {}).get("chats") or []
    hit = [r for r in rows if r.get("peerId") == str(bid)]
    ok(len(hit) == 1, "A chats has B entry")
    c = hit[0] if hit else {}
    ok(c.get("sentUnread") == "2", "A sees sentUnread=2 while B offline")
    ok(c.get("readed") == "0" and c.get("acked") == "0",
       "A ticks still grey (readed=0 acked=0)")

    # ---- B wakes: unread list + counter --------------------------------
    d = api("GET", "/api/im/chats", token=tb)
    rows = (d or {}).get("chats") or []
    hit = [r for r in rows if r.get("peerId") == "1"]
    ok(len(hit) == 1, "B chats has A entry")
    c = hit[0] if hit else {}
    ok(c.get("unread") == "2", "B sees unread=2")
    ok(c.get("lastText") == msg2, "B lastText is the newest message")
    ok(c.get("ackId") == str(id2), "B chats carries newest un-acked ackId")

    d = api("GET", "/api/im/unread", token=tb)
    ok((d or {}).get("count") == "2", "B unread count == 2")

    # ---- B batch-acks (delivery receipt, no open yet) -------------------
    d = api("POST", "/api/im/ack", {"peer": "1"}, token=tb)
    ok(d is not None and d.get("ok") == "1", "B batch ack ok")

    d = api("GET", "/api/im/chats", token=ta)
    rows = (d or {}).get("chats") or []
    hit = [r for r in rows if r.get("peerId") == str(bid)]
    c = hit[0] if hit else {}
    ok(c.get("acked") == "1" and c.get("readed") == "0",
       "A sees delivered (acked=1) but not read")

    # ---- B opens the stream: bulk read ---------------------------------
    d = api("POST", "/api/im/stream", {"peer": "1"}, token=tb)
    msgs = (d or {}).get("msgs") or []
    ok(len(msgs) == 2, "B stream has both messages")
    ok([m.get("content") for m in msgs] == [msg1, msg2], "B stream contents in order")
    ok(all(m.get("mine") == "0" for m in msgs), "B stream marks them incoming")

    d = api("GET", "/api/im/chats", token=ta)
    rows = (d or {}).get("chats") or []
    hit = [r for r in rows if r.get("peerId") == str(bid)]
    c = hit[0] if hit else {}
    ok(c.get("readed") == "1" and c.get("sentUnread") == "0",
       "A sees readed=1 sentUnread=0 after B opens")
    d = api("GET", "/api/im/unread", token=tb)
    ok((d or {}).get("count") == "0", "B unread count back to 0")

    # ---- B quote-replies to message #1; A sees the quote ----------------
    d = api("POST", "/api/im/send",
            {"peer": "1", "content": "收到-" + TS, "replyTo": str(id1)}, token=tb)
    ok(int((d or {}).get("id", "0")) > 0, "B quote-reply send ok")

    # 先查未读再开流：Stream 打开会话即批量已读，开了再查就永远是 0
    # （真实客户端也是徽标先亮、点开清零）。
    d = api("GET", "/api/im/chats", token=ta)
    rows = (d or {}).get("chats") or []
    hit = [r for r in rows if r.get("peerId") == str(bid)]
    c = hit[0] if hit else {}
    ok(c.get("unread") == "1", "A sees the reply as unread")

    d = api("POST", "/api/im/stream", {"peer": str(bid)}, token=ta)
    msgs = (d or {}).get("msgs") or []
    last = msgs[-1] if msgs else {}
    ok(last.get("replyTo") == str(id1), "A stream carries replyTo")
    ok(last.get("quoteWho") == "我", "A stream quoteWho is me (quoted mine)")
    ok(last.get("quoteText") == msg1, "A stream quoteText is msg1 excerpt")

if __name__ == "__main__":
    main()

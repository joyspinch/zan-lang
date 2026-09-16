"""End-to-end e2e for the server-collab collab-domain slice (A327-02/03/07).

Drives /api/collab/* with three accounts and asserts the contract frozen in
README "协作域契约（A327 提案）":

  1. fresh DB, boot the exe (ZAN_NO_BG=1 while A321 is open — see e2e_im.py)
  2. admin cookie login -> create accounts B, C -> bind roles 1+2
  3. token login A (admin), B, C
  4. A conversations -> empty
  5. A creates group with B (clientRequestId) -> id; same-key replay -> same
     id + replayed=1, no second conversation
  6. visibility: A owner / B member; C (non-member) members -> 403; C
     conversations -> empty; forged conversationId -> 404
  7. cross-tenant: B moved to tenant 2 via direct sqlite -> conversations
     empty, members -> 404 (same as nonexistent, no existence leak); restored
     -> visible again
  8. role gating: B (plain member) invite -> 403; A invites C -> added=1;
     invite replay -> replayed=1 added=1; re-invite active member -> added=0
     (no duplicate row)
  9. leave: C leaves -> members 403 again; leave replay -> replayed=1; leave
     when already out -> ok (natural idempotency); A (owner) leave -> 403
 10. rejoin: A invites B back -> left row reactivated (no duplicate)
 11. kick: A kicks B -> member count drops; kick replay -> replayed=1; kick
     an already-left member -> ok
 12. negatives: missing clientRequestId / missing title rejected; non-member
     kick -> 403

The script manages the server lifecycle itself (mirrors e2e_im.py):
  python tools/e2e_collab.py [--exe PATH]
Stdlib urllib/socket/sqlite3/subprocess only.
"""
import argparse
import json
import os
import re
import socket
import sqlite3
import subprocess
import sys
import time
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
opener = urllib.request.build_opener(NoRedirect)

def http(path, data=None, cookie=None):
    req = urllib.request.Request(BASE + path)
    if cookie: req.add_header("Cookie", cookie)
    req.add_header("X-Fragment", "1")
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    if body is not None: req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = opener.open(req, body, timeout=10)
        return resp.status, resp.read().decode("utf-8", "replace"), resp.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), e.headers.get_all("Set-Cookie") or []

def call(method, path, data=None, token=None):
    """Full {code,msg,data} envelope -> (status, dict-or-None). Retries only
    on transport errors (same discipline as e2e_im.api)."""
    raw = None
    st = 0
    for attempt in range(5):
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
            break
        except urllib.error.HTTPError as e:
            st = e.code
            raw = e.read().decode("utf-8", "replace")
            break
        except (urllib.error.URLError, OSError, HTTPException) as e:
            if attempt == 4:
                print("  [net] %s %s failed after 5 tries: %r" % (method, path, e))
                return 0, None
            time.sleep(0.2 + 0.2 * attempt)
    try:
        return st, json.loads(raw)
    except Exception:
        return st, None

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
    log = open(os.path.join(ROOT, "data", "e2e_collab_server.log"), "ab")
    # ZAN_NO_BG=1：A321（后台 ORM 协程与请求并发的响应损坏）修复前与 e2e_im.py
    # 同口径；后台协程开启的实时链路验证是 A327-05 的独立脚本，不混在本契约里。
    env = dict(os.environ)
    env["ZAN_NO_BG"] = "1"
    proc = subprocess.Popen([exe], cwd=ROOT, stdout=log, stderr=log, env=env)
    if not wait_port():
        raise SystemExit("server did not listen on %d; see data/e2e_collab_server.log" % PORT)
    time.sleep(1.5)
    return proc

def stop_server(proc):
    # master + worker 树一起杀（见 e2e_im.stop_server：孤儿 worker 攥监听端口）。
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
    time.sleep(1)

def set_tenant(username, tenant):
    """跨租户负向测试的搬运工：直接改库（单租户恒 1 的一期没有第二个租户
    的 API 面）。WAL 允许与服务进程并发写；tenantId=2 不会被启动回填
    （只归 NULL/0）改写。"""
    p = os.path.join(ROOT, "data", "app.db")
    cx = sqlite3.connect(p, timeout=15)
    cx.execute("UPDATE sys_user SET tenantId=? WHERE username=?", (tenant, username))
    cx.commit()
    cx.close()

TS = str(int(time.time()))[-6:]

def png_bytes(w, h):
    # 服务器只嗅探签名 + IHDR 宽高（偏移 16..24 大端），无需合法 CRC
    return (b"\x89PNG\r\n\x1a\n" + (13).to_bytes(4, "big") + b"IHDR"
            + w.to_bytes(4, "big") + h.to_bytes(4, "big")
            + b"\x08\x06\x00\x00\x00")

def upload(name, data, token, crid):
    """原始请求体即文件字节（服务端 [Upload] 流式管道），元数据走 query。"""
    qs = urllib.parse.urlencode({"name": name, "clientRequestId": crid})
    req = urllib.request.Request(BASE + "/api/collab/uploadattachment?" + qs,
                                 data=data, method="POST")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Content-Type", "application/octet-stream")
    try:
        resp = urllib.request.urlopen(req, timeout=10)
        st, raw = resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        st, raw = e.code, e.read().decode("utf-8", "replace")
    except (urllib.error.URLError, OSError, HTTPException):
        return 0, None
    try:
        return st, json.loads(raw)
    except Exception:
        return st, None

def download(att_id, token):
    req = urllib.request.Request(
        BASE + "/api/collab/downloadattachment?id=%d" % att_id)
    req.add_header("Authorization", "Bearer " + token)
    try:
        resp = urllib.request.urlopen(req, timeout=10)
        return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, b""
    except (urllib.error.URLError, OSError, HTTPException):
        return 0, b""

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
    crid = lambda tag: "e2e-%s-%s" % (TS, tag)

    # ---- admin cookie login + create accounts B, C ----------------------
    st, _, setc = http("/admin/login", data={"user": "admin", "pass": "admin1234"})
    cookie = "".join(x.split(";")[0] + "; " for x in setc)
    ok(st in (200, 302) and cookie != "", "admin cookie login")

    ids = {}
    for uname, nick in (("cx_b_" + TS, "冰冰" + TS), ("cx_c_" + TS, "灿灿" + TS)):
        st, j, _ = http("/admin/system/users/save", data={
            "username": uname, "nickname": nick,
            "email": uname + "@demo.local", "mobile": "1390000" + str(len(ids)) + TS[-4:],
            "departmentId": "0", "status": "1", "password": "cxpass2026"},
            cookie=cookie)
        try:
            saved = json.loads(j).get("code") == "0000"
        except Exception:
            saved = False
        ok(saved, "create account %s" % uname)
        st, j, _ = http("/admin/system/users?kw=" + urllib.parse.quote(uname), cookie=cookie)
        m = re.search(uname + r"[\s\S]{0,400}?users/roles\?id=(\d+)", j)
        uid = int(m.group(1)) if m else 0
        ok(uid > 0, "%s id listed in admin users" % uname)
        st, j, _ = http("/admin/system/users/roles?id=" + str(uid), cookie=cookie)
        listed = re.findall(r'name="roleIds\[\]" value="(\d+)"', j)
        ok("1" in listed and "2" in listed, "roles dialog lists built-in roles (%s)" % uname)
        form = [("id", str(uid)), ("roleIds[]", "1"), ("roleIds[]", "2")]
        st, j, _ = http("/admin/system/users/rolessave", data=form, cookie=cookie)
        try:
            ok(json.loads(j).get("code") == "0000", "assign roles to %s" % uname)
        except Exception:
            ok(False, "assign roles to %s" % uname)
        ids[uname] = uid
    bid = ids["cx_b_" + TS]
    cid = ids["cx_c_" + TS]

    # ---- token logins ----------------------------------------------------
    st, env = call("POST", "/api/auth/login", {"user": "admin", "pass": "admin1234"})
    ta = (env or {}).get("data", {}).get("token", "")
    ok(ta != "", "A (admin) token login")
    st, env = call("POST", "/api/auth/login", {"user": "cx_b_" + TS, "pass": "cxpass2026"})
    tb = (env or {}).get("data", {}).get("token", "")
    ok(tb != "", "B token login")
    st, env = call("POST", "/api/auth/login", {"user": "cx_c_" + TS, "pass": "cxpass2026"})
    tc = (env or {}).get("data", {}).get("token", "")
    ok(tc != "", "C token login")

    def data_of(env):
        return (env or {}).get("data") or {}

    # ---- A starts empty --------------------------------------------------
    st, env = call("GET", "/api/collab/conversations", token=ta)
    ok(st == 200 and len(data_of(env).get("conversations") or []) == 0,
       "A conversations starts empty")

    # ---- A creates group with B; idempotent replay -----------------------
    k1 = crid("c1")
    st, env = call("POST", "/api/collab/create",
                   {"title": "项目组" + TS, "ids": str(bid), "clientRequestId": k1},
                   token=ta)
    conv = int(data_of(env).get("id", "0"))
    ok(st == 200 and conv > 0 and data_of(env).get("added") == "1", "A create group with B")
    ok(data_of(env).get("replayed") is None, "first create not marked replayed")

    st, env = call("POST", "/api/collab/create",
                   {"title": "项目组" + TS, "ids": str(bid), "clientRequestId": k1},
                   token=ta)
    ok(st == 200 and int(data_of(env).get("id", "0")) == conv
       and data_of(env).get("replayed") == "1", "same-key create replays original id")

    # ---- visibility -------------------------------------------------------
    st, env = call("GET", "/api/collab/conversations", token=ta)
    rows = data_of(env).get("conversations") or []
    ok(len(rows) == 1 and rows[0]["role"] == "owner" and rows[0]["members"] == "2",
       "A sees the group as owner with 2 members")
    st, env = call("GET", "/api/collab/conversations", token=tb)
    rows = data_of(env).get("conversations") or []
    ok(len(rows) == 1 and rows[0]["role"] == "member", "B sees the group as member")
    st, env = call("GET", "/api/collab/conversations", token=tc)
    ok(len(data_of(env).get("conversations") or []) == 0, "C sees no conversations")

    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=tc)
    ok(st == 403, "C (non-member) members -> 403")
    st, env = call("GET", "/api/collab/members?conversationId=999999", token=ta)
    ok(st == 404, "forged conversationId -> 404")
    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=ta)
    mem = data_of(env).get("members") or []
    ok(st == 200 and len(mem) == 2 and mem[0]["role"] == "owner", "A members lists 2 with owner first")

    # ---- cross-tenant: B moved to tenant 2 --------------------------------
    set_tenant("cx_b_" + TS, 2)
    st, env = call("GET", "/api/collab/conversations", token=tb)
    ok(len(data_of(env).get("conversations") or []) == 0,
       "cross-tenant B conversations empty")
    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=tb)
    ok(st == 404, "cross-tenant B members -> 404 (no existence leak)")
    set_tenant("cx_b_" + TS, 1)
    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=tb)
    ok(st == 200, "restored B sees members again")

    # ---- role gating on invite --------------------------------------------
    st, env = call("POST", "/api/collab/invite",
                   {"conversationId": str(conv), "ids": str(cid),
                    "clientRequestId": crid("i0")}, token=tb)
    ok(st == 403, "B (plain member) invite -> 403")

    k2 = crid("i1")
    st, env = call("POST", "/api/collab/invite",
                   {"conversationId": str(conv), "ids": str(cid),
                    "clientRequestId": k2}, token=ta)
    ok(st == 200 and data_of(env).get("added") == "1", "A invites C -> added=1")
    st, env = call("POST", "/api/collab/invite",
                   {"conversationId": str(conv), "ids": str(cid),
                    "clientRequestId": k2}, token=ta)
    ok(st == 200 and data_of(env).get("replayed") == "1"
       and data_of(env).get("added") == "1", "invite replay -> replayed=1")
    st, env = call("POST", "/api/collab/invite",
                   {"conversationId": str(conv), "ids": str(cid),
                    "clientRequestId": crid("i2")}, token=ta)
    ok(st == 200 and data_of(env).get("added") == "0", "re-invite active member -> added=0")
    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=ta)
    ok(len(data_of(env).get("members") or []) == 3, "members count 3 (no duplicate rows)")

    # ---- messages: three-member send/receive (A327-07/08 slice) ----------
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "会话消息一号-" + TS,
                    "clientRequestId": crid("m1")}, token=ta)
    mid1 = int(data_of(env).get("id", "0"))
    ok(st == 200 and mid1 > 0, "A sends text message")
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "会话消息一号-" + TS,
                    "clientRequestId": crid("m1")}, token=ta)
    ok(st == 200 and int(data_of(env).get("id", "0")) == mid1
       and data_of(env).get("replayed") == "1", "send replay -> same id replayed=1")

    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=tb)
    msgs = data_of(env).get("msgs") or []
    ok(st == 200 and len(msgs) == 1 and msgs[0]["mine"] == "0"
       and msgs[0]["content"] == "会话消息一号-" + TS,
       "B history sees A's message as incoming")
    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=ta)
    msgs = data_of(env).get("msgs") or []
    ok(len(msgs) == 1 and msgs[0]["mine"] == "1", "A history marks own message mine=1")
    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=tc)
    ok(len(data_of(env).get("msgs") or []) == 1,
       "C (3rd member) history sees it too - group send/receive closed loop")

    # unread: history does NOT clear watermark; explicit read does
    st, env = call("GET", "/api/collab/conversations", token=tb)
    rows = [r for r in data_of(env).get("conversations") or []
            if r["id"] == str(conv)]
    ok(rows and rows[0].get("unread") == "1"
       and rows[0].get("lastText") == "会话消息一号-" + TS,
       "B conversations unread=1 with lastText (history did not clear)")
    st, env = call("POST", "/api/collab/read",
                   {"conversationId": str(conv)}, token=tb)
    ok(st == 200 and int(data_of(env).get("readUpTo", "0")) >= mid1,
       "B read -> watermark advanced")
    st, env = call("GET", "/api/collab/conversations", token=tb)
    rows = [r for r in data_of(env).get("conversations") or []
            if r["id"] == str(conv)]
    ok(rows and rows[0].get("unread") == "0", "B unread back to 0 after read")

    # quote-reply: quoteWho is reader-relative
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "收到-" + TS,
                    "replyTo": str(mid1), "clientRequestId": crid("m2")}, token=tb)
    mid2 = int(data_of(env).get("id", "0"))
    ok(st == 200 and mid2 > mid1, "B quote-replies")
    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=ta)
    msgs = data_of(env).get("msgs") or []
    last = msgs[-1] if msgs else {}
    ok(last.get("replyTo") == str(mid1) and last.get("quoteWho") == "我",
       "A sees quote with quoteWho=me (reader-relative)")
    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=tc)
    msgs = data_of(env).get("msgs") or []
    last = msgs[-1] if msgs else {}
    ok(last.get("quoteWho") not in (None, "我"),
       "C sees quote attributed to the sender's name")

    # cursor pagination: before=mid2&limit=1 -> exactly [mid1]
    st, env = call("GET",
                   "/api/collab/messages?conversationId=%d&before=%d&limit=1"
                   % (conv, mid2), token=ta)
    msgs = data_of(env).get("msgs") or []
    ok(len(msgs) == 1 and int(msgs[0]["id"]) == mid1,
       "cursor page before+limit returns exact window")

    # forged replyTo falls back to a plain message (no dangling reference)
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "伪造引用-" + TS,
                    "replyTo": "999999", "clientRequestId": crid("m3")}, token=ta)
    ok(st == 200, "forged replyTo accepted as plain message")
    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=ta)
    msgs = data_of(env).get("msgs") or []
    ok(bool(msgs) and msgs[-1].get("replyTo") is None,
       "forged replyTo stored as replyTo=0")

    # cross-tenant send -> 404 (no existence leak)
    set_tenant("cx_c_" + TS, 2)
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "跨界",
                    "clientRequestId": crid("m4")}, token=tc)
    ok(st == 404, "cross-tenant send -> 404")
    set_tenant("cx_c_" + TS, 1)

    # empty content rejected
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "",
                    "clientRequestId": crid("m5")}, token=ta)
    ok(st != 200 or (env or {}).get("code") != "0000", "empty content rejected")

    # ---- attachments: upload / bind / download (A327-09) ------------------
    png = png_bytes(64, 32)
    k_att = crid("a1")
    st, env = upload("截图" + TS + ".png", png, ta, k_att)
    att_png = int(data_of(env).get("id", "0"))
    ok(st == 200 and att_png > 0 and data_of(env).get("width") == "64"
       and data_of(env).get("height") == "32"
       and data_of(env).get("mime") == "image/png",
       "A uploads PNG with server-sniffed dimensions")
    st, env = upload("截图" + TS + ".png", png, ta, k_att)
    ok(st == 200 and int(data_of(env).get("id", "0")) == att_png
       and data_of(env).get("replayed") == "1", "upload replay -> same id replayed=1")
    st, env = upload("..\\..\\evil" + TS + ".txt", "遍历测试".encode(), ta, crid("a2"))
    ok(st == 200 and data_of(env).get("name") == "evil" + TS + ".txt",
       "path traversal name sanitized to basename")
    st, env = upload("notes" + TS + ".txt", ("附件正文-" + TS).encode(), ta, crid("a3"))
    att_txt = int(data_of(env).get("id", "0"))
    ok(st == 200 and att_txt > 0, "A uploads txt")

    st, _er = download(att_txt, tb)
    ok(st == 403, "B downloads unbound attachment -> 403 (uploader only)")
    st, _er = download(999999, ta)
    ok(st == 404, "forged attachment download -> 404")
    st, env = upload("empty" + TS + ".txt", b"", ta, crid("a4"))
    ok(st != 200 or (env or {}).get("code") != "0000", "empty upload rejected")

    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "attachmentId": str(att_png),
                    "clientRequestId": crid("m7")}, token=ta)
    ok(st == 200 and int(data_of(env).get("id", "0")) > 0,
       "A sends image message (empty caption ok)")
    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "attachmentId": str(att_txt),
                    "content": "", "clientRequestId": crid("m8")}, token=ta)
    ok(st == 200, "A sends file message")
    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=tb)
    msgs = data_of(env).get("msgs") or []
    img_row = [m for m in msgs if m.get("kind") == "image"]
    file_row = [m for m in msgs if m.get("kind") == "file"]
    ok(len(img_row) == 1 and img_row[0].get("width") == "64"
       and img_row[0].get("attachmentId") == str(att_png),
       "history image row carries attachment meta with dimensions")
    ok(len(file_row) == 1 and file_row[0].get("fileName", "").startswith("notes"),
       "history file row carries fileName")

    st, got = download(att_png, tb)
    ok(st == 200 and got == png, "B downloads bound PNG, bytes roundtrip equal")
    st, got = download(att_png, tc)
    ok(st == 200 and got == png, "C (member) downloads bound PNG too")
    st, got = download(att_txt, tb)
    ok(st == 200 and got == ("附件正文-" + TS).encode(), "txt roundtrip equal")

    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "attachmentId": str(att_png),
                    "clientRequestId": crid("m9")}, token=ta)
    ok(st == 400, "re-binding an attachment rejected (400, not 500)")

    set_tenant("cx_c_" + TS, 2)
    st, _got = download(att_png, tc)
    ok(st == 404, "cross-tenant download -> 404")
    set_tenant("cx_c_" + TS, 1)

    # ---- tasks: state machine, claim CAS, audit timeline (A327-10) --------
    k_t1 = crid("t1")
    st, env = call("POST", "/api/collab/taskcreate",
                   {"title": "任务一号" + TS, "conversationId": str(conv),
                    "description": "跑通状态机", "clientRequestId": k_t1},
                   token=ta)
    task1 = int(data_of(env).get("id", "0"))
    ok(st == 200 and task1 > 0 and data_of(env).get("status") == "unassigned",
       "A creates task (unassigned)")
    st, env = call("POST", "/api/collab/taskcreate",
                   {"title": "任务一号" + TS, "conversationId": str(conv),
                    "clientRequestId": k_t1}, token=ta)
    ok(st == 200 and int(data_of(env).get("id", "0")) == task1
       and data_of(env).get("replayed") == "1", "task create replay -> same id")

    st, env = call("POST", "/api/collab/taskclaim",
                   {"id": str(task1), "clientRequestId": crid("tc1")}, token=tb)
    ok(st == 200 and data_of(env).get("status") == "claimed", "B claims first")
    st, env = call("POST", "/api/collab/taskclaim",
                   {"id": str(task1), "clientRequestId": crid("tc2")}, token=tc)
    ok(st == 409, "C claims same task -> 409 (CAS, exactly one winner)")

    st, env = call("POST", "/api/collab/taskprogress",
                   {"id": str(task1), "progress": "40", "clientRequestId": crid("tp0")},
                   token=tc)
    ok(st == 403, "non-assignee progress -> 403")
    st, env = call("POST", "/api/collab/taskstart",
                   {"id": str(task1), "clientRequestId": crid("ts1")}, token=tb)
    ok(st == 200 and data_of(env).get("status") == "in_progress", "B starts")
    st, env = call("POST", "/api/collab/taskprogress",
                   {"id": str(task1), "progress": "40",
                    "content": "完成四成", "clientRequestId": crid("tp1")}, token=tb)
    ok(st == 200 and data_of(env).get("progress") == "40", "B progress 40")
    st, env = call("POST", "/api/collab/taskblock",
                   {"id": str(task1), "clientRequestId": crid("tb1")}, token=tb)
    ok(st != 200 or (env or {}).get("code") != "0000", "block without reason rejected")
    st, env = call("POST", "/api/collab/taskblock",
                   {"id": str(task1), "reason": "等上游接口",
                    "clientRequestId": crid("tb1")}, token=tb)
    ok(st == 200 and data_of(env).get("status") == "blocked", "B blocks with reason")
    st, env = call("POST", "/api/collab/taskunblock",
                   {"id": str(task1), "clientRequestId": crid("tu1")}, token=tb)
    ok(st == 200 and data_of(env).get("status") == "in_progress", "B unblocks")
    st, env = call("POST", "/api/collab/tasksubmit",
                   {"id": str(task1), "clientRequestId": crid("tsu1")}, token=tb)
    ok(st == 200 and data_of(env).get("status") == "review", "B submits for review")
    st, env = call("POST", "/api/collab/taskapprove",
                   {"id": str(task1), "clientRequestId": crid("ta1")}, token=tb)
    ok(st == 403, "assignee cannot self-approve")
    st, env = call("POST", "/api/collab/taskapprove",
                   {"id": str(task1), "clientRequestId": crid("ta1")}, token=ta)
    ok(st == 200 and data_of(env).get("status") == "done", "A approves -> done")

    st, env = call("GET", "/api/collab/taskdetail?id=%d" % task1, token=tb)
    evs = data_of(env).get("events") or []
    kinds = [e["event"] for e in evs]
    ok(st == 200 and kinds == ["created", "claimed", "started", "progress",
                               "blocked", "unblocked", "submitted", "approved"],
       "timeline immutable and complete in order")
    pr = [e for e in evs if e["event"] == "progress"]
    ok(len(pr) == 1 and pr[0]["fromProgress"] == "0" and pr[0]["toProgress"] == "40",
       "progress event carries from/to snapshot")

    st, env = call("POST", "/api/collab/taskcreate",
                   {"title": "任务二号" + TS, "conversationId": str(conv),
                    "clientRequestId": crid("t2")}, token=ta)
    task2 = int(data_of(env).get("id", "0"))
    st, env = call("POST", "/api/collab/taskassign",
                   {"id": str(task2), "userId": str(cid),
                    "clientRequestId": crid("ta2")}, token=ta)
    ok(st == 200 and data_of(env).get("assigneeId") == str(cid),
       "A assigns task2 to C")
    st, env = call("POST", "/api/collab/taskassign",
                   {"id": str(task2), "userId": str(bid),
                    "clientRequestId": crid("ta3")}, token=ta)
    ok(st != 200 or (env or {}).get("code") != "0000",
       "transfer without reason rejected")
    st, env = call("POST", "/api/collab/taskassign",
                   {"id": str(task2), "userId": str(bid), "reason": "C 休假",
                    "clientRequestId": crid("ta3")}, token=ta)
    ok(st == 200 and data_of(env).get("assigneeId") == str(bid),
       "transfer to B with reason")

    st, env = call("POST", "/api/collab/taskcreate",
                   {"title": "任务三号" + TS, "conversationId": str(conv),
                    "dueAt": "1000000000", "clientRequestId": crid("t3")}, token=ta)
    task3 = int(data_of(env).get("id", "0"))
    st, env = call("GET", "/api/collab/tasksmine", token=ta)
    rows = data_of(env).get("tasks") or []
    r3 = [r for r in rows if r["id"] == str(task3)]
    ok(st == 200 and bool(r3) and r3[0].get("overdue") == "1",
       "overdue derived from dueAt (past deadline, not done)")
    st, env = call("GET", "/api/collab/taskspool?conversationId=%d" % conv, token=tb)
    pool_ids = [r["id"] for r in data_of(env).get("tasks") or []]
    ok(str(task3) in pool_ids, "unassigned task3 visible in pool")

    st, env = call("POST", "/api/collab/taskcancel",
                   {"id": str(task3), "reason": "需求变更",
                    "clientRequestId": crid("tx1")}, token=ta)
    ok(st == 200 and data_of(env).get("status") == "cancelled", "A cancels task3")
    st, env = call("POST", "/api/collab/taskclaim",
                   {"id": str(task3), "clientRequestId": crid("tc3")}, token=tb)
    ok(st == 409, "claim cancelled task -> 409")

    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "taskId": str(task1),
                    "clientRequestId": crid("mcard")}, token=ta)
    ok(st == 200, "A sends task card message")
    st, env = call("GET", "/api/collab/messages?conversationId=%d" % conv, token=tb)
    msgs = data_of(env).get("msgs") or []
    cards = [m for m in msgs if m.get("kind") == "task"]
    if not (len(cards) == 1 and cards[0].get("taskId") == str(task1)
            and cards[0].get("taskStatus") == "done"):
        print("  [dbg] cards=%r" % (cards,))
        print("  [dbg] tail=%r" % (msgs[-3:],))
    ok(len(cards) == 1 and cards[0].get("taskId") == str(task1)
       and cards[0].get("taskStatus") == "done",
       "task card message carries taskId and live status")
    k3 = crid("l1")
    st, env = call("POST", "/api/collab/leave",
                   {"conversationId": str(conv), "clientRequestId": k3}, token=tc)
    ok(st == 200 and data_of(env).get("ok") == "1", "C leaves")
    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=tc)
    ok(st == 403, "C after leave members -> 403")
    st, env = call("POST", "/api/collab/leave",
                   {"conversationId": str(conv), "clientRequestId": k3}, token=tc)
    ok(st == 200 and data_of(env).get("replayed") == "1", "leave replay -> replayed=1")
    st, env = call("POST", "/api/collab/leave",
                   {"conversationId": str(conv), "clientRequestId": crid("l2")}, token=tc)
    ok(st == 200 and data_of(env).get("ok") == "1"
       and data_of(env).get("replayed") is None, "leave when already out -> ok (natural idempotent)")
    st, env = call("POST", "/api/collab/leave",
                   {"conversationId": str(conv), "clientRequestId": crid("l3")}, token=ta)
    ok(st == 403, "owner leave -> 403")

    # ---- rejoin: B out and back in (row reactivation) -----------------------
    st, env = call("POST", "/api/collab/leave",
                   {"conversationId": str(conv), "clientRequestId": crid("l4")}, token=tb)
    ok(st == 200, "B leaves for rejoin test")
    st, env = call("POST", "/api/collab/invite",
                   {"conversationId": str(conv), "ids": str(bid),
                    "clientRequestId": crid("i3")}, token=ta)
    ok(st == 200 and data_of(env).get("added") == "1", "A re-invites B (reactivates row)")
    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=ta)
    mem = data_of(env).get("members") or []
    ok(len(mem) == 2 and len([m for m in mem if m["userId"] == str(bid)]) == 1,
       "B rejoined as single active row")

    # ---- kick ----------------------------------------------------------------
    k4 = crid("k1")
    st, env = call("POST", "/api/collab/kick",
                   {"conversationId": str(conv), "userId": str(bid),
                    "clientRequestId": k4}, token=ta)
    ok(st == 200 and data_of(env).get("ok") == "1", "A kicks B")
    st, env = call("GET", "/api/collab/members?conversationId=%d" % conv, token=ta)
    ok(len(data_of(env).get("members") or []) == 1, "members count back to 1")
    st, env = call("POST", "/api/collab/kick",
                   {"conversationId": str(conv), "userId": str(bid),
                    "clientRequestId": k4}, token=ta)
    ok(st == 200 and data_of(env).get("replayed") == "1", "kick replay -> replayed=1")
    st, env = call("POST", "/api/collab/kick",
                   {"conversationId": str(conv), "userId": str(cid),
                    "clientRequestId": crid("k2")}, token=ta)
    ok(st == 200 and data_of(env).get("ok") == "1", "kick already-left member -> ok")

    # ---- negatives ------------------------------------------------------------
    st, env = call("POST", "/api/collab/create", {"title": "无键群"}, token=ta)
    ok(st != 200 or (env or {}).get("code") != "0000", "create without clientRequestId rejected")
    st, env = call("POST", "/api/collab/create", {"clientRequestId": crid("n1")}, token=ta)
    ok(st != 200 or (env or {}).get("code") != "0000", "create without title rejected")
    st, env = call("POST", "/api/collab/kick",
                   {"conversationId": str(conv), "userId": str(ta and 1),
                    "clientRequestId": crid("n2")}, token=tc)
    ok(st == 403, "non-member kick -> 403")

    # ---- events: cursor replay + entitlement (A327-04) --------------------
    st, env = call("GET", "/api/collab/events?after=0", token=ta)
    evs = data_of(env).get("events") or []
    ok(st == 200 and len(evs) > 0, "A events replay non-empty")
    kinds = [e.get("event") for e in evs]
    ok("conversation.created" in kinds and "message.created" in kinds
       and "member.joined" in kinds and "member.left" in kinds,
       "all four event kinds present in replay")
    ev_ids = [int(e["eventId"]) for e in evs]
    ok(ev_ids == sorted(ev_ids) and len(set(ev_ids)) == len(ev_ids),
       "events ascending and unique")
    ok(all(e.get("conversationId") == str(conv) for e in evs),
       "events scoped to the conversation")
    kick_ev = [e for e in evs
               if e.get("event") == "member.left" and e.get("kicked") == "1"]
    ok(len(kick_ev) >= 1, "kick carries kicked=1 in payload")
    m1ev = [e for e in evs if e.get("event") == "message.created"
            and e.get("messageId") == str(mid1)]
    ok(len(m1ev) == 1 and "会话消息一号" in (m1ev[0].get("excerpt") or ""),
       "message.created carries excerpt")

    nxt = int(data_of(env).get("next", "0"))
    ok(nxt == ev_ids[-1], "next cursor is the largest eventId")
    st, env = call("GET", "/api/collab/events?after=%d" % nxt, token=ta)
    ok(len(data_of(env).get("events") or []) == 0, "incremental pull empty at cursor")

    st, env = call("POST", "/api/collab/send",
                   {"conversationId": str(conv), "content": "增量事件-" + TS,
                    "clientRequestId": crid("m6")}, token=ta)
    ok(st == 200, "A sends one more message")
    st, env = call("GET", "/api/collab/events?after=%d" % nxt, token=ta)
    evs = data_of(env).get("events") or []
    ok(len(evs) == 1 and evs[0].get("event") == "message.created"
       and evs[0].get("excerpt") == "增量事件-" + TS,
       "incremental pull returns exactly the new event")

    # entitlement: kicked B and left C must not see the conversation events
    st, env = call("GET", "/api/collab/events?after=0", token=tb)
    evs = data_of(env).get("events") or []
    ok(all(e.get("conversationId") != str(conv) for e in evs),
       "kicked B sees none of the conversation events")
    st, env = call("GET", "/api/collab/events?after=0", token=tc)
    evs = data_of(env).get("events") or []
    ok(all(e.get("conversationId") != str(conv) for e in evs),
       "left C sees none of the conversation events")

if __name__ == "__main__":
    main()

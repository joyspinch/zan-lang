"""Contract e2e for the server-mvc template.

Drives a live instance over HTTP with per-endpoint assertions, plus direct
sqlite reads for persistence contracts. Covers the chains the template
actually ships:

  infra     health shallow/deep, front page, static assets
  auth      admin login (bad/good), session cookie, anonymous redirect guard
  content   category save / inline quick (rename, clamp, unknown field and
            identity slug rejected, anonymous rejected), dict item quick
            toggle, SQL-ish payload stored safely
  coder     designer three-step (save design -> add column -> migrate DDL,
            verified via PRAGMA), inline quick on a designed column, AI
            guard when AI unconfigured, preview, delete design
  monitor   index / sql / history / alerts shape / generic data manager / docs
  mail      SMTP catcher (local sink, stdlib socket): unconfigured guard,
            settings save flips mail.ssl off, forgot-code delivery (base64
            body decode -> 6-digit code), 60s resend cooldown, wrong code,
            real reset (old password rejected, session invalidated, new
            password logs in)

The script manages the server lifecycle itself:
  1. refresh a sandbox in _scratch/mvc_e2e/ (config on port 8299, cache in
     memory, worker 1, fresh data/, views+wwwroot copied from the template)
  2. build the exe with zanc when the sandbox has none or --build
  3. boot `app.exe start`, wait for /health
  4. run the matrix, stop the server via its control port -- the netstat
     fallback only ever kills PIDs listening on the sandbox port, never by
     image name (the dev instance shares it)

Run from anywhere; paths resolve from this file's location:
  python tools/e2e_mvc.py [--exe PATH] [--build] [--port 8299] [--keep]
Stdlib urllib/socket/subprocess/sqlite3 only.
"""
import argparse
import base64
import json
import os
import re
import shutil
import socket
import sqlite3
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TMPL = os.path.dirname(HERE)                    # templates/server/server-mvc
# repo root is three levels up: templates/server/server-mvc -> .. -> .. -> ..
REPO = os.path.dirname(os.path.dirname(os.path.dirname(TMPL)))
SANDBOX = os.path.join(REPO, "_scratch", "mvc_e2e")
EXE = os.path.join(SANDBOX, "app.exe")
DB = os.path.join(SANDBOX, "data", "app.db")
MAIL_PORT = 8725

BASE = "http://127.0.0.1:8299"
fails = []
checks = 0


def ok(cond, label):
    global checks
    checks += 1
    print(("PASS " if cond else "FAIL ") + label)
    if not cond:
        fails.append(label)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):
        return None


opener = urllib.request.build_opener(NoRedirect)


def http(path, data=None, cookie=None, timeout=15):
    """Returns (status, body, set_cookie_lines). Never raises on HTTP errors."""
    req = urllib.request.Request(BASE + path)
    if cookie:
        req.add_header("Cookie", cookie)
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    if body is not None:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        resp = opener.open(req, body, timeout=timeout)
        return (resp.status, resp.read().decode("utf-8", "replace"),
                resp.headers.get_all("Set-Cookie") or [])
    except urllib.error.HTTPError as e:
        return (e.code, e.read().decode("utf-8", "replace"),
                e.headers.get_all("Set-Cookie") or [])


def raw_post(path, payload, cookie=None, timeout=15):
    """POST raw bytes (streaming upload probe). Returns the decoded body."""
    req = urllib.request.Request(BASE + path, data=payload, method="POST")
    if cookie:
        req.add_header("Cookie", cookie)
    req.add_header("Content-Type", "application/octet-stream")
    try:
        resp = opener.open(req, timeout=timeout)
        return resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.read().decode("utf-8", "replace")


def raw_get(path, timeout=15):
    """GET returning (status, raw bytes) — for binary static checks."""
    try:
        resp = opener.open(BASE + path, timeout=timeout)
        return (resp.status, resp.read())
    except urllib.error.HTTPError as e:
        return (e.code, e.read())


def code_of(body):
    """JSON envelope code, or "" when the reply is not the API envelope."""
    try:
        return json.loads(body).get("code", "")
    except Exception:
        return ""


def ctype(path):
    """GET returning (status, Content-Type) — for feed/mime contract checks."""
    try:
        resp = opener.open(BASE + path, timeout=15)
        return (resp.status, resp.headers.get("Content-Type") or "")
    except urllib.error.HTTPError as e:
        return (e.code, e.headers.get("Content-Type") or "")


def session_cookie(setc):
    for line in setc:
        if line.startswith("zsession="):
            return line.split(";")[0]
    return ""


def sql(query, args=()):
    con = sqlite3.connect(DB, timeout=10)
    try:
        rows = con.execute(query, args).fetchall()
        con.commit()
        return rows
    finally:
        con.close()


def table_exists(name):
    return bool(sql(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)))


# ---- SMTP catcher ----------------------------------------------------------

class Catcher:
    """Minimal no-auth SMTP sink answering exactly the session Mailer speaks
    (220 -> EHLO -> MAIL/RCPT 250 -> DATA 354 -> dot-line -> 250 -> QUIT)
    and keeping every DATA payload for inspection."""

    def __init__(self, port):
        self.port = port
        self.mails = []
        self.lock = threading.Lock()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", port))
        self.sock.listen(4)
        threading.Thread(target=self.loop, daemon=True).start()

    def loop(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self.talk, args=(conn,),
                             daemon=True).start()

    def talk(self, conn):
        f = conn.makefile("rb")
        conn.sendall(b"220 e2e-catcher ready\r\n")
        in_data = False
        buf = b""
        while True:
            line = f.readline()
            if not line:
                break
            if in_data:
                if line in (b".\r\n", b".\n"):
                    with self.lock:
                        self.mails.append(buf)
                    buf = b""
                    in_data = False
                    conn.sendall(b"250 accepted\r\n")
                else:
                    buf += line
                continue
            cmd = line.strip().upper()
            if cmd.startswith(b"DATA"):
                in_data = True
                conn.sendall(b"354 go\r\n")
            elif cmd.startswith(b"QUIT"):
                conn.sendall(b"221 bye\r\n")
                break
            else:
                conn.sendall(b"250 ok\r\n")
        conn.close()

    def body(self):
        """Decoded text of the latest mail (base64 CTE per Mailer)."""
        with self.lock:
            if not self.mails:
                return ""
            raw = self.mails[-1].decode("utf-8", "replace")
        m = re.search(r"Content-Transfer-Encoding: base64\r\n\r\n(.+)",
                      raw, re.S)
        if not m:
            return raw
        return base64.b64decode(
            re.sub(r"\s+", "", m.group(1))).decode("utf-8", "replace")


# ---- lifecycle -------------------------------------------------------------

def build_exe():
    # ZANC 覆盖：编译器车道在途重建 build/zanc.exe 时，用已知好二进制跑本套件
    # （例：ZANC=_scratch/wt_xxx/build/zanc.exe）。缺省仍取 build/zanc.exe。
    zanc = os.environ.get("ZANC") or os.path.join(REPO, "build", "zanc.exe")
    if not os.path.exists(zanc):
        print("zanc not found at", zanc)
        sys.exit(2)
    srcs = [os.path.join(TMPL, "src", "main.zan")]
    for root, _dirs, files in os.walk(os.path.join(TMPL, "src")):
        for fn in sorted(files):
            if fn.endswith(".zan") and fn != "main.zan":
                srcs.append(os.path.join(root, fn))
    cmd = [zanc] + srcs + ["--auto-stdlib", "--publish", "-o", EXE]
    print("[build] zanc ... (%d sources)" % len(srcs))
    r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(EXE):
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit(2)


def setup_sandbox(port):
    # data/ 重建，uploads/（上传暂存）与 wwwroot/media（媒体落盘）清零：
    # 三处都是运行时产物，残留会让断言看到上一轮的幽灵文件。
    shutil.rmtree(os.path.join(SANDBOX, "data"), ignore_errors=True)
    os.makedirs(os.path.join(SANDBOX, "data"), exist_ok=True)
    shutil.rmtree(os.path.join(SANDBOX, "uploads"), ignore_errors=True)
    shutil.rmtree(os.path.join(SANDBOX, "wwwroot", "media"), ignore_errors=True)
    shutil.copytree(os.path.join(TMPL, "views"),
                    os.path.join(SANDBOX, "views"), dirs_exist_ok=True)
    shutil.copytree(os.path.join(TMPL, "wwwroot"),
                    os.path.join(SANDBOX, "wwwroot"), dirs_exist_ok=True)
    cfg = json.load(open(os.path.join(TMPL, "config", "app.json"),
                         encoding="utf-8"))
    cfg["server"]["name"] = "mvc-e2e"
    cfg["server"]["port"] = port
    cfg["worker"]["count"] = 1
    if "daemon" in cfg["worker"]:
        cfg["worker"]["daemon"] = False
    # 无 Redis 的沙箱：本地内存缓存，worker=1 下语义正确。
    cfg["cache"]["driver"] = "memory"
    cfg["metrics"]["flushSeconds"] = 2
    os.makedirs(os.path.join(SANDBOX, "config"), exist_ok=True)
    with open(os.path.join(SANDBOX, "config", "app.json"), "w",
              encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


def stop_server(port):
    subprocess.run([EXE, "stop"], cwd=SANDBOX, capture_output=True, timeout=30)
    for _ in range(20):
        try:
            http("/health", timeout=2)
            time.sleep(0.5)
        except Exception:
            return
    # 控制端口失灵的兜底：只杀监听沙箱端口的 PID，绝不按镜像名杀
    #（开发实例共用 app.exe 镜像名）。
    out = subprocess.run(["netstat", "-ano"], capture_output=True,
                         text=True).stdout
    pids = {ln.split()[-1] for ln in out.splitlines()
            if ":" + str(port) + " " in ln and "LISTENING" in ln}
    for pid in pids:
        subprocess.run(["taskkill", "/F", "/PID", pid], capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="",
                    help="reuse this built app.exe instead of compiling")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--port", type=int, default=8299)
    ap.add_argument("--keep", action="store_true",
                    help="keep the sandbox for inspection")
    a = ap.parse_args()

    global BASE
    BASE = "http://127.0.0.1:%d" % a.port

    setup_sandbox(a.port)
    if a.exe:
        shutil.copyfile(a.exe, EXE)
        # 运行时 DLL（sqlite/ssl 等）与 exe 同目录，由 --publish 落盘。
        for fn in os.listdir(os.path.dirname(os.path.abspath(a.exe))):
            if fn.lower().endswith(".dll"):
                shutil.copyfile(os.path.join(
                    os.path.dirname(os.path.abspath(a.exe)), fn),
                    os.path.join(SANDBOX, fn))
    elif a.build or not os.path.exists(EXE):
        build_exe()

    subprocess.Popen([EXE, "start"], cwd=SANDBOX,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    up = False
    for _ in range(60):
        try:
            code, _b, _c = http("/health", timeout=2)
            if code == 200:
                up = True
                break
        except Exception:
            pass
        time.sleep(0.5)
    if not up:
        print("server did not come up on", BASE)
        for log in ("zan_crash.log", "server.log", "run.log"):
            p = os.path.join(SANDBOX, log)
            if os.path.exists(p):
                print("--- %s ---" % log)
                print(open(p, encoding="utf-8", errors="replace").read()[-2000:])
        stop_server(a.port)
        sys.exit(2)
    print("[boot] health ok on", BASE)

    catcher = Catcher(MAIL_PORT)
    try:
        run_matrix(catcher)
    finally:
        stop_server(a.port)
        print("\n%d checks, %d failed" % (checks, len(fails)))
        for f in fails:
            print("  FAIL:", f)
        if not a.keep:
            shutil.rmtree(SANDBOX, ignore_errors=True)
    sys.exit(1 if fails else 0)


def run_matrix(catcher):
    # ---- infra ----
    code, body, _ = http("/health")
    ok(code == 200 and '"ok"' in body, "health shallow 200 ok")
    code, body, _ = http("/health?deep=1")
    ok(code == 200, "health deep 200")
    code, _b, _c = http("/static/css/admin.css?v=4")
    ok(code == 200, "static admin.css 200")
    code, body, _ = http("/")
    ok(code == 200 and len(body) > 200, "front page renders")

    # ---- auth ----
    code, _b, setc = http("/admin")
    ok(code in (301, 302) and not session_cookie(setc),
       "admin guards anonymous -> redirect")
    code, _b, setc = http("/admin/login", {"user": "admin", "pass": "wrong"})
    ok(code not in (301, 302) and not session_cookie(setc),
       "bad login rejected")
    code, _b, setc = http("/admin/login",
                          {"user": "admin", "pass": "admin1234"})
    cookie = session_cookie(setc)
    ok(code in (301, 302) and cookie, "admin login sets session")
    code, body, _ = http("/admin", cookie=cookie)
    ok(code == 200 and len(body) > 200, "dashboard renders with session")

    # ---- users: declared table + conf contract + generic field ----
    code, body, _ = http("/admin/system/users", cookie=cookie)
    ok(code == 200 and ">账号</th>" in body
       and 'data-table-key="/admin/system/users"' in body,
       "users table rendered from column declaration")
    ok('class="tag ok"' in body,
       "status badges rendered from tag declaration")
    ok('data-post="/admin/system/users/status"' in body and "{id}" not in body,
       "row ops rendered with {field} substitution")
    code, body, _ = http("/admin/system/users/conf", cookie=cookie)
    ok(code == 200 and '"resp_code":"0000"' in body and '"field":"username"' in body,
       "conf endpoint serves table config from declaration")
    ok('"type":"Input"' in body and '"type":"DateRange"' in body,
       "conf filters projected from declaration")
    r = http("/admin/system/users/field", cookie=cookie,
             data={"id": "1", "field": "nickname", "value": "e2e 改名"})
    ok(code_of(r[1]) == "0000", "field endpoint edits whitelisted column")
    ok(sql("SELECT nickname FROM sys_user WHERE id=1")[0][0] == "e2e 改名",
       "field edit persisted")
    r = http("/admin/system/users/field", cookie=cookie,
             data={"id": "1", "field": "passwordHash", "value": "hijack"})
    ok(code_of(r[1]) != "0000", "field endpoint rejects non-whitelisted column")
    ok(sql("SELECT passwordHash FROM sys_user WHERE id=1")[0][0] != "hijack",
       "credential material untouched")
    r = http("/admin/system/users/field",
             data={"id": "1", "field": "nickname", "value": "anon"})
    ok(code_of(r[1]) != "0000", "field endpoint rejects anonymous")

    # ---- content: categories ----
    http("/admin/content/categories/save", cookie=cookie, data={
        "name": "e2e 分类", "slug": "e2e-cat", "description": "e2e 建的",
        "sortOrder": "9", "status": "1"})
    ok(table_exists("blog_category") and
       sql("SELECT id FROM blog_category WHERE slug='e2e-cat'"),
       "category row persisted")
    cid = sql("SELECT id FROM blog_category WHERE slug='e2e-cat'")[0][0]
    code, body, _ = http("/admin/content/categories", cookie=cookie)
    ok(code == 200 and "e2e-cat" in body, "category listed")
    r = http("/admin/content/categories/quick", cookie=cookie,
             data={"id": cid, "field": "name", "value": "e2e 分类改"})
    ok(code_of(r[1]) == "0000", "category quick rename ok")
    ok(sql("SELECT name FROM blog_category WHERE id=?", (cid,))[0][0]
       == "e2e 分类改", "rename persisted")
    http("/admin/content/categories/quick", cookie=cookie,
         data={"id": cid, "field": "sortOrder", "value": "-5"})
    ok(sql("SELECT sortOrder FROM blog_category WHERE id=?", (cid,))[0][0] == 0,
       "negative sort clamped to 0")
    r = http("/admin/content/categories/quick", cookie=cookie,
             data={"id": cid, "field": "slug", "value": "hijack"})
    ok(code_of(r[1]) != "0000", "slug not inline-editable")
    ok(sql("SELECT slug FROM blog_category WHERE id=?", (cid,))[0][0]
       == "e2e-cat", "slug untouched")
    r = http("/admin/content/categories/quick",
             data={"id": cid, "field": "name", "value": "anon"})
    ok(code_of(r[1]) != "0000", "quick rejects anonymous")
    http("/admin/content/categories/save", cookie=cookie, data={
        "name": "'; DROP TABLE blog_category;--", "slug": "sqli-e2e",
        "sortOrder": "1", "status": "1"})
    ok(table_exists("blog_category") and
       sql("SELECT COUNT(*) FROM blog_category")[0][0] >= 2,
       "sql-ish name stored safely, table intact")

    # ---- dicts: item quick ----
    code, _b, _c = http("/admin/system/dicts", cookie=cookie)
    ok(code == 200, "dicts page renders")
    items = sql("SELECT id, status FROM sys_dict_item ORDER BY id LIMIT 1")
    if items:
        iid, st = items[0]
        r = http("/admin/system/dicts/itemquick", cookie=cookie,
                 data={"id": iid, "field": "status",
                       "value": "0" if st == 1 else "1"})
        ok(code_of(r[1]) == "0000", "dict item quick ok")
        ok(sql("SELECT status FROM sys_dict_item WHERE id=?", (iid,))[0][0]
           != st, "dict status flipped")
        r = http("/admin/system/dicts/itemquick", cookie=cookie,
                 data={"id": iid, "field": "value", "value": "hijack"})
        ok(code_of(r[1]) != "0000", "dict value not inline-editable")
    else:
        ok(False, "seeded dict items exist")

    # ---- jobs: in-process scheduler end to end ----
    code, body, _ = http("/admin/system/jobs", cookie=cookie)
    ok(code == 200 and "日志清理" in body, "jobs page lists seeded cleanup")
    r = http("/admin/system/jobs/save", cookie=cookie, data={
        "name": "e2e 心跳", "kind": "ping", "param": "",
        "intervalSec": "5", "enabled": "1"})
    ok(code_of(r[1]) == "0000" or r[0] in (200, 301, 302), "ping job created")
    jid = sql("SELECT id FROM sys_job WHERE name='e2e 心跳'")[0][0]
    time.sleep(8)
    ok(sql("SELECT COUNT(*) FROM sys_job_log WHERE jobId=?",
           (jid,))[0][0] >= 1, "scheduler fired the job on interval")
    ok(sql("SELECT lastOk FROM sys_job WHERE id=?", (jid,))[0][0] == 1,
       "ping run recorded ok")
    r = http("/admin/system/jobs/quick", cookie=cookie,
             data={"id": jid, "field": "enabled", "value": "0"})
    ok(code_of(r[1]) == "0000", "quick disable ok")
    before = sql("SELECT COUNT(*) FROM sys_job_log WHERE jobId=?",
                 (jid,))[0][0]
    time.sleep(7)
    ok(sql("SELECT COUNT(*) FROM sys_job_log WHERE jobId=?",
           (jid,))[0][0] == before, "disabled job not scheduled")
    r = http("/admin/system/jobs/run", cookie=cookie, data={"id": jid})
    ok(r[0] in (200, 301, 302), "run-now accepted")
    ok(sql("SELECT COUNT(*) FROM sys_job_log WHERE jobId=?",
           (jid,))[0][0] == before + 1, "run-now logged one run")
    r = http("/admin/system/jobs/quick", cookie=cookie,
             data={"id": jid, "field": "name", "value": "hijack"})
    ok(code_of(r[1]) != "0000", "job name not inline-editable")
    r = http("/admin/system/jobs/quick",
             data={"id": jid, "field": "enabled", "value": "0"})
    ok(code_of(r[1]) != "0000", "job quick rejects anonymous")
    code, body, _ = http("/admin/system/jobs/logs?id=%d" % jid, cookie=cookie)
    ok(code == 200 and "pong" in body, "job logs page shows runs")
    http("/admin/system/jobs/delete", cookie=cookie, data={"id": jid})
    ok(not sql("SELECT 1 FROM sys_job WHERE id=?", (jid,)) and
       not sql("SELECT 1 FROM sys_job_log WHERE jobId=?", (jid,)),
       "job delete removes logs too")

    # ---- wiki: spaces, docs, revisions, guards ----
    code, _b, _c = http("/admin/wiki", cookie=cookie)
    ok(code == 200, "wiki page renders")
    r = http("/admin/wiki/spacesave", cookie=cookie, data={
        "name": "e2e 研发库", "description": "研发部门知识库", "icon": "",
        "departmentId": "0", "sortOrder": "1", "status": "1"})
    ok(code_of(r[1]) == "0000" or r[0] in (200, 301, 302), "space created")
    sid = sql("SELECT id FROM wiki_space WHERE name='e2e 研发库'")[0][0]
    body1 = "# 部署\n\n第一步 **准备** 机器。\n\n- 装依赖\n- 起服务"
    r = http("/admin/wiki/save", cookie=cookie, data={
        "spaceId": sid, "title": "部署手册", "body": body1,
        "tags": "部署,运维", "note": "创建", "status": "1"})
    ok(code_of(r[1]) == "0000" or r[0] in (200, 301, 302), "doc created")
    did = sql("SELECT id FROM wiki_doc WHERE title='部署手册'")[0][0]
    ok(sql("SELECT rev FROM wiki_doc WHERE id=?", (did,))[0][0] == 1,
       "initial revision is 1")
    ok(sql("SELECT note FROM wiki_revision WHERE docId=? AND rev=1",
           (did,))[0][0] == "创建", "revision 1 snapshotted")
    http("/admin/wiki/save", cookie=cookie, data={
        "id": did, "spaceId": sid, "title": "部署手册",
        "body": "# 部署\n\n补充：回滚章节。\n\n- 装依赖",
        "tags": "部署,运维", "note": "补充回滚章节", "status": "1"})
    ok(sql("SELECT rev FROM wiki_doc WHERE id=?", (did,))[0][0] == 2 and
       sql("SELECT COUNT(*) FROM wiki_revision WHERE docId=?",
           (did,))[0][0] == 2, "edit bumps revision to 2")
    code, body, _ = http("/admin/wiki/page?id=%d" % did, cookie=cookie)
    ok(code == 200 and "<strong>准备</strong>" not in body
       and "<h3>" in body and "<li>装依赖</li>" in body,
       "current markdown rendered to html")
    code, body, _ = http("/admin/wiki/history?id=%d" % did, cookie=cookie)
    ok(code == 200 and "补充回滚章节" in body, "history lists revisions")
    code, body, _ = http("/admin/wiki/revision?id=%d&rev=1" % did,
                         cookie=cookie)
    ok(code == 200 and "<strong>准备</strong>" in body,
       "revision view renders old content")
    r = http("/admin/wiki/rollback", cookie=cookie,
             data={"id": did, "rev": "1"})
    ok(code_of(r[1]) == "0000" or r[0] in (200, 301, 302), "rollback ok")
    ok(sql("SELECT rev FROM wiki_doc WHERE id=?", (did,))[0][0] == 3 and
       sql("SELECT COUNT(*) FROM wiki_revision WHERE docId=?",
           (did,))[0][0] == 3, "rollback created revision 3")
    ok(sql("SELECT body FROM wiki_doc WHERE id=?", (did,))[0][0] == body1,
       "rolled-back body matches r1")
    r = http("/admin/wiki/aiorganize", cookie=cookie, data={"id": did})
    ok(code_of(r[1]) != "0000", "ai organize guarded when AI off")
    r = http("/admin/wiki/spacedelete", cookie=cookie, data={"id": sid})
    ok(code_of(r[1]) != "0000", "space with docs not deletable")
    r = http("/admin/wiki/save", data={
        "spaceId": sid, "title": "anon", "body": "x", "note": "",
        "status": "1"})
    ok(code_of(r[1]) != "0000", "wiki save rejects anonymous")
    http("/admin/wiki/delete", cookie=cookie, data={"id": did})
    ok(not sql("SELECT 1 FROM wiki_doc WHERE id=?", (did,)) and
       not sql("SELECT 1 FROM wiki_revision WHERE docId=?", (did,)),
       "doc delete cascades revisions")
    r = http("/admin/wiki/spacedelete", cookie=cookie, data={"id": sid})
    ok(code_of(r[1]) == "0000" or r[0] in (200, 301, 302),
       "empty space deletable after cleanup")

    # ---- coder: designer three-step ----
    r = http("/admin/dev/coder/tablesave", cookie=cookie, data={
        "tableName": "e2e_demo", "title": "e2e 演示"})
    ok(code_of(r[1]) == "0000" or r[0] in (200, 301, 302), "design saved")
    tid = sql("SELECT id FROM sys_gen_table WHERE tableName='e2e_demo'")[0][0]
    code, body, _ = http("/admin/dev/coder/design?id=%d" % tid, cookie=cookie)
    ok(code == 200 and "e2e_demo" in body, "design page renders")
    r = http("/admin/dev/coder/columnsave", cookie=cookie, data={
        "tableId": tid, "name": "qty", "label": "数量", "kind": "int",
        "widget": "number", "sortOrder": "10"})
    ok(code_of(r[1]) == "0000" or r[0] in (200, 301, 302), "column saved")
    col = sql("SELECT id FROM sys_gen_column WHERE tableId=? AND name='qty'",
              (tid,))
    ok(bool(col), "column row persisted")
    colId = col[0][0]
    body = http("/admin/dev/coder/design?id=%d" % tid, cookie=cookie)[1]
    ok('data-quick-field="label"' in body, "design table inline-editable")
    r = http("/admin/dev/coder/columnquick", cookie=cookie,
             data={"id": colId, "field": "kind", "value": "long"})
    ok(code_of(r[1]) == "0000", "column quick kind ok")
    ok(sql("SELECT kind FROM sys_gen_column WHERE id=?", (colId,))[0][0]
       == "long", "column kind persisted")
    r = http("/admin/dev/coder/columnquick", cookie=cookie,
             data={"id": colId, "field": "name", "value": "hijack"})
    ok(code_of(r[1]) != "0000", "column name not inline-editable")
    r = http("/admin/dev/coder/aidesign", cookie=cookie,
             data={"tableId": tid, "desc": "test"})
    ok(code_of(r[1]) != "0000", "aidesign guarded when AI off")
    r = http("/admin/dev/coder/migrate", cookie=cookie, data={"id": tid})
    ok(r[0] in (200, 301, 302), "migrate accepted")
    ok(table_exists("e2e_demo"), "migrate created table")
    cols = [row[1] for row in sql("PRAGMA table_info(e2e_demo)")]
    ok("qty" in cols, "migrated column present")
    # 数据管理页只认「已设计的表」：设计同步后即可管其数据。
    code, body, _ = http("/admin/monitor/data?t=e2e_demo", cookie=cookie)
    ok(code == 200 and "e2e_demo" in body, "generic data manager 200")
    code, body, _ = http("/admin/dev/coder/preview?id=%d" % tid, cookie=cookie)
    ok(code == 200 and "e2e_demo" in body, "preview generates code")
    ok("FrontControllerSource" not in body
       and "namespace ZanWeb.Front" in body
       and "FrontListViewSource" not in body,
       "preview contains front controller")
    # 生成代码：默认 gen.root=".."，沙箱里落到 templates/server/ 上一级，
    # 显式把 gen.root 指到 _scratch/genroot 再验证产物。
    r = http("/admin/system/settings/save", cookie=cookie,
             data={"gen.root": os.path.join(SANDBOX, "genroot")})
    ok(r[0] in (200, 301, 302), "gen.root saved")
    r = http("/admin/dev/coder/build", cookie=cookie, data={"id": tid})
    ok(code_of(r[1]) == "0000", "build writes sources")
    gr = os.path.join(SANDBOX, "genroot")
    gen_files = [
        os.path.join(gr, "src", "Model", "Gen", "E2eDemo.zan"),
        os.path.join(gr, "src", "Controller", "Admin", "E2eDemo.zan"),
        os.path.join(gr, "src", "Dao", "Gen", "E2eDemoDao.zan"),
        os.path.join(gr, "views", "Admin", "E2eDemo.Index.html"),
        os.path.join(gr, "views", "Admin", "E2eDemo.Form.html"),
        os.path.join(gr, "src", "Controller", "Front", "E2eDemoFront.zan"),
        os.path.join(gr, "views", "Front", "E2eDemoFront.Index.html"),
        os.path.join(gr, "views", "Front", "E2eDemoFront.Show.html"),
    ]
    ok(all(os.path.isfile(p) for p in gen_files), "all 8 generated files exist")
    fctl = open(gen_files[5], encoding="utf-8").read() if os.path.isfile(
        gen_files[5]) else ""
    ok("CustomAuthorization.None" in fctl and "/e2e_demo" in fctl.replace('\"', '"'),
       "front controller is public and routed")
    r = http("/admin/dev/coder/tabledelete", cookie=cookie, data={"id": tid})
    ok(r[0] in (200, 301, 302), "design deleted")
    ok(not sql("SELECT 1 FROM sys_gen_table WHERE id=?", (tid,)),
       "design row gone")

    # ---- monitor ----
    for path, label in [("/admin/monitor", "monitor index"),
                        ("/admin/monitor/sql", "monitor sql"),
                        ("/admin/monitor/history", "monitor history"),
                        ("/admin/system/docs", "api docs")]:
        code, _b, _c = http(path, cookie=cookie)
        ok(code == 200, label + " 200")
    r = http("/admin/monitor/alerts", cookie=cookie)
    d = {}
    try:
        d = json.loads(r[1])
    except Exception:
        pass
    ok("alerts" in d and "ts" in d, "alerts endpoint shape")

    # ---- i18n: language switch through site settings ----
    r = http("/admin/system/settings/save", cookie=cookie,
             data={"site.language": "en-US"})
    ok(r[0] in (200, 301, 302), "language set to en-US")
    code, body, _ = http("/admin/wiki", cookie=cookie)
    ok(code == 200 and "Knowledge Base" in body
       and "Data Dictionaries" in body and "{{i" not in body,
       "english shell renders translated menu")
    code, body, _ = http("/admin", cookie=cookie)
    ok(code == 200 and "Dashboard" in body, "dashboard heading translated")
    code, body, _ = http("/admin/system/jobs", cookie=cookie)
    ok(code == 200 and "Scheduled Jobs" in body and "Run Once" in body,
       "common actions translated")
    r = http("/admin/system/settings/save", cookie=cookie,
             data={"site.language": "xx-XX"})
    code, body, _ = http("/admin/wiki", cookie=cookie)
    ok(code == 200 and "知识库" in body, "unknown language falls back")
    r = http("/admin/system/settings/save", cookie=cookie,
             data={"site.language": "zh-CN"})
    code, body, _ = http("/admin/wiki", cookie=cookie)
    ok(code == 200 and "知识库" in body, "switch back to chinese works")

    # ---- seo: rss / sitemap / robots / tag archive / meta+og ----
    # 种子文章 1=「Zan 是什么」tags "zan,llvm,arc"，站点绝对链接未配置时
    # 按请求 Host 推导（沙箱端口可变，断言跟着 BASE 走）。
    code, body, _ = http("/rss.xml")
    ok(code == 200 and "<rss version=\"2.0\">" in body
       and "<title>Zan 是什么：熟悉的语法，原生的产物</title>" in body,
       "rss lists seed post")
    ok("<link>" + BASE + "/blog/1</link>" in body
       and "<guid isPermaLink=\"true\">" + BASE + "/blog/1</guid>" in body,
       "rss links are absolute")
    ok(re.search(r"<pubDate>\w{3}, \d{2} \w{3} \d{4} \d{2}:\d{2}:\d{2} [+-]\d{4}",
                 body), "rss pubDate is rfc822")
    st, ct = ctype("/rss.xml")
    ok(st == 200 and "application/rss+xml" in ct, "rss content type")
    code, body, _ = http("/sitemap.xml")
    ok(code == 200 and "<url><loc>" + BASE + "/</loc></url>" in body
       and "<loc>" + BASE + "/blog</loc>" in body
       and "<loc>" + BASE + "/blog/1</loc><lastmod>20" in body,
       "sitemap covers home, blog and posts")
    code, body, _ = http("/robots.txt")
    ok(code == 200 and "Disallow: /admin/" in body
       and body.startswith("User-agent: *")
       and "Sitemap: " + BASE + "/sitemap.xml" in body,
       "robots allows public, denies admin, points at sitemap")
    code, body, _ = http("/blog?tag=zan")
    ok(code == 200 and "标签「zan」下的文章" in body
       and "Zan 是什么" in body, "tag archive filters by whole tag")
    code, body, _ = http("/blog?tag=ar")
    ok(code == 200 and "还没有已发布的文章" in body,
       "tag filter rejects substring matches")
    code, body, _ = http("/blog/1")
    ok(code == 200
       and "property=\"og:title\" content=\"Zan 是什么" in body
       and "name=\"description\" content=\"C# 的写法" in body
       and "property=\"og:url\" content=\"" + BASE + "/blog/1\"" in body,
       "post page carries description and og meta")
    ok("href=\"/blog?tag=zan\"" in body
       and "rel=\"alternate\" type=\"application/rss+xml\"" in body,
       "post page links tags and feed")
    # site.url 配置接管绝对链接（Settings.Forget 后下一请求即生效）。
    r = http("/admin/system/settings/save", cookie=cookie,
             data={"site.url": "https://feeds.example.com"})
    ok(r[0] in (200, 301, 302), "site.url saved")
    code, body, _ = http("/robots.txt")
    ok("Sitemap: https://feeds.example.com/sitemap.xml" in body,
       "site.url drives robots sitemap")
    code, body, _ = http("/rss.xml")
    ok("<link>https://feeds.example.com/</link>" in body,
       "site.url drives rss links")
    http("/admin/system/settings/save", cookie=cookie, data={"site.url": ""})
    code, body, _ = http("/robots.txt")
    ok("Sitemap: " + BASE + "/sitemap.xml" in body,
       "site.url cleared falls back to request host")

    # ---- media: stream upload, serve, delete ----
    payload = b"\x89PNG\r\n\x1a\n" + b"e2e-media-bytes-0123456789" * 4
    upbody = raw_post("/admin/media/upload?name=e2e-logo.png", payload,
                      cookie=cookie)
    ok(code_of(upbody) == "0000", "media upload accepted")
    rows = sql("SELECT name, path, ext, size FROM media ORDER BY id DESC LIMIT 1")
    ok(bool(rows) and rows[0][0] == "e2e-logo.png" and rows[0][2] == "png",
       "media row recorded with clean name")
    mpath = rows[0][1] if rows else ""
    ok(bool(rows) and rows[0][3] == len(payload), "media size matches payload")
    disk = os.path.join(SANDBOX, "wwwroot", "media", mpath)
    ok(bool(mpath) and os.path.isfile(disk)
       and open(disk, "rb").read() == payload, "file stored byte-identical")
    gcode, got = raw_get("/static/media/" + mpath)
    ok(gcode == 200 and got == payload, "uploaded file served back")
    code, body, _ = http("/admin/media", cookie=cookie)
    ok(code == 200 and "e2e-logo.png" in body, "media list shows the file")
    ok(code_of(raw_post("/admin/media/upload?name=e2e-evil.exe", b"MZx",
                        cookie=cookie)) != "0000",
       "disallowed extension rejected")
    ok(sql("SELECT COUNT(*) FROM media")[0][0] == 1, "rejected upload stored nothing")
    spool_left = os.listdir(os.path.join(SANDBOX, "uploads"))
    ok(not spool_left, "rejected upload leaves no spool file")
    ok(code_of(raw_post("/admin/media/upload?name=x.png", b"x")) != "0000",
       "anonymous upload rejected")
    mid = sql("SELECT id FROM media ORDER BY id DESC LIMIT 1")[0][0]
    r = http("/admin/media/delete", cookie=cookie, data={"id": str(mid)})
    ok(code_of(r[1]) == "0000", "media delete ok")
    ok(not sql("SELECT 1 FROM media WHERE id=?", (mid,)), "media row gone")
    ok(not os.path.isfile(disk), "file removed from disk")

    # reset 会使旧会话失效，admin 会话断言放在邮件段之前。
    code, _b, _c = http("/admin", cookie=cookie)
    ok(code == 200, "admin alive before mail section")

    # ---- mail: password reset end to end ----
    r = http("/forgot/code", data={"email": "someone@example.com"})
    ok(code_of(r[1]) != "0000", "forgot-code guarded when mail off")
    # 验证码发给种子管理员：顺带验证「改密使旧会话与旧密码失效」。
    sql("UPDATE sys_user SET email=? WHERE id=(SELECT MIN(id) FROM sys_user)",
        ("e2e-reset@example.com",))
    r = http("/admin/system/settings/save", cookie=cookie, data={
        "mail.host": "127.0.0.1", "mail.port": str(MAIL_PORT),
        "mail.ssl": "0", "mail.from": "e2e@zanweb.test",
        "mail.fromName": "e2e"})
    ok(r[0] in (200, 301, 302), "mail settings saved")
    r = http("/forgot/code", data={"email": "e2e-reset@example.com"})
    ok(code_of(r[1]) == "0000", "forgot-code sent")
    time.sleep(0.5)
    mailbody = catcher.body()
    m = re.search(r"\b(\d{6})\b", mailbody)
    ok(bool(m), "reset mail delivered with 6-digit code")
    r = http("/forgot/code", data={"email": "e2e-reset@example.com"})
    ok(code_of(r[1]) != "0000", "resend within cooldown rejected")
    if m:
        code6 = m.group(1)
        wrong = "%06d" % ((int(code6) + 1) % 1000000)
        r = http("/forgot", data={"email": "e2e-reset@example.com",
                                  "code": wrong, "pass": "newpass-e2e-123"})
        ok("验证码不正确" in r[1], "wrong code rejected")
        r = http("/forgot", data={"email": "e2e-reset@example.com",
                                  "code": code6, "pass": "newpass-e2e-123"})
        ok("密码已重设" in r[1], "reset with mailed code succeeds")
        # 改密 bump tokenVersion；版本校验有 30s TTL 缓存（Auth.VerTtlSec，
        # 注释明说"最迟再有效 30 秒"），旧 token 最迟 30s 后被拒。
        ok(sql("SELECT tokenVersion FROM sys_user WHERE username='admin'"
               )[0][0] >= 1, "tokenVersion bumped in db")
        code, _b, setc = http("/admin/login",
                              {"user": "admin", "pass": "admin1234"})
        ok(not session_cookie(setc), "old password rejected")
        code, _b, setc = http("/admin/login",
                              {"user": "admin", "pass": "newpass-e2e-123"})
        ok(code in (301, 302) and session_cookie(setc),
           "login with new password works")
        print("[wait] 31s for the token-version cache to expire")
        time.sleep(31)
        code, _b, _c = http("/admin", cookie=cookie)
        ok(code in (301, 302), "old session invalidated after reset")
    else:
        ok(False, "mail body captured")
        ok(False, "wrong code rejected")
        ok(False, "reset with mailed code succeeds")
        ok(False, "old password rejected")
        ok(False, "login with new password works")
        ok(False, "old session invalidated after reset")


if __name__ == "__main__":
    main()

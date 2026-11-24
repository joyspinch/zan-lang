#!/usr/bin/env python3
"""通讯协议压测基线驱动（跨平台：Windows / macOS / Linux）。

编译 server.zan、pload.zan 与 ../web_mvc/load.zan → 生成临时自签证书 → 起服务端
（明文 + TLS 两个 WebApp）→ 跑协议矩阵 → 输出 JSON；--compare 与旧基线逐项比对，
吞吐跌幅或 p99 涨幅超过阈值即非零退出（同机比对才有意义）。

矩阵（不经 MVC，只量协议栈 + 路由 + 应答写出）：
  h1      明文 HTTP/1.1 keep-alive（裸套接字客户端 load.zan）  /hello ×{8,32}  /4k ×8
  https   HTTPS/1.1 keep-alive（HttpClient）                    /hello ×8
  h2      HTTP/2 over TLS（Http2ClientConnection）              /hello ×8 连接 ×{1,8} 并发流
  ws      WebSocket 文本回显 64 字节                            ×{8,32}

用法（仓库根目录）：
  python3 tests/perf/protocols/run_bench.py --out tests/perf/protocols/baselines/<机器>.json
  python3 tests/perf/protocols/run_bench.py --compare tests/perf/protocols/baselines/<机器>.json

TLS 场景需要 PATH 上有 openssl（生成一次性自签证书）；没有就跳过并提示。
不进 ctest：压测数字只在同一台机器上可比，且一轮要几分钟。
"""
import argparse
import datetime
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
HERE = os.path.join(ROOT, "tests", "perf", "protocols")
EXE = ".exe" if os.name == "nt" else ""
WORK = os.path.join(ROOT, "_scratch", "bench_protocols")

# (场景名, 协议, 端口种类, 路径, 连接数, 每连接并发流, ws 消息字节)
SCENARIOS = [
    ("h1-hello", "h1", "plain", "/hello", 8, 1, 0),
    ("h1-hello", "h1", "plain", "/hello", 32, 1, 0),
    ("h1-4k", "h1", "plain", "/4k", 8, 1, 0),
    ("https-hello", "https", "tls", "/hello", 8, 1, 0),
    ("h2-hello", "h2", "tls", "/hello", 8, 1, 0),
    ("h2-hello", "h2", "tls", "/hello", 8, 8, 0),
    ("ws-echo64", "ws", "plain", "/ws", 8, 1, 64),
    ("ws-echo64", "ws", "plain", "/ws", 32, 1, 64),
]


def zanc_path(arg):
    if arg:
        return arg
    return os.path.join(ROOT, "build", "zanc" + EXE)


def compile_all(zanc, outdir):
    os.makedirs(outdir, exist_ok=True)
    server = os.path.join(outdir, "bench_proto_server" + EXE)
    pload = os.path.join(outdir, "bench_proto_load" + EXE)
    h1load = os.path.join(outdir, "bench_web_mvc_load" + EXE)
    jobs = [
        ([zanc, os.path.join(HERE, "server.zan"), "--auto-stdlib", "-o", server], "server"),
        ([zanc, os.path.join(HERE, "pload.zan"), "--auto-stdlib", "-o", pload], "pload"),
        ([zanc, os.path.join(ROOT, "tests", "perf", "web_mvc", "load.zan"),
          "--auto-stdlib", "-o", h1load], "load"),
    ]
    for cmd, name in jobs:
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit("compile failed: " + name)
    return server, pload, h1load


def make_cert():
    """一次性自签证书（不进仓库）；没有 openssl 返回 None，TLS 场景跳过。"""
    openssl = shutil.which("openssl")
    if not openssl:
        return None
    os.makedirs(WORK, exist_ok=True)
    cert = os.path.join(WORK, "cert.pem")
    key = os.path.join(WORK, "key.pem")
    r = subprocess.run(
        [openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
         "-out", cert, "-days", "2", "-subj", "/CN=localhost"],
        capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        return None
    return cert, key


def port_open(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            return True
    except OSError:
        return False


def start_server(server, port, tls_port, cert, log_path):
    """输出写日志文件而非管道（管道写满会堵住服务端），就绪以端口可连为准
    （stdout 重定向到文件时运行时整块缓冲，BENCH_READY 可能迟迟不落盘）。"""
    os.makedirs(WORK, exist_ok=True)
    log = open(log_path, "w", encoding="utf-8")
    args = [server, "--port=%d" % port, "--tls-port=%d" % tls_port]
    if cert:
        args += ["--cert=" + cert[0], "--key=" + cert[1]]
    proc = subprocess.Popen(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            break
        if port_open(port) and (not cert or port_open(tls_port)):
            return proc, log
        time.sleep(0.2)
    proc.kill()
    raise SystemExit("server did not come up; see " + log_path)


def run_load(cmd, seconds, warmup, label):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                       timeout=seconds + warmup + 60)
    lines = [l for l in r.stdout.splitlines() if l.startswith("{")]
    if r.returncode != 0 or not lines:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("load generator failed on " + label)
    return json.loads(lines[-1])


def git_head():
    try:
        head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                              capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(
            ["git", "status", "--porcelain", "--", "src/runtime", "src/compiler",
             "packages", "stdlib", "tests/perf"],
            cwd=ROOT, capture_output=True, text=True).stdout.strip()
        return head + ("-dirty" if dirty else "")
    except OSError:
        return ""


def key_of(r):
    return (r["scenario"], r["conns"], r.get("streams", 1))


def compare(base, cur, rps_drop, p99_rise):
    by_key = {key_of(r): r for r in base["results"]}
    bad = 0
    print("%-12s %5s %3s %10s %10s %8s %10s %10s %8s" %
          ("scenario", "conns", "st", "rps(base)", "rps(now)", "Δrps", "p99(base)", "p99(now)", "Δp99"))
    for r in cur["results"]:
        b = by_key.get(key_of(r))
        if not b:
            continue
        d_rps = (r["rps"] - b["rps"]) / max(b["rps"], 1)
        d_p99 = (r["p99_us"] - b["p99_us"]) / max(b["p99_us"], 1)
        flag = ""
        if d_rps < -rps_drop or d_p99 > p99_rise:
            flag = "  REGRESSION"
            bad += 1
        print("%-12s %5d %3d %10d %10d %+7.1f%% %10d %10d %+7.1f%%%s" %
              (r["scenario"], r["conns"], r.get("streams", 1), b["rps"], r["rps"], d_rps * 100,
               b["p99_us"], r["p99_us"], d_p99 * 100, flag))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zanc", default="")
    ap.add_argument("--port", type=int, default=18490)
    ap.add_argument("--tls-port", type=int, default=18491)
    ap.add_argument("--seconds", type=int, default=10)
    ap.add_argument("--warmup", type=int, default=2)
    ap.add_argument("--only", default="", help="逗号分隔的场景名前缀，如 h2,ws")
    ap.add_argument("--out", default="")
    ap.add_argument("--compare", default="")
    ap.add_argument("--rps-drop", type=float, default=0.15, help="允许的吞吐跌幅（0.15 = 15%%）")
    ap.add_argument("--p99-rise", type=float, default=0.30, help="允许的 p99 涨幅")
    a = ap.parse_args()

    load_avg = None
    if hasattr(os, "getloadavg"):
        load_avg = round(os.getloadavg()[0], 2)
        if load_avg > (os.cpu_count() or 1) * 0.25:
            print("WARNING: machine busy (1-min load %.1f on %d cpus); numbers will be noisy"
                  % (load_avg, os.cpu_count() or 1))
    server, pload, h1load = compile_all(zanc_path(a.zanc), os.path.join(ROOT, "build", "perf"))
    cert = make_cert()
    if not cert:
        print("WARNING: openssl not found; skipping https/h2 scenarios")
    only = [p for p in a.only.split(",") if p]
    log_path = os.path.join(WORK, "server.log")
    proc, log = start_server(server, a.port, a.tls_port, cert, log_path)
    results = []
    try:
        for name, proto, kind, path, conns, streams, nbytes in SCENARIOS:
            if only and not any(name.startswith(p) for p in only):
                continue
            if kind == "tls" and not cert:
                continue
            port = a.tls_port if kind == "tls" else a.port
            common = ["--port=%d" % port, "--path=%s" % path, "--conns=%d" % conns,
                      "--seconds=%d" % a.seconds, "--warmup=%d" % a.warmup]
            if proto == "h1":
                cmd = [h1load] + common
            else:
                cmd = [pload, "--proto=" + proto, "--streams=%d" % streams,
                       "--bytes=%d" % nbytes] + common
            r = run_load(cmd, a.seconds, a.warmup, name)
            r["scenario"] = name
            r["streams"] = streams
            results.append(r)
            print("%-12s conns=%-3d streams=%-2d rps=%-7d p50=%-6dus p99=%-6dus errors=%d non200=%d stuck=%d" %
                  (name, conns, streams, r["rps"], r["p50_us"], r["p99_us"], r["errors"],
                   r["non200"], r.get("stuck", 0)))
            if proc.poll() is not None:
                raise SystemExit("server exited (code %s) during %s; see %s"
                                 % (proc.returncode, name, log_path))
    finally:
        proc.kill()
        proc.wait()
        log.close()

    doc = {
        "bench": "protocols",
        "commit": git_head(),
        "date": datetime.date.today().isoformat(),
        "machine": {
            "os": platform.system() + " " + platform.release(),
            "arch": platform.machine(),
            "cpu": platform.processor(),
            "cpus": os.cpu_count(),
            "load_avg_1m": load_avg,
        },
        "params": {"seconds": a.seconds, "warmup": a.warmup,
                   "server": "single process: plain WebApp + TLS WebApp, no MVC"},
        "results": results,
    }
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(doc, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print("wrote " + a.out)
    failed = any(r["errors"] or r["non200"] or r.get("stuck", 0) for r in results)
    if a.compare:
        with open(a.compare, encoding="utf-8") as f:
            base = json.load(f)
        bad = compare(base, doc, a.rps_drop, a.p99_rise)
        if bad:
            print("%d scenario(s) regressed beyond threshold" % bad)
            failed = True
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

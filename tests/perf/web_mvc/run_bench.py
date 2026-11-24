#!/usr/bin/env python3
"""Web MVC 压测基线驱动（跨平台：Windows / macOS / Linux）。

编译 server.zan 与 load.zan → 起服务端 → 跑 {纯模板页, 后台列表页} ×
{并发 = 池大小 ×1, ×4} 矩阵 → 输出 JSON；给了 --compare 时与旧基线逐项比对，
吞吐跌幅或 p99 涨幅超过阈值即非零退出（同机比对才有意义）。

用法（仓库根目录）：
  python3 tests/perf/web_mvc/run_bench.py --out tests/perf/web_mvc/baselines/<机器>.json
  python3 tests/perf/web_mvc/run_bench.py --compare tests/perf/web_mvc/baselines/<机器>.json

不进 ctest：压测数字只在同一台机器上可比，且一轮要几分钟。
"""
import argparse
import datetime
import json
import os
import platform
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
HERE = os.path.join(ROOT, "tests", "perf", "web_mvc")
EXE = ".exe" if os.name == "nt" else ""

SCENARIOS = [
    ("plain", "/bench/plain"),
    ("list", "/bench/list?page=3"),
]


def zanc_path(arg):
    if arg:
        return arg
    return os.path.join(ROOT, "build", "zanc" + EXE)


def mvc_sources():
    out = []
    base = os.path.join(ROOT, "packages", "Zan.Mvc", "src")
    for d, _, files in os.walk(base):
        for f in files:
            if f.endswith(".zan"):
                out.append(os.path.join(d, f))
    return sorted(out)


def compile_all(zanc, outdir):
    os.makedirs(outdir, exist_ok=True)
    server = os.path.join(outdir, "bench_web_mvc_server" + EXE)
    load = os.path.join(outdir, "bench_web_mvc_load" + EXE)
    jobs = [
        ([zanc, os.path.join(HERE, "server.zan")] + mvc_sources() + ["--auto-stdlib", "-o", server], "server"),
        ([zanc, os.path.join(HERE, "load.zan"), "--auto-stdlib", "-o", load], "load"),
    ]
    for cmd, name in jobs:
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit("compile failed: " + name)
    return server, load


def start_server(server, port, pool, log_path):
    """服务端输出写日志文件而非管道：管道没人读时写满会把服务端堵住，
    整轮表现为成片连接失败；日志也留作失败现场。"""
    os.makedirs(os.path.join(ROOT, "_scratch"), exist_ok=True)
    log = open(log_path, "w", encoding="utf-8")
    proc = subprocess.Popen(
        [server, "--port=%d" % port, "--pool=%d" % pool,
         "--db=_scratch/bench_web_mvc.db", "--views=tests/perf/web_mvc/views"],
        cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    deadline = time.time() + 60
    while time.time() < deadline:
        if proc.poll() is not None:
            break
        with open(log_path, encoding="utf-8", errors="replace") as f:
            if "BENCH_READY" in f.read():
                return proc, log
        time.sleep(0.2)
    proc.kill()
    raise SystemExit("server did not report BENCH_READY; see " + log_path)


def run_load(load, port, path, conns, seconds, warmup):
    r = subprocess.run(
        [load, "--port=%d" % port, "--path=%s" % path, "--conns=%d" % conns,
         "--seconds=%d" % seconds, "--warmup=%d" % warmup],
        cwd=ROOT, capture_output=True, text=True, timeout=seconds + warmup + 60)
    lines = [l for l in r.stdout.splitlines() if l.startswith("{")]
    if r.returncode != 0 or not lines:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("load generator failed on " + path)
    return json.loads(lines[-1])


def git_head():
    """HEAD 短哈希；影响数字的源码（运行时/编译器/包/标准库/本基准）有未提交
    改动时加 -dirty——基线必须能追溯到真正被测的代码。"""
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


def compare(base, cur, rps_drop, p99_rise):
    by_key = {(r["scenario"], r["conns"]): r for r in base["results"]}
    bad = 0
    print("%-8s %5s %10s %10s %8s %10s %10s %8s" %
          ("scenario", "conns", "rps(base)", "rps(now)", "Δrps", "p99(base)", "p99(now)", "Δp99"))
    for r in cur["results"]:
        b = by_key.get((r["scenario"], r["conns"]))
        if not b:
            continue
        d_rps = (r["rps"] - b["rps"]) / max(b["rps"], 1)
        d_p99 = (r["p99_us"] - b["p99_us"]) / max(b["p99_us"], 1)
        flag = ""
        if d_rps < -rps_drop or d_p99 > p99_rise:
            flag = "  REGRESSION"
            bad += 1
        print("%-8s %5d %10d %10d %+7.1f%% %10d %10d %+7.1f%%%s" %
              (r["scenario"], r["conns"], b["rps"], r["rps"], d_rps * 100,
               b["p99_us"], r["p99_us"], d_p99 * 100, flag))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zanc", default="")
    ap.add_argument("--port", type=int, default=18480)
    ap.add_argument("--pool", type=int, default=8)
    ap.add_argument("--seconds", type=int, default=10)
    ap.add_argument("--warmup", type=int, default=2)
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
    server, load = compile_all(zanc_path(a.zanc), os.path.join(ROOT, "build", "perf"))
    log_path = os.path.join(ROOT, "_scratch", "bench_web_mvc_server.log")
    proc, log = start_server(server, a.port, a.pool, log_path)
    results = []
    try:
        for name, path in SCENARIOS:
            for mult in (1, 4):
                conns = a.pool * mult
                r = run_load(load, a.port, path, conns, a.seconds, a.warmup)
                r["scenario"] = name
                results.append(r)
                print("%-6s conns=%-3d rps=%-7d p50=%-6dus p99=%-6dus errors=%d non200=%d stuck=%d" %
                      (name, conns, r["rps"], r["p50_us"], r["p99_us"], r["errors"],
                       r["non200"], r.get("stuck", 0)))
            if proc.poll() is not None:
                raise SystemExit("server exited during %s; see %s" % (name, log_path))
    finally:
        proc.kill()
        proc.wait()
        log.close()

    doc = {
        "bench": "web_mvc",
        "commit": git_head(),
        "date": datetime.date.today().isoformat(),
        "machine": {
            "os": platform.system() + " " + platform.release(),
            "arch": platform.machine(),
            "cpu": platform.processor(),
            "cpus": os.cpu_count(),
            "load_avg_1m": load_avg,
        },
        "params": {"pool": a.pool, "seconds": a.seconds, "warmup": a.warmup,
                   "server": "single WebApp process, SQLite pool"},
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

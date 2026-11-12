# Parallel driver for the self-host differential gate (tests/run_selfhost_diff.cmake).
#
# The per-case loop is embarrassingly parallel -- each case is an independent
# gen1 emit + clang link + run + stdout compare -- but CMake script mode has no
# job control, so the cmake harness used to walk all 231 cases sequentially
# (~17 min, dominated by 230 clang links). This driver keeps the exact same
# semantics (emit -> link -> run -> normalized stdout compare; gaps reported
# not failed; rejects fail-closed with pinned diagnostics, optionally cross-
# checked against the C host's wording) and runs the cases on a thread pool.
#
# The cmake harness stays the entry point: it builds gen1, then hands the case
# list and tool paths over here and fails if the exit code is non-zero. The
# final summary line is byte-identical to the sequential loop's.

import argparse
import os
import shutil
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor

_print_lock = threading.Lock()


def log(msg):
    with _print_lock:
        print(msg, flush=True)


def run_cmd(cmd, workdir, timeout=0):
    """Run a subprocess, capturing output the way execute_process does.
    A timeout kills the process: unvetted corpus cases can miscompile into
    a spin or a memory hog, and one runaway must not take the pool down."""
    try:
        p = subprocess.run(cmd, cwd=workdir, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout or None)
        return p.returncode, p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace")
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace")
        err = (e.stderr or b"").decode(errors="replace")
        return -9, out, err
    except OSError as e:
        return -1, "", str(e)


def normalize(out):
    """The golden compare: CRLF -> LF, trailing whitespace stripped."""
    out = out.replace("\r\n", "\n")
    return out.rstrip(" \t\r\n")


def read_golden(path):
    with open(path, "rb") as f:
        return f.read().decode(errors="replace")





def run_case(case, a):
    src = os.path.join(a.case_dir, case + ".zan")
    if not os.path.exists(src):
        return ("fatal", "case source not found: %s" % src)
    ll = os.path.join(a.outdir, case + ".ll")
    exe = os.path.join(a.outdir, case + ".exe")
    for f in (ll, exe):
        if os.path.exists(f):
            os.remove(f)

    why = ""
    helper = os.path.join(a.case_dir, "helpers", case + ".zan")
    extra_args = [helper] if os.path.exists(helper) else []
    rc, out, err = run_cmd([a.gen1, ll, src] + extra_args, a.workdir, a.emit_timeout)
    if rc == -9:
        why = "gen1 emit timed out after %ds (miscompile spin?)" % a.emit_timeout
    elif rc != 0:
        why = "gen1 failed to compile it (rc=%d): %s%s" % (rc, out, err)
    elif a.clang and a.rtobjs:
        rc, out, err = run_cmd([a.clang, ll] + a.rtobjs + a.syslibs + ["-o", exe], a.workdir, a.link_timeout)
        if rc == -9:
            why = "clang link timed out after %ds" % a.link_timeout
        elif rc != 0:
            why = "clang rejected the emitted IR (rc=%d): %s%s" % (rc, out, err)
    if why == "":
        rc, out, err = run_cmd([exe], a.workdir, a.run_timeout)
        if rc == -9:
            why = "the program timed out after %ds (spin or hang)" % a.run_timeout
        elif rc != 0:
            why = "the program exited with %d: %s%s" % (rc, out, err)
        elif os.path.exists(os.path.join(a.case_dir, case + ".out")):
            exp = normalize(read_golden(os.path.join(a.case_dir, case + ".out")))
            act = normalize(out)
            if act != exp:
                why = ("stdout differs from the C host's golden output\n"
                       "--- expected ---\n%s\n--- actual ---\n%s" % (exp, act))
    return (case, why)


def run_reject(name, text, hosttext, a):
    src = os.path.join(a.reject_dir, name + ".zan")
    if not os.path.exists(src):
        return ("fatal", "reject case source not found: %s" % src)
    extra = os.path.join(a.reject_dir, name + "_helper.zan")
    extra_args = [extra] if os.path.exists(extra) else []
    ll = os.path.join(a.outdir, "reject_%s.ll" % name)
    if os.path.exists(ll):
        os.remove(ll)

    rc, out, err = run_cmd([a.gen1, ll, src] + extra_args, a.workdir, a.emit_timeout)
    if rc < 0 or rc > 125:
        return ("reject", name, "FAIL",
                "the self-hosted compiler timed out, crashed, or failed to launch (rc=%d)" % rc)
    if rc == 0:
        return ("reject", name, "FAIL", "the self-hosted compiler accepted a program it must reject")
    if os.path.exists(ll):
        return ("reject", name, "FAIL", "the self-hosted compiler wrote IR for a program it must reject")
    if text != "" and text not in (out + err):
        return ("reject", name, "FAIL",
                "the diagnostic does not contain '%s' -- refused for another reason" % text)
    log("-- REJECT %s: refused as expected" % name)

    if hosttext != "":
        hexe = os.path.join(a.outdir, "reject_%s_host.exe" % name)
        if os.path.exists(hexe):
            os.remove(hexe)
        rc, out, err = run_cmd([a.zanc, src] + extra_args + ["--auto-stdlib", "-o", hexe], a.workdir, a.emit_timeout)
        if rc < 0 or rc > 125:
            return ("reject", name, "FAIL",
                    "the C host timed out, crashed, or failed to launch (rc=%d)" % rc)
        if rc == 0:
            return ("reject", name, "FAIL",
                    "the C host accepted the program the self-host must reject")
        if os.path.exists(hexe):
            return ("reject", name, "FAIL",
                    "the C host wrote an executable for a program it must reject")
        if hosttext not in (out + err):
            return ("reject", name, "FAIL",
                    "the C host's diagnostic does not contain '%s' -- the two compilers do not agree on the wording" % hosttext)
        log("-- REJECT %s: the C host refuses it with the same wording" % name)
    return ("reject", name, "OK", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zanc", required=True)
    ap.add_argument("--gen1", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--case-dir", required=True)
    ap.add_argument("--cases", required=True)
    ap.add_argument("--clang", default="")
    ap.add_argument("--rtobjs", nargs="*", default=[])
    ap.add_argument("--gaps", default="")
    ap.add_argument("--rejects", default="")
    ap.add_argument("--reject-dir", default="")
    ap.add_argument("--workdir", default=os.getcwd())
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--emit-timeout", type=int, default=120)
    ap.add_argument("--link-timeout", type=int, default=180)
    ap.add_argument("--run-timeout", type=int, default=5)
    a = ap.parse_args()

    os.makedirs(a.outdir, exist_ok=True)
    jobs = a.jobs if a.jobs > 0 else (os.cpu_count() or 8)
    # the same link set the cmake gate resolves by platform: the system
    # libraries are fixed per OS, so they are derived here rather than
    # threaded through the cmake->python argument boundary
    if sys.platform == "win32":
        a.syslibs = ["-lkernel32", "-lshell32", "-luser32", "-lgdi32", "-ladvapi32",
                     "-lversion", "-lws2_32", "-lbcrypt", "-lpsapi", "-lsetupapi",
                     "-liphlpapi", "-lwinspool", "-lntdll", "-lole32"]
        sqlite_dir = os.path.join(a.workdir, "packages", "Zan.Data", "src", "System", "Data", "Sqlite", "drivers", "win-x64")
        sqlite_lib = os.path.join(sqlite_dir, "libsqlite3.dll.a")
        if os.path.exists(sqlite_lib):
            a.syslibs.append(sqlite_lib)
            sqlite_dll = os.path.join(sqlite_dir, "libsqlite3-0.dll")
            if os.path.exists(sqlite_dll):
                shutil.copy2(sqlite_dll, a.outdir)
        for dlib, ddll in [("zan_audio.lib", "zan_audio.dll"),
                           ("zan_image.lib", "zan_image.dll"),
                           ("zan_gui.lib", "zan_gui.dll")]:
            lp = os.path.join(a.workdir, "build", dlib)
            if os.path.exists(lp):
                a.syslibs.append(lp)
            dp = os.path.join(a.workdir, "build", ddll)
            if os.path.exists(dp):
                shutil.copy2(dp, a.outdir)
    else:
        a.syslibs = ["-ldl", "-lpthread", "-lm"]

    # ---- case list / gaps / rejects: same comment+blank filtering ----------
    def parse_lines(path, split_gaps=False):
        out = []
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for raw in f:
                s = raw.strip()
                if s == "" or s.startswith("#"):
                    continue
                if split_gaps:
                    sep = s.find(" --")
                    if sep != -1:
                        s = s[:sep].strip()
                out.append(s)
        return out

    cases = parse_lines(a.cases)
    gaps = set(parse_lines(a.gaps, split_gaps=True)) if a.gaps else set()

    rejects = []  # (name, self_substring, host_substring)
    if a.rejects:
        for line in parse_lines(a.rejects):
            parts = line.split("|||")
            name = parts[0]
            self_sub = parts[1] if len(parts) > 1 else ""
            host_sub = parts[2] if len(parts) > 2 else ""
            rejects.append((name, self_sub, host_sub))

    failures = 0
    gap_hits = 0
    passed = 0
    rejected = 0

    # ---- the differential cases, on the pool -------------------------------
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for res in pool.map(lambda c: run_case(c, a), cases):
            if res[0] == "fatal":
                print(res[1], file=sys.stderr, flush=True)
                sys.exit(1)
            case, why = res
            if why == "":
                passed += 1
                log("-- PASS %s" % case)
                if case in gaps:
                    log("WARNING: known gap '%s' now passes: delete it from %s" % (case, a.gaps))
            elif case in gaps:
                gap_hits += 1
                log("-- GAP  %s: %s" % (case, why))
            else:
                failures += 1
                log("-- FAIL %s: %s" % (case, why))

    # ---- the reject cases, on the pool --------------------------------------
    if rejects:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for res in pool.map(lambda r: run_reject(r[0], r[1], r[2], a), rejects):
                kind, name, status, msg = res
                if status == "FAIL":
                    failures += 1
                    log("-- FAIL reject %s: %s" % (name, msg))
                else:
                    rejected += 1

    log("-- selfhost differential: %d passed, %d known gaps, %d rejected, %d failures"
        % (passed, gap_hits, rejected, failures))
    if failures > 0:
        log("ERROR: selfhost differential gate: %d case(s) disagree with the C host" % failures)
        sys.exit(1)


if __name__ == "__main__":
    main()

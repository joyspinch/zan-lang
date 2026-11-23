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
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor

_print_lock = threading.Lock()


def log(msg):
    with _print_lock:
        print(msg, flush=True)


def capture_version(exe, workdir):
    """First line of `<exe> --version`, or '' when the binary predates the
    flag or cannot run: the stamp degrades to empty, it never fabricates
    identity (B-ID189 -- the report must be attributable)."""
    try:
        rc, out, _ = run_cmd([exe, "--version"], workdir, timeout=60)
    except Exception:
        return ""
    if rc != 0 or not out:
        return ""
    return out.splitlines()[0].strip()


def version_token(v):
    """The X.Y.Z out of 'zanc X.Y.Z (genN)', or '' for an unstamped line."""
    parts = v.split()
    return parts[1] if len(parts) >= 2 else ""


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


def stage(a, name, command, timeout):
    rc, out, err = run_cmd(command, a.workdir, timeout)
    if a.log_dir:
        for suffix, text in [("exit", str(rc) + "\n"), ("stdout", out),
                             ("stderr", err), ("command.json", json.dumps(command))]:
            with open(os.path.join(a.log_dir, name + "." + suffix), "w", encoding="utf-8") as f:
                f.write(text)
    return rc, out, err


def emit_error(out, err):
    text = re.sub(r"\x1b\[[0-9;]*m", "", out + "\n" + err)
    return re.search(r"\berror:|[1-9][0-9]* error\(s\)", text) is not None


def check_output(exe, golden, name, a):
    rc, out, err = stage(a, name + ".run", [exe], a.run_timeout)
    if rc != 0:
        return "program exit %d (expected 0): %s%s" % (rc, out, err)
    expected = normalize(read_golden(golden))
    if normalize(out) != expected or err:
        return "golden stdout or empty stderr mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n--- stderr ---\n%s" % (expected, out, err)
    return ""


def run_focused_case(case, a):
    src = os.path.join(a.case_dir, case + ".zan")
    golden = os.path.join(a.case_dir, case + ".out")
    ll = os.path.join(a.outdir, case + ".ll")
    host_exe = os.path.join(a.outdir, case + ".host.exe")
    gen_exe = os.path.join(a.outdir, case + ".gen1.exe")
    bc = os.path.join(a.outdir, case + ".bc")
    for path in (ll, host_exe, gen_exe, bc):
        if os.path.exists(path):
            os.remove(path)
    helper = os.path.join(a.case_dir, "helpers", case + ".zan")
    extras = [helper] if os.path.exists(helper) else []
    failures = []
    rc, out, err = stage(a, case + ".host.compile",
                         [a.zanc, src] + extras + ["--auto-stdlib", "--package-project", a.workdir, "-o", host_exe], a.emit_timeout)
    if rc != 0 or emit_error(out, err) or not os.path.isfile(host_exe):
        failures.append("host compile failed (rc=%d): %s%s" % (rc, out, err))
    else:
        why = check_output(host_exe, golden, case + ".host", a)
        if why:
            failures.append("host " + why)
    rc, out, err = stage(a, case + ".gen1.emit", [a.gen1, ll, src] + extras, a.emit_timeout)
    if rc != 0 or emit_error(out, err) or not os.path.isfile(ll):
        failures.append("gen1 emit failed (rc=%d): %s%s" % (rc, out, err))
        return case, "\n".join(failures)
    if a.llvm_as:
        rc, out, err = stage(a, case + ".gen1.llvm-as", [a.llvm_as, ll, "-o", bc], a.link_timeout)
        if rc != 0 or not os.path.isfile(bc):
            failures.append("llvm-as rejected IR (rc=%d): %s%s" % (rc, out, err))
            return case, "\n".join(failures)
    rc, out, err = stage(a, case + ".gen1.link", [a.clang, ll] + a.rtobjs + a.syslibs + ["-o", gen_exe], a.link_timeout)
    if rc != 0 or not os.path.isfile(gen_exe):
        failures.append("gen1 link failed (rc=%d): %s%s" % (rc, out, err))
    else:
        why = check_output(gen_exe, golden, case + ".gen1", a)
        if why:
            failures.append("gen1 " + why)
    return case, "\n".join(failures)


def run_case(case, a):
    if a.focus:
        return run_focused_case(case, a)
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


def run_focused_reject(name, text, hosttext, count, a):
    src = os.path.join(a.reject_dir, name + ".zan")
    helper = os.path.join(a.reject_dir, name + "_helper.zan")
    extras = [helper] if os.path.isfile(helper) else []
    failures = []
    for compiler in ("host", "gen1"):
        output = os.path.join(a.outdir, "reject_%s.%s" % (name, "host.exe" if compiler == "host" else "ll"))
        if os.path.exists(output):
            os.remove(output)
        command = ([a.zanc, src] + extras + ["--auto-stdlib", "--package-project", a.workdir, "-o", output]
                   if compiler == "host" else [a.gen1, output, src] + extras)
        rc, out, err = stage(a, name + "." + compiler + ".reject", command, a.emit_timeout)
        diagnostic = re.sub(r"\x1b\[[0-9;]*m", "", out + "\n" + err)
        expected = hosttext if compiler == "host" else text
        if rc < 1 or rc > 125:
            failures.append("%s must reject normally, got exit %d" % (compiler, rc))
        if os.path.exists(output):
            failures.append("%s wrote output for rejected input" % compiler)
        if expected not in diagnostic:
            failures.append("%s diagnostic does not contain '%s'" % (compiler, expected))
        actual_count = len(re.findall(r"^.*:\d+:\d+:\s*error:", diagnostic, re.MULTILINE))
        if actual_count != count:
            failures.append("%s emitted %d diagnostic(s), expected %d" % (compiler, actual_count, count))
    if failures:
        return "reject", name, "FAIL", "\n".join(failures)
    log("-- REJECT %s: host/gen1 refused with matching diagnostic and count %d" % (name, count))
    return "reject", name, "OK", ""


def run_reject(name, text, hosttext, a):
    if a.focus:
        return run_focused_reject(name, text, hosttext, a.reject_counts[name], a)
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
    ap.add_argument("--cases", default="")
    ap.add_argument("--focus", default="", help="manifest: case, or !reject|||self diagnostic|||host diagnostic|||count")
    ap.add_argument("--only", nargs="+", default=[], help="exact names from the focus manifest")
    ap.add_argument("--log-dir", default="")
    ap.add_argument("--llvm-as", default="")
    ap.add_argument("--stdlib-stamp", default="")
    ap.add_argument("--clang", default="")
    ap.add_argument("--rtobjs", nargs="*", default=[])
    ap.add_argument("--gaps", default="")
    ap.add_argument("--rejects", default="")
    ap.add_argument("--reject-dir", default="")
    ap.add_argument("--workdir", default=os.getcwd())
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--emit-timeout", type=int, default=120)
    ap.add_argument("--link-timeout", type=int, default=180)
    ap.add_argument("--run-timeout", type=int, default=30)
    a = ap.parse_args()

    if not a.focus and not a.cases:
        ap.error("--cases is required unless --focus is supplied")
    if a.only and not a.focus:
        ap.error("--only requires --focus")
    if a.focus and (not a.clang or not a.rtobjs or not a.log_dir):
        ap.error("--focus requires --clang, --rtobjs and --log-dir (existing tools only)")
    os.makedirs(a.outdir, exist_ok=True)
    if a.log_dir:
        os.makedirs(a.log_dir, exist_ok=True)
        passed_file = os.path.join(a.log_dir, "passed.txt")
        if os.path.exists(passed_file):
            os.remove(passed_file)
    jobs = a.jobs if a.jobs > 0 else min(os.cpu_count() or 8, 8)

    # ---- report identity stamp (B-ID189) ------------------------------------
    # A differential report is only worth anything if a later session can
    # reproduce it: both compilers' --version lines and the SHA256 of the
    # selfhost source manifest pin which binaries and which source set ran.
    # The gen1 driver keeps its version literal synced with the VERSION file
    # via scripts/bump_version.ps1; when the two drift apart the report would
    # pair mismatched identities, so the gate refuses to run at all.
    stamp_zanc = capture_version(a.zanc, a.workdir)
    stamp_gen1 = capture_version(a.gen1, a.workdir)
    stamp_sources = ""
    manifest = os.path.join(a.workdir, "scripts", "selfhost_sources.txt")
    if os.path.isfile(manifest):
        with open(manifest, "rb") as f:
            stamp_sources = hashlib.sha256(f.read()).hexdigest()
    log("-- selfhost differential stamp: zanc='%s' gen1='%s' sources=%s"
        % (stamp_zanc, stamp_gen1, stamp_sources[:16]))
    ztok, gtok = version_token(stamp_zanc), version_token(stamp_gen1)
    if ztok and gtok and ztok != gtok:
        log("ERROR: gen1 version drift: gen1 reports '%s', zanc reports '%s'; "
            "sync src/selfhost/driver/main.zan (scripts/bump_version.ps1)"
            % (stamp_gen1, stamp_zanc))
        sys.exit(1)
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
            if not a.focus and os.path.exists(sqlite_dll):
                shutil.copy2(sqlite_dll, a.outdir)
        for dlib, ddll in [("zan_audio.lib", "zan_audio.dll"),
                           ("zan_image.lib", "zan_image.dll"),
                           ("zan_gui.lib", "zan_gui.dll")]:
            lp = os.path.join(a.workdir, "build", dlib)
            if os.path.exists(lp):
                a.syslibs.append(lp)
            dp = os.path.join(a.workdir, "build", ddll)
            if not a.focus and os.path.exists(dp):
                shutil.copy2(dp, a.outdir)
        if a.focus:
            os.environ["PATH"] = os.pathsep.join([os.path.join(a.workdir, "build"), sqlite_dir, os.environ.get("PATH", "")])
    else:
        a.syslibs = ["-ldl", "-lpthread", "-lm"]
    if a.focus:
        os.environ["ZAN_STDLIB_PATH"] = os.path.join(a.workdir, "stdlib")
        a.llvm_as = a.llvm_as or shutil.which("llvm-as") or ""
        if not a.llvm_as:
            log("-- llvm-as not available: assembly verification skipped (clang still verifies IR)")

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

    cases = parse_lines(a.cases) if a.cases else []
    gaps = set(parse_lines(a.gaps, split_gaps=True)) if a.gaps else set()

    rejects = []  # (name, self_substring, host_substring)
    if a.rejects:
        for line in parse_lines(a.rejects):
            parts = line.split("|||")
            name = parts[0]
            self_sub = parts[1] if len(parts) > 1 else ""
            host_sub = parts[2] if len(parts) > 2 else ""
            rejects.append((name, self_sub, host_sub))
    if a.focus:
        cases, rejects, gaps, a.reject_counts = [], [], set(), {}
        names = set()
        for line in parse_lines(a.focus):
            parts = line.split("|||")
            reject = parts[0].startswith("!")
            name = parts[0][1:] if reject else parts[0]
            if not re.fullmatch(r"[A-Za-z0-9_-]+", name) or name in names:
                ap.error("invalid or duplicate focus case: " + name)
            names.add(name)
            if reject:
                if len(parts) != 4 or not parts[1] or not parts[2] or not parts[3].isdigit() or int(parts[3]) < 1:
                    ap.error("focus reject requires !name|||self diagnostic|||host diagnostic|||positive count")
                a.reject_counts[name] = int(parts[3])
                rejects.append((name, parts[1], parts[2]))
            elif len(parts) == 1:
                cases.append(name)
            else:
                ap.error("focus positive must be a case name: " + name)
        if a.only:
            if set(a.only) - names:
                ap.error("unknown focus case(s): " + ", ".join(sorted(set(a.only) - names)))
            cases = [name for name in cases if name in a.only]
            rejects = [r for r in rejects if r[0] in a.only]
        if not cases and not rejects:
            ap.error("focus manifest selects no cases")
        inputs = [a.zanc, a.gen1, a.clang, a.focus, os.path.abspath(__file__),
                  os.path.join(a.workdir, "tests", "run_selfhost_diff.cmake")] + a.rtobjs
        inputs += [p for p in a.syslibs if os.path.isfile(p)]
        inputs += [p for p in [a.llvm_as, a.stdlib_stamp] if p]
        for case in cases:
            inputs += [os.path.join(a.case_dir, case + ext) for ext in (".zan", ".out")]
            helper = os.path.join(a.case_dir, "helpers", case + ".zan")
            if os.path.isfile(helper):
                inputs.append(helper)
        for name, _, _ in rejects:
            inputs.append(os.path.join(a.reject_dir, name + ".zan"))
            helper = os.path.join(a.reject_dir, name + "_helper.zan")
            if os.path.isfile(helper):
                inputs.append(helper)
        def fingerprint():
            records = []
            for path in dict.fromkeys(inputs):
                with open(path, "rb") as f:
                    digest = hashlib.sha256(f.read()).hexdigest()
                records.append({"path": path, "sha256": digest, "mtime_ns": os.stat(path).st_mtime_ns})
            return records
        try:
            before = fingerprint()
        except OSError as e:
            ap.error("missing focus input: " + str(e))
        with open(os.path.join(a.log_dir, "inputs.json"), "w", encoding="utf-8") as f:
            json.dump(before, f, indent=2)
        log("-- focused selfhost differential: %d positive, %d negative; existing compilers only" % (len(cases), len(rejects)))

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

    if a.focus:
        try:
            after = fingerprint()
        except OSError:
            after = []
        with open(os.path.join(a.log_dir, "inputs.after.json"), "w", encoding="utf-8") as f:
            json.dump(after, f, indent=2)
        if before != after:
            failures += 1
            log("-- FAIL inputs changed during validation; see inputs.json/inputs.after.json")
        with open(os.path.join(a.log_dir, "result.json"), "w", encoding="utf-8") as f:
            json.dump({"passed": passed, "rejected": rejected, "failures": failures,
                       "inputs_stable": before == after, "llvm_as": a.llvm_as,
                       "zanc_version": stamp_zanc, "gen1_version": stamp_gen1,
                       "sources_sha256": stamp_sources}, f, indent=2)
        if failures == 0:
            with open(os.path.join(a.log_dir, "passed.txt"), "w", encoding="utf-8") as f:
                f.write("%d host/gen1 golden+exit pairs, %d diagnostic rejects; inputs stable.\n" % (passed, rejected))
    log("-- selfhost differential: %d passed, %d known gaps, %d rejected, %d failures"
        % (passed, gap_hits, rejected, failures))
    if failures > 0:
        log("ERROR: selfhost differential gate: %d case(s) disagree with the C host" % failures)
        sys.exit(1)


if __name__ == "__main__":
    main()

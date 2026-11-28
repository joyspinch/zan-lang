#!/usr/bin/env python3
"""Cross-platform conformance consistency matrix & link-time capability gate (S33, B-ID226).

Modes:
  --check-matrix
      Verify tests/cross_platform_matrix.txt has no duplicates, every case has
      .zan and .out on disk, and every conformance test anchored by
      [rule:SPEC-*] in docs/SPEC.md is included in the matrix.

  --run --zanc <zanc> --out <manifest.json> [--jobs N] [--skip-link-probe]
      Compile and execute every case in tests/cross_platform_matrix.txt, assert
      exit code 0 and exact LF-normalized byte parity with its .out golden, and
      run the S33 link-time fail-closed capability probe (asserting an
      unresolved platform [DllImport] fails at link time rather than degrading
      silently). Writes a deterministic JSON manifest with per-case SHA-256
      digests.

  --compare [--require-os linux,macos,windows] <manifest1.json> <manifest2.json> [...]
      Compare platform manifests from multiple OS runners and fail closed on
      any missing OS, missing case, failed link-time probe, non-zero exit code,
      or single-byte stdout SHA-256 divergence.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import importlib.util
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_MATRIX = os.path.join(REPO_ROOT, "tests", "cross_platform_matrix.txt")
# Conformance cases driven by a custom native C fixture harness rather than
# standalone `zanc <case>.zan --auto-stdlib` (mirrors CMakeLists.txt `_conf_skip_cases`).
CUSTOM_HARNESS_SPEC_ANCHORS = frozenset({"arc_deferred_stack"})


def normalize_os_name(raw=None):
    name = (raw or platform.system()).strip().lower()
    if name.startswith("win") or name.startswith("mingw") or name.startswith("msys"):
        return "windows"
    if name in ("darwin", "macos", "osx"):
        return "macos"
    if name.startswith("linux"):
        return "linux"
    return name


def load_matrix_cases(matrix_path):
    cases = []
    seen = set()
    duplicates = []
    with open(matrix_path, "r", encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line in seen:
                duplicates.append((lineno, line))
                continue
            seen.add(line)
            cases.append(line)
    return cases, duplicates


def load_spec_conformance_anchors(root):
    checker_path = os.path.join(root, "scripts", "check_spec_anchors.py")
    if not os.path.isfile(checker_path):
        return []
    spec = importlib.util.spec_from_file_location("check_spec_anchors", checker_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    en_rules, _ = mod.parse_spec_rules(os.path.join(root, "docs", "SPEC.md"))
    stems = set()
    for meta in en_rules.values():
        for rel in meta["tests"]:
            if rel.startswith("tests/conformance/") and rel.endswith(".zan"):
                stems.add(rel[len("tests/conformance/") : -len(".zan")])
    return sorted(stems)


def check_matrix(root, matrix_path):
    findings = []
    if not os.path.isfile(matrix_path):
        return [f"missing matrix file: {matrix_path}"], []

    cases, duplicates = load_matrix_cases(matrix_path)
    for lineno, name in duplicates:
        findings.append(f"{matrix_path}:{lineno}: duplicate matrix case '{name}'")
    if not cases:
        findings.append(f"{matrix_path}: matrix is empty")

    for name in cases:
        zan_path = os.path.join(root, "tests", "conformance", name + ".zan")
        out_path = os.path.join(root, "tests", "conformance", name + ".out")
        if not os.path.isfile(zan_path):
            findings.append(f"matrix case missing source: tests/conformance/{name}.zan")
        if not os.path.isfile(out_path):
            findings.append(f"matrix case missing golden: tests/conformance/{name}.out")

    case_set = set(cases)
    for anchor_stem in load_spec_conformance_anchors(root):
        if anchor_stem in CUSTOM_HARNESS_SPEC_ANCHORS:
            continue
        if anchor_stem not in case_set:
            findings.append(
                f"SPEC-anchored conformance test '{anchor_stem}' is missing from "
                f"tests/cross_platform_matrix.txt"
            )
    return findings, cases


def normalize_lf(data):
    return data.replace(b"\r\n", b"\n")


def run_one_case(zanc, root, workdir, name, timeout_s):
    src = os.path.join(root, "tests", "conformance", name + ".zan")
    golden_path = os.path.join(root, "tests", "conformance", name + ".out")
    ext = ".exe" if os.name == "nt" else ""
    exe = os.path.join(workdir, name + ext)

    comp = subprocess.run(
        [zanc, src, "--auto-stdlib", "-o", exe],
        capture_output=True,
        timeout=timeout_s,
    )
    if comp.returncode != 0 or not os.path.isfile(exe):
        err_msg = (comp.stderr + comp.stdout).decode("utf-8", errors="replace").strip()
        return name, None, f"compile failed (rc={comp.returncode}): {err_msg[:300]}"

    run = subprocess.run([exe], capture_output=True, timeout=timeout_s)
    if run.returncode != 0:
        err_msg = run.stderr.decode("utf-8", errors="replace").strip()
        return name, None, f"runtime failed (rc={run.returncode}): {err_msg[:300]}"

    got_lf = normalize_lf(run.stdout)
    with open(golden_path, "rb") as gf:
        exp_lf = normalize_lf(gf.read())

    # Match tests/run_case.cmake trailing-newline tolerance for golden comparison,
    # while hashing exact LF-normalized stdout bytes for cross-OS parity.
    if got_lf.rstrip(b"\n") != exp_lf.rstrip(b"\n"):
        return (
            name,
            None,
            f"stdout mismatch vs golden ({len(got_lf)} bytes got vs {len(exp_lf)} expected)",
        )

    digest = hashlib.sha256(got_lf).hexdigest()
    return (
        name,
        {
            "exit_code": run.returncode,
            "bytes": len(got_lf),
            "stdout_sha256": digest,
        },
        None,
    )


def verify_missing_capability_fails_link(zanc, workdir, timeout_s):
    """S33 fail-closed check: missing platform capability must fail at link time."""
    probe_src = os.path.join(workdir, "_s33_missing_cap_probe.zan")
    ext = ".exe" if os.name == "nt" else ""
    probe_exe = os.path.join(workdir, "_s33_missing_cap_probe" + ext)
    if os.path.exists(probe_exe):
        os.remove(probe_exe)

    code = (
        "using System;\n"
        "using System.Runtime.InteropServices;\n"
        "class S33MissingCapProbe {\n"
        '    [DllImport("zan_nonexistent_platform_cap_s33")]\n'
        "    static extern int zan_s33_missing_platform_symbol(int x);\n"
        "    static void Main() {\n"
        "        Console.WriteLine(zan_s33_missing_platform_symbol(1));\n"
        "    }\n"
        "}\n"
    )
    with open(probe_src, "w", encoding="utf-8") as f:
        f.write(code)

    res = subprocess.run(
        [zanc, probe_src, "--auto-stdlib", "-o", probe_exe],
        capture_output=True,
        timeout=timeout_s,
    )
    diag = (res.stderr + res.stdout).decode("utf-8", errors="replace").strip()
    if res.returncode == 0 or os.path.isfile(probe_exe):
        return False, (
            f"missing platform capability unexpectedly succeeded (rc={res.returncode}, "
            f"exe_exists={os.path.isfile(probe_exe)})"
        )
    link_markers = (
        "undefined symbol",
        "unresolved external",
        "cannot find",
        "unable to find",
        "library not found",
        "linking failed",
        "undefined reference",
        "zan_nonexistent_platform_cap_s33",
        "zan_s33_missing_platform_symbol",
    )
    matched_line = None
    for line in diag.splitlines():
        low = line.lower()
        if any(m in low for m in link_markers):
            matched_line = line.strip()
            break
    if not matched_line:
        return False, f"probe failed before link phase (unexpected diag): {diag[:240]}"
    return True, matched_line


def run_matrix(root, matrix_path, zanc, out_path, jobs, timeout_s, skip_link_probe):
    findings, cases = check_matrix(root, matrix_path)
    if findings:
        for item in findings:
            print(f"XPLAT MATRIX: {item}", file=sys.stderr)
        return 1

    zanc_abs = os.path.abspath(zanc)
    if not os.path.isfile(zanc_abs):
        print(f"XPLAT MATRIX: zanc binary not found: {zanc_abs}", file=sys.stderr)
        return 1

    workdir = tempfile.mkdtemp(prefix="zan_xplat_matrix_")
    results = {}
    failures = []
    try:
        max_workers = max(1, min(jobs, len(cases)))
        with ThreadPoolExecutor(max_workers=max_workers) as pool:
            futs = {
                pool.submit(run_one_case, zanc_abs, root, workdir, name, timeout_s): name
                for name in cases
            }
            for fut in as_completed(futs):
                name, meta, err = fut.result()
                if err:
                    failures.append(f"{name}: {err}")
                else:
                    results[name] = meta

        link_probe_ok = True
        link_probe_detail = "skipped"
        if not skip_link_probe:
            link_probe_ok, link_probe_detail = verify_missing_capability_fails_link(
                zanc_abs, workdir, timeout_s
            )
            if not link_probe_ok:
                failures.append(f"link_fail_closed_probe: {link_probe_detail}")
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    if failures:
        for item in sorted(failures):
            print(f"XPLAT FAIL: {item}", file=sys.stderr)
        print(
            f"run_cross_platform_matrix: {len(failures)} failure(s) out of {len(cases)} cases",
            file=sys.stderr,
        )
        return 1

    ordered_cases = {k: results[k] for k in sorted(results.keys())}
    canonical_payload = json.dumps(ordered_cases, sort_keys=True, separators=(",", ":")).encode(
        "utf-8"
    )
    manifest = {
        "schema_version": 1,
        "os": normalize_os_name(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "case_count": len(ordered_cases),
        "matrix_sha256": hashlib.sha256(canonical_payload).hexdigest(),
        "link_fail_closed": link_probe_ok,
        "link_probe_detail": link_probe_detail,
        "cases": ordered_cases,
    }

    if out_path:
        out_dir = os.path.dirname(os.path.abspath(out_path))
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2, sort_keys=True)
            f.write("\n")

    print(
        f"run_cross_platform_matrix: {manifest['os']} clean "
        f"({manifest['case_count']} cases, matrix_sha256={manifest['matrix_sha256'][:16]}, "
        f"link_fail_closed={manifest['link_fail_closed']})"
    )
    return 0


def compare_manifests(paths, required_oses=None):
    findings = []
    if len(paths) < 2:
        return ["compare requires at least 2 platform manifest files"], {}

    loaded = []
    for p in paths:
        if not os.path.isfile(p):
            findings.append(f"manifest not found: {p}")
            continue
        with open(p, "r", encoding="utf-8") as f:
            data = json.load(f)
        loaded.append((p, data))

    if findings:
        return findings, {}

    seen_oses = {}
    for p, data in loaded:
        os_name = normalize_os_name(data.get("os", "unknown"))
        if os_name in seen_oses:
            findings.append(
                f"duplicate OS '{os_name}' in manifests ({seen_oses[os_name]} and {p})"
            )
        seen_oses[os_name] = p
        if not data.get("link_fail_closed", False):
            findings.append(f"{os_name} ({p}): link_fail_closed is not true")

    if required_oses:
        for req in required_oses:
            req_norm = normalize_os_name(req)
            if req_norm not in seen_oses:
                findings.append(f"missing required OS manifest: '{req_norm}'")

    ref_path, ref = loaded[0]
    ref_os = normalize_os_name(ref.get("os", "unknown"))
    ref_cases = ref.get("cases", {})
    if not ref_cases:
        findings.append(f"{ref_os} ({ref_path}): manifest contains zero cases")

    for p, data in loaded[1:]:
        cur_os = normalize_os_name(data.get("os", "unknown"))
        cur_cases = data.get("cases", {})
        missing = sorted(set(ref_cases.keys()) - set(cur_cases.keys()))
        extra = sorted(set(cur_cases.keys()) - set(ref_cases.keys()))
        if missing:
            findings.append(f"{cur_os} missing cases present in {ref_os}: {missing}")
        if extra:
            findings.append(f"{cur_os} has extra cases absent from {ref_os}: {extra}")

        for case in sorted(set(ref_cases.keys()) & set(cur_cases.keys())):
            r_c = ref_cases[case]
            c_c = cur_cases[case]
            if r_c.get("exit_code") != 0 or c_c.get("exit_code") != 0:
                findings.append(
                    f"case '{case}': non-zero exit code "
                    f"({ref_os}={r_c.get('exit_code')}, {cur_os}={c_c.get('exit_code')})"
                )
            if (
                r_c.get("stdout_sha256") != c_c.get("stdout_sha256")
                or r_c.get("bytes") != c_c.get("bytes")
            ):
                findings.append(
                    f"case '{case}': byte divergence between {ref_os} "
                    f"({r_c.get('stdout_sha256')[:12]}, {r_c.get('bytes')}B) and {cur_os} "
                    f"({c_c.get('stdout_sha256')[:12]}, {c_c.get('bytes')}B)"
                )

    summary = {
        "oses": sorted(seen_oses.keys()),
        "case_count": len(ref_cases),
        "matrix_sha256": ref.get("matrix_sha256", ""),
    }
    return findings, summary


def main(argv=None):
    ap = argparse.ArgumentParser(description="S33 Cross-platform conformance matrix gate.")
    ap.add_argument("--root", default=REPO_ROOT, help="Repository root directory")
    ap.add_argument("--matrix", default=None, help="Path to cross_platform_matrix.txt")
    ap.add_argument("--check-matrix", action="store_true", help="Validate matrix definition")
    ap.add_argument("--run", action="store_true", help="Execute matrix and emit JSON manifest")
    ap.add_argument("--zanc", help="Path to zanc compiler executable (for --run)")
    ap.add_argument("--out", help="Output JSON manifest path (for --run)")
    ap.add_argument("--jobs", type=int, default=4, help="Parallel compile/run workers")
    ap.add_argument("--timeout", type=int, default=120, help="Per-case timeout seconds")
    ap.add_argument(
        "--skip-link-probe",
        action="store_true",
        help="Skip missing-capability link-time fail-closed probe",
    )
    ap.add_argument(
        "--compare",
        nargs="+",
        metavar="MANIFEST",
        help="Compare 2+ platform JSON manifests for byte-identical parity",
    )
    ap.add_argument(
        "--require-os",
        default="",
        help="Comma-separated OS list required in --compare (e.g. linux,macos,windows)",
    )
    args = ap.parse_args(argv)

    matrix_path = args.matrix or os.path.join(args.root, "tests", "cross_platform_matrix.txt")

    if args.check_matrix:
        findings, cases = check_matrix(args.root, matrix_path)
        if findings:
            for item in findings:
                print(f"XPLAT MATRIX: {item}", file=sys.stderr)
            return 1
        print(f"cross_platform_matrix: clean ({len(cases)} cases, 100% SPEC anchors covered)")
        return 0

    if args.run:
        if not args.zanc:
            print("error: --run requires --zanc", file=sys.stderr)
            return 2
        return run_matrix(
            args.root,
            matrix_path,
            args.zanc,
            args.out,
            args.jobs,
            args.timeout,
            args.skip_link_probe,
        )

    if args.compare:
        req = [s.strip() for s in args.require_os.split(",") if s.strip()]
        findings, summary = compare_manifests(args.compare, required_oses=req)
        if findings:
            for item in findings:
                print(f"XPLAT PARITY: {item}", file=sys.stderr)
            print(f"compare_manifests: {len(findings)} finding(s)", file=sys.stderr)
            return 1
        print(
            f"cross_platform_parity: PASS ({', '.join(summary['oses'])}, "
            f"{summary['case_count']} cases byte-identical, "
            f"matrix_sha256={summary['matrix_sha256'][:16]})"
        )
        return 0

    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())

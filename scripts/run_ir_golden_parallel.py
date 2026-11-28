#!/usr/bin/env python3
"""Parallel selfhost IR golden runner & drift classifier (B-ID214).

Compiles every case in --cases with --gen1 in parallel, computes raw per-function
SHA-256 fingerprints in-process via scripts/normalize_ir.py, and compares against
<golden-dir>/<case>.hashes.

Drift governance (distinguishing emitter changes from in-flight drift):
  <golden-dir>/EMITTER_PROVENANCE.json records the exact input tree hashes that
  produced the blessed goldens:
    - emitter_sha256: SHA-256 over scripts/selfhost_sources.txt + every listed
      src/<unit>.zan file + scripts/normalize_ir.py
    - stdlib_sha256:  SHA-256 over all stdlib/**/* files

  When any case's IR fingerprint differs from its golden:
    - EMITTER_CHANGED:    current emitter_sha256 != blessed emitter_sha256
                          (deliberate/unblessed emitter change — review diff and
                          bless in the same commit with -DBLESS=1)
    - IN_FLIGHT_DRIFT:    current emitter_sha256 == blessed emitter_sha256, yet
                          IR changed (stdlib/package/case change or emitter
                          nondeterminism; prints whether stdlib_sha256 changed)
    - UNPROVENANCED_DIFF: EMITTER_PROVENANCE.json is absent (legacy golden set;
                          bless with -DBLESS=1 to establish provenance)

  Orphaned *.hashes files present in --golden-dir but absent from --cases are
  reported as ORPHAN_GOLDEN (and removed when --bless --prune-orphans is given).
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

PROVENANCE_FILE = "EMITTER_PROVENANCE.json"
PROVENANCE_SCHEMA_VERSION = 1


def _load_normalizer(norm_path):
    spec = importlib.util.spec_from_file_location("zan_ir_normalizer", str(norm_path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _write_atomic(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", newline="\n", delete=False,
                dir=str(path.parent), prefix=".ir-golden-", suffix=".tmp") as f:
            tmp = f.name
            f.write(text)
        os.replace(tmp, str(path))
    finally:
        if tmp is not None and os.path.exists(tmp):
            os.unlink(tmp)


def _hash_files(root, paths):
    h = hashlib.sha256()
    root = Path(root).resolve()
    for p in sorted(paths, key=lambda x: Path(x).resolve().as_posix()):
        p = Path(p)
        try:
            rel = p.resolve().relative_to(root).as_posix()
        except ValueError:
            rel = p.name
        h.update(rel.encode("utf-8"))
        h.update(b"\0")
        h.update(p.read_bytes())
        h.update(b"\0")
    return h.hexdigest()


def compute_tree_provenance(workdir, norm_path, sources_list_path=None):
    """Compute deterministic hashes for the selfhost emitter and stdlib trees."""
    root = Path(workdir).resolve()
    sources_list = Path(sources_list_path) if sources_list_path else (root / "scripts" / "selfhost_sources.txt")
    emitter_files = []
    if sources_list.is_file():
        emitter_files.append(sources_list)
        for raw in sources_list.read_text(encoding="utf-8").splitlines():
            s = raw.strip()
            if not s or s.startswith("#"):
                continue
            unit = root / "src" / (s + ".zan")
            if unit.is_file():
                emitter_files.append(unit)
    norm_p = Path(norm_path)
    if norm_p.is_file():
        emitter_files.append(norm_p)
    emitter_sha = _hash_files(root, emitter_files) if emitter_files else ""

    stdlib_dir = root / "stdlib"
    stdlib_files = [p for p in stdlib_dir.rglob("*") if p.is_file()] if stdlib_dir.is_dir() else []
    stdlib_sha = _hash_files(root, stdlib_files) if stdlib_files else ""
    return {
        "schema_version": PROVENANCE_SCHEMA_VERSION,
        "emitter_sha256": emitter_sha,
        "emitter_file_count": len(emitter_files),
        "stdlib_sha256": stdlib_sha,
        "stdlib_file_count": len(stdlib_files),
    }


def load_provenance(golden_dir):
    p = Path(golden_dir) / PROVENANCE_FILE
    if not p.is_file():
        return None
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict) or not data.get("emitter_sha256"):
        return None
    return data


def classify_drift(current_prov, blessed_prov):
    """Classify an IR golden mismatch using emitter/stdlib provenance hashes.

    Returns (code, detail):
      - EMITTER_CHANGED:    emitter sources differ from blessed provenance
      - IN_FLIGHT_DRIFT:    emitter sources match, but IR drifted (stdlib/case/nondet)
      - UNPROVENANCED_DIFF: no EMITTER_PROVENANCE.json recorded in golden_dir
    """
    if blessed_prov is None:
        return (
            "UNPROVENANCED_DIFF",
            "no %s in golden directory; bless with -DBLESS=1 to establish emitter provenance"
            % PROVENANCE_FILE,
        )
    cur_emit = current_prov.get("emitter_sha256", "")
    old_emit = blessed_prov.get("emitter_sha256", "")
    if cur_emit != old_emit:
        return (
            "EMITTER_CHANGED",
            "selfhost emitter changed (%s -> %s); review IR diff and re-bless with -DBLESS=1"
            % (old_emit[:12], cur_emit[:12]),
        )
    cur_std = current_prov.get("stdlib_sha256", "")
    old_std = blessed_prov.get("stdlib_sha256", "")
    if cur_std != old_std:
        return (
            "IN_FLIGHT_DRIFT",
            "emitter unchanged (%s) but stdlib tree changed (%s -> %s)"
            % (cur_emit[:12], old_std[:12], cur_std[:12]),
        )
    return (
        "IN_FLIGHT_DRIFT",
        "emitter (%s) and stdlib (%s) unchanged; drift is from case/package input or nondeterminism"
        % (cur_emit[:12], cur_std[:12]),
    )


def _diff_summary(expected_text, actual_text):
    def parse_rows(text):
        out = {}
        for line in text.splitlines():
            parts = line.split()
            if len(parts) == 2:
                out[parts[1]] = parts[0]
        return out

    exp = parse_rows(expected_text)
    act = parse_rows(actual_text)
    added = sorted(act.keys() - exp.keys())
    removed = sorted(exp.keys() - act.keys())
    changed = sorted(k for k in act.keys() & exp.keys() if act[k] != exp[k])
    return added, removed, changed


def _run_case(case, args, normalizer):
    src = Path(args.case_dir) / (case + ".zan")
    if not src.is_file():
        return {"case": case, "status": "missing_source",
                "detail": "case source not found under %s" % args.case_dir}
    ll_path = Path(args.outdir) / (case + ".ll")
    if ll_path.exists():
        ll_path.unlink()
    cmd = [args.gen1, str(ll_path), str(src)]
    try:
        proc = subprocess.run(
            cmd, cwd=args.workdir, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return {"case": case, "status": "compile_timeout",
                "detail": "gen1 timed out after %ds" % args.timeout}
    if proc.returncode != 0 or not ll_path.is_file():
        err = (proc.stderr or proc.stdout or "").strip().splitlines()
        tail = err[-1] if err else "no output"
        return {"case": case, "status": "compile_failed",
                "detail": "gen1 failed (rc=%d): %s" % (proc.returncode, tail)}
    try:
        ir_text = ll_path.read_text(encoding="utf-8", errors="replace")
        actual_hashes = normalizer.compute_fnhash_text(ir_text)
    except Exception as exc:
        return {"case": case, "status": "normalize_failed",
                "detail": "normalizer error: %s" % exc}

    golden_path = Path(args.golden_dir) / (case + ".hashes")
    if args.bless:
        _write_atomic(golden_path, actual_hashes)
        return {"case": case, "status": "blessed"}

    if not golden_path.is_file():
        return {"case": case, "status": "missing_golden",
                "detail": "no golden at %s" % golden_path}

    got_path = Path(args.outdir) / (case + ".hashes")
    _write_atomic(got_path, actual_hashes)
    expected_hashes = golden_path.read_text(encoding="utf-8")
    if actual_hashes.strip() == expected_hashes.strip():
        return {"case": case, "status": "pass"}

    added, removed, changed = _diff_summary(expected_hashes, actual_hashes)
    return {
        "case": case,
        "status": "diff",
        "added": added,
        "removed": removed,
        "changed": changed,
        "detail": "+%d -%d ~%d functions (sample: %s)" % (
            len(added), len(removed), len(changed),
            ", ".join((changed + added + removed)[:5]),
        ),
    }


def load_cases(cases_path):
    cases = []
    seen = set()
    for raw in Path(cases_path).read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        if s in seen:
            raise ValueError("duplicate case in %s: %s" % (cases_path, s))
        seen.add(s)
        cases.append(s)
    return cases


def find_orphan_goldens(golden_dir, cases):
    gdir = Path(golden_dir)
    if not gdir.is_dir():
        return []
    expected = set(cases)
    return sorted(p.stem for p in gdir.glob("*.hashes") if p.stem not in expected)


def run_gate(args):
    cases = load_cases(args.cases)
    Path(args.outdir).mkdir(parents=True, exist_ok=True)
    Path(args.golden_dir).mkdir(parents=True, exist_ok=True)
    normalizer = _load_normalizer(args.norm)

    current_prov = compute_tree_provenance(
        args.workdir, args.norm, args.sources_list)
    blessed_prov = load_provenance(args.golden_dir)
    orphans = find_orphan_goldens(args.golden_dir, cases)

    jobs = max(1, int(args.jobs))
    results = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futs = {pool.submit(_run_case, c, args, normalizer): c for c in cases}
        for fut in as_completed(futs):
            results.append(fut.result())
    results.sort(key=lambda r: cases.index(r["case"]))

    passed = [r for r in results if r["status"] == "pass"]
    blessed = [r for r in results if r["status"] == "blessed"]
    diffs = [r for r in results if r["status"] == "diff"]
    errors = [r for r in results if r["status"] not in ("pass", "blessed", "diff")]

    pruned_orphans = []
    if args.bless and args.prune_orphans and orphans:
        for name in orphans:
            p = Path(args.golden_dir) / (name + ".hashes")
            if p.exists():
                p.unlink()
                pruned_orphans.append(name)
        orphans = []

    if args.bless and not errors:
        prov_out = dict(current_prov)
        prov_out["cases_count"] = len(cases)
        _write_atomic(
            Path(args.golden_dir) / PROVENANCE_FILE,
            json.dumps(prov_out, indent=2, sort_keys=True) + "\n",
        )

    drift_code = None
    drift_detail = None
    if diffs:
        drift_code, drift_detail = classify_drift(current_prov, blessed_prov)

    report = {
        "mode": "bless" if args.bless else "check",
        "cases_total": len(cases),
        "passed": len(passed),
        "blessed": len(blessed),
        "diffs": len(diffs),
        "errors": len(errors),
        "drift_classification": drift_code,
        "drift_detail": drift_detail,
        "orphan_goldens": orphans,
        "pruned_orphans": pruned_orphans,
        "current_provenance": current_prov,
        "blessed_provenance": blessed_prov,
        "failures": [r for r in results if r["status"] not in ("pass", "blessed")],
    }

    if args.report_json:
        _write_atomic(args.report_json, json.dumps(report, indent=2, sort_keys=True) + "\n")

    # Emit concise, actionable status output.
    if args.verbose:
        for r in results:
            if r["status"] == "pass":
                print("-- PASS %s" % r["case"])
            elif r["status"] == "blessed":
                print("-- BLESS %s" % r["case"])
    for r in errors:
        print("FAIL %s [%s]: %s" % (r["case"], r["status"].upper(), r["detail"]),
              file=sys.stderr)
    for r in diffs:
        print("FAIL %s [%s]: %s" % (r["case"], drift_code, r["detail"]),
              file=sys.stderr)
    if orphans:
        print("WARN ORPHAN_GOLDEN (%d): %s" % (
            len(orphans), ", ".join(orphans[:10])), file=sys.stderr)

    if args.bless:
        print("-- ir golden bless: %d rewritten, %d pruned orphans, %d errors (emitter=%s stdlib=%s)" % (
            len(blessed), len(pruned_orphans), len(errors),
            current_prov["emitter_sha256"][:12], current_prov["stdlib_sha256"][:12]))
        return 0 if not errors else 1

    if diffs:
        print("-- ir golden drift classification: %s — %s" % (drift_code, drift_detail),
              file=sys.stderr)
    print("-- ir golden gate: %d passed, %d diffs, %d errors, %d orphans" % (
        len(passed), len(diffs), len(errors), len(orphans)))
    if errors or diffs or (args.strict_orphans and orphans):
        return 1
    return 0


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--gen1", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--case-dir", required=True)
    ap.add_argument("--cases", required=True)
    ap.add_argument("--golden-dir", required=True)
    ap.add_argument("--norm", required=True)
    ap.add_argument("--workdir", default=".")
    ap.add_argument("--sources-list", default=None)
    ap.add_argument("--jobs", type=int, default=min(8, max(1, os.cpu_count() or 4)))
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--bless", action="store_true")
    ap.add_argument("--prune-orphans", action="store_true")
    ap.add_argument("--strict-orphans", action="store_true")
    ap.add_argument("--report-json", default=None)
    ap.add_argument("--verbose", action="store_true")
    return ap.parse_args(argv)


def main(argv=None):
    return run_gate(parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())

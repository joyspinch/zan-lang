#!/usr/bin/env python3
"""Read saved IR and baselines without invoking a compiler or writing files.

Usage: python -I scripts/audit_selfhost_ir_golden.py [--outdir build/selfhost_repo]
Prints JSON to stdout. Saved IR is historical evidence unless accompanied by a
stable-input provenance record from the final emitter. Hash-only old baselines
cannot distinguish renaming/formatting from semantic changes; this tool never
labels a changed function semantically equivalent and never blesses a baseline.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

# Loading the existing fingerprint parser must not create __pycache__ either.
sys.dont_write_bytecode = True


def read_identity(path):
    data = path.read_bytes()
    return data, {"path": str(path.resolve()), "sha256": hashlib.sha256(data).hexdigest(),
                  "mtime_ns": path.stat().st_mtime_ns}


def fingerprints(text, parser):
    lines = text.splitlines()
    result = {}
    for index, line in enumerate(lines):
        if not line.strip().startswith("define "):
            continue
        header = re.fullmatch(
            r'define\s+[^@\n]+@([A-Za-z_.$][\w.$]*|[0-9]+)\(.*\).*\{\s*',
            line.strip())
        if header is None or header[1] in result:
            raise ValueError("Unsupported or duplicate function header at line %d" % (index + 1))
        end, _, _ = parser._collect_function(lines, index)
        result[header[1]] = hashlib.sha256(
            "\n".join(lines[index:end + 1]).encode("utf-8")).hexdigest()[:16]
    if not result:
        raise ValueError("No function definitions")
    return result


def baseline(text):
    result = {}
    for line in text.splitlines():
        digest, name = line.split()
        if not re.fullmatch(r"[0-9a-f]{16}", digest) or name in result:
            raise ValueError("Malformed or duplicate baseline row")
        result[name] = digest
    if not result:
        raise ValueError("Empty baseline")
    return result


def classify(name, user_types):
    # This is a review-routing heuristic only. Closure/binding thunks can be
    # emitted for imported libraries, so keep that ambiguity in the label.
    if any(name == t or name.startswith(t + "_") for t in user_types):
        return "case_type_symbol"
    if name.startswith(("__lambda_", "__mg_", "__task_", "__zbind_")):
        return "generated_owner_unresolved"
    if name == "main" or name.startswith(("__zan_", "zan_")) or re.match(
            r"^z(?:alloc|retain|release|str|arr|list|dict|delegate)", name):
        return "runtime_or_entry_symbol"
    return "library_or_unresolved_symbol"


def audit(repo, outdir, cases_path, gen1_path=None):
    norm = repo / "scripts/normalize_ir.py"
    spec = importlib.util.spec_from_file_location("ir_fingerprint_parser", norm)
    parser = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parser)
    case_bytes, case_identity = read_identity(cases_path)
    cases = [s.strip() for s in case_bytes.decode("utf-8").splitlines()
             if s.strip() and not s.strip().startswith("#")]
    if len(cases) != len(set(cases)):
        raise ValueError("Duplicate case in acceptance list")
    inputs = [repo / "scripts/selfhost_sources.txt", norm, Path(__file__)]
    for line in inputs[0].read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.strip().startswith("#"):
            inputs.append(repo / "src" / (line.strip() + ".zan"))
    inputs.extend(p for p in (repo / "stdlib").rglob("*") if p.is_file())
    identities = [read_identity(p)[1] for p in inputs]
    # Review-tool edits do not date the emitter; only its source/stdlib inputs do.
    newest_input = max(i["mtime_ns"] for i in identities
                       if i["path"] not in {str(norm.resolve()), str(Path(__file__).resolve())})
    gen1 = gen1_path or outdir / ("gen1.exe" if (outdir / "gen1.exe").exists() else "gen1")
    gen1_identity = read_identity(gen1)[1] if gen1.is_file() else None
    host = repo / "build/zanc.exe"
    if not host.exists():
        host = repo / "build/zanc"
    host_identity = read_identity(host)[1] if host.is_file() else None
    rows = []
    counts = Counter()
    routing = Counter()
    for case in cases:
        row = {"case": case}
        src = repo / "tests/conformance" / (case + ".zan")
        golden = repo / "tests/selfhost/ir_golden" / (case + ".hashes")
        ir = outdir / (case + ".ll")
        source_data = b""
        if not src.is_file():
            row["source_missing"] = True
            counts["missing_source"] += 1
        else:
            source_data, row["source"] = read_identity(src)
        if not golden.is_file():
            counts["missing_baseline"] += 1
            row["baseline_missing"] = True
        if not ir.is_file():
            counts["missing_saved_ir"] += 1
            row["status"] = "no_saved_ir"
            rows.append(row)
            continue
        ir_data, row["saved_ir"] = read_identity(ir)
        row["predates_current_input_by_mtime"] = row["saved_ir"]["mtime_ns"] < max(
            newest_input, row.get("source", {}).get("mtime_ns", 0))
        counts["saved_ir"] += 1
        counts["saved_ir_predates_current_input"] += row["predates_current_input_by_mtime"]
        try:
            actual = fingerprints(ir_data.decode("utf-8"), parser)
            row["function_count"] = len(actual)
            if not golden.is_file():
                row["status"] = "missing_baseline_with_saved_ir"
            else:
                golden_data, row["baseline"] = read_identity(golden)
                expected = baseline(golden_data.decode("utf-8"))
                added = sorted(actual.keys() - expected.keys())
                removed = sorted(expected.keys() - actual.keys())
                changed = sorted(n for n in actual.keys() & expected.keys()
                                 if actual[n] != expected[n])
                row.update(added=added, removed=removed, changed=changed)
                names = added + removed + changed
                row["status"] = "text_diff" if names else "match"
                counts[row["status"]] += 1
                source = source_data.decode("utf-8")
                user_types = re.findall(r"\b(?:class|struct|interface|enum)\s+(\w+)", source)
                groups = {kind: sorted(n for n in names if classify(n, user_types) == kind)
                          for kind in ("case_type_symbol", "generated_owner_unresolved",
                                       "runtime_or_entry_symbol", "library_or_unresolved_symbol")}
                row["review_routing_heuristic"] = groups
                if groups["case_type_symbol"] or groups["generated_owner_unresolved"]:
                    routing["case_or_generated_function_diff"] += 1
                elif groups["library_or_unresolved_symbol"]:
                    routing["library_or_unresolved_diff_no_case_symbol_detected"] += 1
                elif names:
                    routing["runtime_entry_only_by_symbol_heuristic"] += 1
        except (UnicodeError, ValueError) as error:
            row["status"] = "parse_error"
            row["error"] = str(error)
            counts["parse_error"] += 1
        rows.append(row)
    return {"schema": "saved-ir-review-v1", "case_count": len(cases),
            "case_list": case_identity, "counts": dict(counts),
            "review_routing_heuristic": dict(routing), "gen1": gen1_identity,
            "host": host_identity, "current_inputs": identities,
            "gen1_predates_current_input_by_mtime": bool(
                gen1_identity and gen1_identity["mtime_ns"] < newest_input),
            "limitations": ["mtime can prove stale ordering, not freshness or provenance",
                            "saved IR may come from different runs; no final-bin claim",
                            "old hashes cannot prove rename-only or semantic equivalence",
                            "module globals/types/declarations/attributes are outside fingerprints",
                            "function-owner routing is heuristic; inspect every changed function"],
            "cases": rows}


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    args.add_argument("--outdir", type=Path)
    args.add_argument("--cases", type=Path)
    args.add_argument("--gen1", type=Path, help="Actual emitter; no copy into IR output directory required")
    args.add_argument("--require-complete", action="store_true",
                      help="Fail on missing saved IR/gen1 too; baseline differences remain review findings")
    options = args.parse_args()
    repo = options.repo.resolve()
    result = audit(repo, options.outdir or repo / "build/selfhost_repo",
                   options.cases or repo / "tests/selfhost/diff_cases.txt", options.gen1)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return audit_exit_code(result, options.require_complete)


def audit_exit_code(result, require_complete=False):
    counts = result["counts"]
    invalid = counts.get("parse_error") or counts.get("missing_source")
    incomplete = require_complete and (counts.get("missing_saved_ir") or not result.get("gen1"))
    # Success means a parseable review inventory, never baseline acceptance.
    return 1 if invalid or incomplete else 0


if __name__ == "__main__":
    raise SystemExit(main())

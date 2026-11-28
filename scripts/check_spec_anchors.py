#!/usr/bin/env python3
"""SPEC-to-test bidirectional anchor gate (ZAN_STANDARD S30 / S31, B-ID226).

Validates every `[rule:SPEC-<DOMAIN>-<NN>]` entry in `docs/SPEC.md` and
`docs/SPEC.zh-CN.md`:

  1. No duplicate rule IDs within a spec document.
  2. Bilingual parity: `docs/SPEC.md` and `docs/SPEC.zh-CN.md` declare the exact
     same rule IDs, anchored test paths, and referenced diagnostic codes.
  3. Every rule anchors at least one `.zan` test under `tests/`, and both the
     test source and its companion oracle (`.out` for conformance tests,
     `.stderr` for selfhost reject tests) exist on disk.
  4. Diagnostic codes (`E####` / `W####`) referenced by rules exist in
     `docs/DIAGNOSTIC_CODES.md` (when present), and any anchored reject test's
     `.stderr` mentions at least one referenced diagnostic code when diagnostic
     codes are listed on that rule.

Exit 0 = clean, 1 = findings reported.
"""

import argparse
import json
import os
import re
import sys

RULE_RE = re.compile(r"\[rule:(SPEC-[A-Z]+-\d{2})\]")
TEST_PATH_RE = re.compile(r"`(tests/[A-Za-z0-9_./-]+\.zan)`")
DIAG_CODE_RE = re.compile(r"\b([EW]\d{4})\b")
DIAG_TABLE_RE = re.compile(r"^\|\s*([EW]\d{4})\s*\|")
REQUIRED_DOMAINS = ("GEN", "ARC", "ASYNC")


def parse_spec_rules(path):
    """Return (rules_dict, errors) where rules_dict maps rule_id -> metadata."""
    rules = {}
    errors = []
    if not os.path.isfile(path):
        return rules, [f"missing spec file: {path}"]

    with open(path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    for lineno, line in enumerate(lines, 1):
        matches = RULE_RE.findall(line)
        if not matches:
            continue
        if len(matches) > 1:
            errors.append(
                f"{path}:{lineno}: multiple rule anchors on one line: {matches}"
            )
            continue
        rule_id = matches[0]
        if rule_id in rules:
            prev = rules[rule_id]["line"]
            errors.append(
                f"{path}:{lineno}: duplicate rule {rule_id} (first at line {prev})"
            )
            continue
        tests = sorted(set(TEST_PATH_RE.findall(line)))
        diags = sorted(set(DIAG_CODE_RE.findall(line)))
        rules[rule_id] = {
            "line": lineno,
            "tests": tests,
            "diagnostics": diags,
        }
    return rules, errors


def load_known_diag_codes(diag_md_path):
    if not os.path.isfile(diag_md_path):
        return None
    codes = set()
    with open(diag_md_path, "r", encoding="utf-8") as f:
        for line in f:
            m = DIAG_TABLE_RE.match(line)
            if m:
                codes.add(m.group(1))
    return codes


def companion_oracle_path(root, rel_zan):
    stem = rel_zan[:-4]
    if rel_zan.startswith("tests/conformance/"):
        return os.path.join(root, stem + ".out"), stem + ".out"
    if rel_zan.startswith("tests/selfhost/reject/"):
        return os.path.join(root, stem + ".stderr"), stem + ".stderr"
    return None, None


def check_repo(root):
    en_path = os.path.join(root, "docs", "SPEC.md")
    zh_path = os.path.join(root, "docs", "SPEC.zh-CN.md")
    diag_md_path = os.path.join(root, "docs", "DIAGNOSTIC_CODES.md")

    en_rules, en_errs = parse_spec_rules(en_path)
    zh_rules, zh_errs = parse_spec_rules(zh_path)
    findings = list(en_errs) + list(zh_errs)

    if not en_rules:
        findings.append("docs/SPEC.md: no [rule:SPEC-*] anchors found")
    if not zh_rules:
        findings.append("docs/SPEC.zh-CN.md: no [rule:SPEC-*] anchors found")

    en_ids = set(en_rules.keys())
    zh_ids = set(zh_rules.keys())
    for rid in sorted(en_ids - zh_ids):
        findings.append(f"rule {rid} present in docs/SPEC.md but missing in docs/SPEC.zh-CN.md")
    for rid in sorted(zh_ids - en_ids):
        findings.append(f"rule {rid} present in docs/SPEC.zh-CN.md but missing in docs/SPEC.md")

    present_domains = {rid.split("-")[1] for rid in en_ids}
    for domain in REQUIRED_DOMAINS:
        if domain not in present_domains:
            findings.append(f"docs/SPEC.md: missing required rule domain SPEC-{domain}-NN")

    known_diags = load_known_diag_codes(diag_md_path)
    diag_to_rules = {}

    for rid in sorted(en_ids):
        en_meta = en_rules[rid]
        if rid in zh_rules:
            zh_meta = zh_rules[rid]
            if en_meta["tests"] != zh_meta["tests"]:
                findings.append(
                    f"rule {rid}: test anchor drift between SPEC.md ({en_meta['tests']}) "
                    f"and SPEC.zh-CN.md ({zh_meta['tests']})"
                )
            if en_meta["diagnostics"] != zh_meta["diagnostics"]:
                findings.append(
                    f"rule {rid}: diagnostic code drift between SPEC.md ({en_meta['diagnostics']}) "
                    f"and SPEC.zh-CN.md ({zh_meta['diagnostics']})"
                )

        if not en_meta["tests"]:
            findings.append(f"rule {rid}: must anchor at least one tests/.../*.zan file")
            continue

        for rel_zan in en_meta["tests"]:
            abs_zan = os.path.join(root, rel_zan)
            if not os.path.isfile(abs_zan):
                findings.append(f"rule {rid}: anchored test missing on disk: {rel_zan}")
                continue
            oracle_abs, oracle_rel = companion_oracle_path(root, rel_zan)
            if oracle_abs is not None and not os.path.isfile(oracle_abs):
                findings.append(
                    f"rule {rid}: missing companion oracle {oracle_rel} for {rel_zan}"
                )
            elif (
                rel_zan.startswith("tests/selfhost/reject/")
                and oracle_abs is not None
                and en_meta["diagnostics"]
            ):
                with open(oracle_abs, "r", encoding="utf-8") as sf:
                    stderr_text = sf.read()
                if not any(code in stderr_text for code in en_meta["diagnostics"]):
                    findings.append(
                        f"rule {rid}: reject oracle {oracle_rel} does not mention any of "
                        f"{en_meta['diagnostics']}"
                    )

        for code in en_meta["diagnostics"]:
            diag_to_rules.setdefault(code, []).append(rid)
            if known_diags is not None and code not in known_diags:
                findings.append(
                    f"rule {rid}: diagnostic code {code} not found in docs/DIAGNOSTIC_CODES.md"
                )

    summary = {
        "rules": {
            rid: {
                "tests": en_rules[rid]["tests"],
                "diagnostics": en_rules[rid]["diagnostics"],
            }
            for rid in sorted(en_ids)
        },
        "diagnostics_to_rules": {k: sorted(v) for k, v in sorted(diag_to_rules.items())},
        "unique_tests": sorted({t for meta in en_rules.values() for t in meta["tests"]}),
    }
    return findings, summary


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check SPEC rule-to-test anchors (S30/S31).")
    ap.add_argument(
        "--root",
        default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        help="Repository root directory",
    )
    ap.add_argument(
        "--json",
        action="store_true",
        help="Emit rule/test/diagnostic mapping as JSON on success",
    )
    args = ap.parse_args(argv)

    findings, summary = check_repo(args.root)
    if findings:
        for item in findings:
            print(f"SPEC ANCHOR: {item}", file=sys.stderr)
        print(f"check_spec_anchors: {len(findings)} finding(s)", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print(
            "check_spec_anchors: clean (%d rules, %d unique tests, %d diag links)"
            % (
                len(summary["rules"]),
                len(summary["unique_tests"]),
                len(summary["diagnostics_to_rules"]),
            )
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())

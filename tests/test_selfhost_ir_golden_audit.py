"""Saved-IR audit protocol checks. No compiler, regeneration or golden writes."""
import hashlib
import importlib.util
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


AUDIT = load("golden_audit", ROOT / "scripts/audit_selfhost_ir_golden.py")
PARSER = load("golden_parser", ROOT / "scripts/normalize_ir.py")
RUNNER = load("golden_runner", ROOT / "scripts/run_ir_golden_parallel.py")
FUNCTION = "define i32 @Program_Main(i32 %x) {\nentry:\n  ret i32 %x\n}"


class SavedIrAuditTests(unittest.TestCase):
    def test_raw_fingerprint_contract_and_rename_sensitivity(self):
        hashes = AUDIT.fingerprints(FUNCTION, PARSER)
        self.assertEqual(hashes, {"Program_Main": hashlib.sha256(
            FUNCTION.encode()).hexdigest()[:16]})
        self.assertEqual(AUDIT.baseline("%s Program_Main\n" % hashes["Program_Main"]), hashes)
        self.assertNotEqual(hashes, AUDIT.fingerprints(
            FUNCTION.replace("%x", "%renamed"), PARSER))
        # Module-only edits remain outside the raw function gate. The report
        # must retain its explicit limitation instead of treating this as proof.
        self.assertEqual(hashes, AUDIT.fingerprints("@state = global i32 9\n" + FUNCTION, PARSER))

    def test_invalid_evidence_fails_closed(self):
        for text in ("; empty", FUNCTION + "\n" + FUNCTION,
                     FUNCTION[:-1], "define broken\n}"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                AUDIT.fingerprints(text, PARSER)
        for text in ("", "not-a-digest main", "a" * 16 + " main\n" + "b" * 16 + " main"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                AUDIT.baseline(text)

    def test_complete_inventory_is_separate_from_baseline_acceptance(self):
        findings = {"counts": {"missing_baseline": 118, "text_diff": 244},
                    "gen1": {"sha256": "synthetic-emitter"}}
        self.assertEqual(AUDIT.audit_exit_code(findings, True), 0)
        findings["counts"]["missing_saved_ir"] = 1
        self.assertEqual(AUDIT.audit_exit_code(findings), 0)
        self.assertEqual(AUDIT.audit_exit_code(findings, True), 1)
        findings["counts"].pop("missing_saved_ir")
        findings["gen1"] = None
        self.assertEqual(AUDIT.audit_exit_code(findings, True), 1)
        findings["counts"]["parse_error"] = 1
        self.assertEqual(AUDIT.audit_exit_code(findings), 1)

    def test_symbol_routing_preserves_ambiguity(self):
        self.assertEqual(AUDIT.classify("Program_Main", ["Program"]), "case_type_symbol")
        self.assertEqual(AUDIT.classify("__lambda_0", []), "generated_owner_unresolved")
        self.assertEqual(AUDIT.classify("Unknown_Run", []), "library_or_unresolved_symbol")
        self.assertEqual(AUDIT.classify("zan_co_sched_run", []), "runtime_or_entry_symbol")


class DriftGovernanceTests(unittest.TestCase):
    """B-ID214: verify EMITTER_CHANGED vs IN_FLIGHT_DRIFT vs UNPROVENANCED_DIFF
    classification and parallel bless/orphan-pruning contracts."""

    def test_classify_drift_distinguishes_emitter_from_inflight(self):
        cur = {"emitter_sha256": "aaaa" * 16, "stdlib_sha256": "1111" * 16}
        code, _ = RUNNER.classify_drift(cur, None)
        self.assertEqual(code, "UNPROVENANCED_DIFF")

        old_emit = {"emitter_sha256": "bbbb" * 16, "stdlib_sha256": "1111" * 16}
        code, detail = RUNNER.classify_drift(cur, old_emit)
        self.assertEqual(code, "EMITTER_CHANGED")
        self.assertIn("re-bless", detail)

        old_stdlib = {"emitter_sha256": "aaaa" * 16, "stdlib_sha256": "2222" * 16}
        code, detail = RUNNER.classify_drift(cur, old_stdlib)
        self.assertEqual(code, "IN_FLIGHT_DRIFT")
        self.assertIn("stdlib", detail)

        same_all = {"emitter_sha256": "aaaa" * 16, "stdlib_sha256": "1111" * 16}
        code, detail = RUNNER.classify_drift(cur, same_all)
        self.assertEqual(code, "IN_FLIGHT_DRIFT")
        self.assertIn("nondeterminism", detail)

    def test_parallel_runner_bless_check_orphans_and_drift(self):
        import json
        import tempfile

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "scripts").mkdir()
            (root / "src/selfhost").mkdir(parents=True)
            (root / "stdlib").mkdir()
            (root / "cases").mkdir()
            (root / "goldens").mkdir()
            (root / "out").mkdir()

            emitter_file = root / "src/selfhost/irgen.zan"
            emitter_file.write_text("// emitter v1\n", encoding="utf-8")
            (root / "scripts/selfhost_sources.txt").write_text(
                "selfhost/irgen\n", encoding="utf-8")
            stdlib_file = root / "stdlib/Core.zan"
            stdlib_file.write_text("// stdlib v1\n", encoding="utf-8")

            # Fake gen1 executable script that emits deterministic IR from case file
            fake_gen1_py = root / "fake_gen1.py"
            fake_gen1_py.write_text(
                "import pathlib, sys\n"
                "out_ll, src = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])\n"
                "body = src.read_text(encoding='utf-8').strip()\n"
                "out_ll.write_text(f'define i32 @Main() {{\\nentry:\\n  ret i32 {body}\\n}}\\n', encoding='utf-8')\n",
                encoding="utf-8",
            )
            if sys.platform == "win32":
                fake_gen1 = root / "fake_gen1.cmd"
                fake_gen1.write_text(
                    '@echo off\r\n"%s" "%s" %%1 %%2\r\n' % (sys.executable, fake_gen1_py),
                    encoding="utf-8",
                )
            else:
                fake_gen1 = root / "fake_gen1.sh"
                fake_gen1.write_text(
                    '#!/bin/sh\nexec "%s" "%s" "$1" "$2"\n' % (sys.executable, fake_gen1_py),
                    encoding="utf-8",
                )
                fake_gen1.chmod(0o755)

            (root / "cases/alpha.zan").write_text("1\n", encoding="utf-8")
            (root / "cases/beta.zan").write_text("2\n", encoding="utf-8")
            cases_txt = root / "cases.txt"
            cases_txt.write_text("alpha\nbeta\n", encoding="utf-8")
            orphan = root / "goldens/stale_orphan.hashes"
            orphan.write_text("0000000000000000 Main\n", encoding="utf-8")

            base_argv = [
                "--gen1", str(fake_gen1),
                "--outdir", str(root / "out"),
                "--case-dir", str(root / "cases"),
                "--cases", str(cases_txt),
                "--golden-dir", str(root / "goldens"),
                "--norm", str(ROOT / "scripts/normalize_ir.py"),
                "--workdir", str(root),
                "--jobs", "2",
            ]
            rep_path = root / "report.json"

            # 1. Bless writes goldens, writes EMITTER_PROVENANCE.json, prunes orphan
            rc = RUNNER.main(base_argv + ["--bless", "--prune-orphans", "--report-json", str(rep_path)])
            self.assertEqual(rc, 0)
            self.assertFalse(orphan.exists())
            self.assertTrue((root / "goldens/EMITTER_PROVENANCE.json").is_file())

            # 2. Clean check passes
            rc = RUNNER.main(base_argv + ["--report-json", str(rep_path)])
            self.assertEqual(rc, 0)
            rep = json.loads(rep_path.read_text(encoding="utf-8"))
            self.assertEqual(rep["passed"], 2)
            self.assertEqual(rep["diffs"], 0)

            # 3. Mutate case IR without touching emitter -> IN_FLIGHT_DRIFT
            (root / "cases/beta.zan").write_text("99\n", encoding="utf-8")
            rc = RUNNER.main(base_argv + ["--report-json", str(rep_path)])
            self.assertEqual(rc, 1)
            rep = json.loads(rep_path.read_text(encoding="utf-8"))
            self.assertEqual(rep["drift_classification"], "IN_FLIGHT_DRIFT")
            self.assertEqual(rep["diffs"], 1)

            # 4. Mutate emitter source -> EMITTER_CHANGED
            emitter_file.write_text("// emitter v2\n", encoding="utf-8")
            rc = RUNNER.main(base_argv + ["--report-json", str(rep_path)])
            self.assertEqual(rc, 1)
            rep = json.loads(rep_path.read_text(encoding="utf-8"))
            self.assertEqual(rep["drift_classification"], "EMITTER_CHANGED")


if __name__ == "__main__":
    unittest.main()

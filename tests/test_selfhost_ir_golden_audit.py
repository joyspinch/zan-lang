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


if __name__ == "__main__":
    unittest.main()

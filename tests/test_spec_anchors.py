"""Unit & live-repo tests for scripts/check_spec_anchors.py (ZAN_STANDARD S30/S31)."""

import importlib.util
import os
import tempfile
import unittest


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT_PATH = os.path.join(REPO_ROOT, "scripts", "check_spec_anchors.py")


def load_checker():
    spec = importlib.util.spec_from_file_location("check_spec_anchors", SCRIPT_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def write_file(root, rel, content):
    full = os.path.join(root, rel)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8") as f:
        f.write(content)


def make_valid_fixture(root):
    spec_text = "\n".join(
        [
            "- `[rule:SPEC-GEN-01]` gen (`tests/conformance/g.zan`)",
            "- `[rule:SPEC-ARC-01]` arc `E1016` (`tests/selfhost/reject/r.zan`)",
            "- `[rule:SPEC-ASYNC-01]` async (`tests/conformance/a.zan`)",
        ]
    )
    write_file(root, "docs/SPEC.md", spec_text + "\n")
    write_file(root, "docs/SPEC.zh-CN.md", spec_text + "\n")
    write_file(
        root,
        "docs/DIAGNOSTIC_CODES.md",
        "| 号 | 级 | 说明 |\n|---|---|---|\n| E1016 | E | no dtor |\n",
    )
    write_file(root, "tests/conformance/g.zan", "class M { static void Main() {} }\n")
    write_file(root, "tests/conformance/g.out", "")
    write_file(root, "tests/conformance/a.zan", "class M { static void Main() {} }\n")
    write_file(root, "tests/conformance/a.out", "")
    write_file(root, "tests/selfhost/reject/r.zan", "class B { ~B() {} }\n")
    write_file(root, "tests/selfhost/reject/r.stderr", "E1016: destructors not supported\n")


class SpecAnchorsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = load_checker()

    def test_live_repo_clean(self):
        findings, summary = self.mod.check_repo(REPO_ROOT)
        self.assertEqual(findings, [], msg="\n".join(findings))
        self.assertGreaterEqual(len(summary["rules"]), 15)
        self.assertIn("SPEC-GEN-01", summary["rules"])
        self.assertIn("SPEC-ARC-01", summary["rules"])
        self.assertIn("SPEC-ASYNC-01", summary["rules"])
        self.assertIn("E1016", summary["diagnostics_to_rules"])
        self.assertIn("W2111", summary["diagnostics_to_rules"])

    def test_synthetic_clean(self):
        with tempfile.TemporaryDirectory() as td:
            make_valid_fixture(td)
            findings, _ = self.mod.check_repo(td)
            self.assertEqual(findings, [])

    def test_missing_test_file_fails(self):
        with tempfile.TemporaryDirectory() as td:
            make_valid_fixture(td)
            os.remove(os.path.join(td, "tests/conformance/g.zan"))
            findings, _ = self.mod.check_repo(td)
            self.assertTrue(any("missing on disk" in f for f in findings), findings)

    def test_missing_oracle_fails(self):
        with tempfile.TemporaryDirectory() as td:
            make_valid_fixture(td)
            os.remove(os.path.join(td, "tests/conformance/a.out"))
            findings, _ = self.mod.check_repo(td)
            self.assertTrue(any("companion oracle" in f for f in findings), findings)

    def test_en_zh_drift_fails(self):
        with tempfile.TemporaryDirectory() as td:
            make_valid_fixture(td)
            write_file(
                td,
                "docs/SPEC.zh-CN.md",
                "- `[rule:SPEC-GEN-01]` gen (`tests/conformance/g.zan`)\n"
                "- `[rule:SPEC-ASYNC-01]` async (`tests/conformance/a.zan`)\n",
            )
            findings, _ = self.mod.check_repo(td)
            self.assertTrue(any("SPEC-ARC-01" in f and "missing" in f for f in findings), findings)

    def test_duplicate_rule_fails(self):
        with tempfile.TemporaryDirectory() as td:
            make_valid_fixture(td)
            with open(os.path.join(td, "docs/SPEC.md"), "a", encoding="utf-8") as f:
                f.write("- `[rule:SPEC-GEN-01]` dup (`tests/conformance/g.zan`)\n")
            findings, _ = self.mod.check_repo(td)
            self.assertTrue(any("duplicate rule SPEC-GEN-01" in f for f in findings), findings)

    def test_unknown_diag_code_fails(self):
        with tempfile.TemporaryDirectory() as td:
            make_valid_fixture(td)
            write_file(
                td,
                "docs/DIAGNOSTIC_CODES.md",
                "| 号 | 级 | 说明 |\n|---|---|---|\n| E0001 | E | other |\n",
            )
            findings, _ = self.mod.check_repo(td)
            self.assertTrue(any("E1016" in f and "not found" in f for f in findings), findings)

    def test_reject_stderr_diag_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as td:
            make_valid_fixture(td)
            write_file(td, "tests/selfhost/reject/r.stderr", "E9999: unrelated error\n")
            findings, _ = self.mod.check_repo(td)
            self.assertTrue(any("does not mention" in f for f in findings), findings)


if __name__ == "__main__":
    unittest.main()

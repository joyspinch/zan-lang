"""Unit & live-repo protocol tests for scripts/run_cross_platform_matrix.py (S33)."""

import hashlib
import importlib.util
import json
import os
import tempfile
import unittest


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT_PATH = os.path.join(REPO_ROOT, "scripts", "run_cross_platform_matrix.py")


def load_runner():
    spec = importlib.util.spec_from_file_location("run_cross_platform_matrix", SCRIPT_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def write_manifest(path, os_name, cases, link_fail_closed=True):
    ordered = {k: cases[k] for k in sorted(cases.keys())}
    payload = json.dumps(ordered, sort_keys=True, separators=(",", ":")).encode("utf-8")
    obj = {
        "schema_version": 1,
        "os": os_name,
        "case_count": len(ordered),
        "matrix_sha256": hashlib.sha256(payload).hexdigest(),
        "link_fail_closed": link_fail_closed,
        "cases": ordered,
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f)


class CrossPlatformMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = load_runner()

    def test_live_repo_matrix_clean_and_covers_spec(self):
        matrix_path = os.path.join(REPO_ROOT, "tests", "cross_platform_matrix.txt")
        findings, cases = self.mod.check_matrix(REPO_ROOT, matrix_path)
        self.assertEqual(findings, [], msg="\n".join(findings))
        self.assertGreaterEqual(len(cases), 30)
        self.assertIn("generic_dual_path_matrix", cases)

    def test_compare_identical_three_os_passes(self):
        cases = {
            "a": {"exit_code": 0, "bytes": 4, "stdout_sha256": "1" * 64},
            "b": {"exit_code": 0, "bytes": 9, "stdout_sha256": "2" * 64},
        }
        with tempfile.TemporaryDirectory() as td:
            paths = []
            for os_name in ("linux", "macos", "windows"):
                p = os.path.join(td, f"{os_name}.json")
                write_manifest(p, os_name, cases)
                paths.append(p)
            findings, summary = self.mod.compare_manifests(
                paths, required_oses=["linux", "macos", "windows"]
            )
            self.assertEqual(findings, [])
            self.assertEqual(summary["case_count"], 2)
            self.assertEqual(summary["oses"], ["linux", "macos", "windows"])

    def test_compare_detects_single_byte_divergence(self):
        c1 = {"a": {"exit_code": 0, "bytes": 4, "stdout_sha256": "a" * 64}}
        c2 = {"a": {"exit_code": 0, "bytes": 4, "stdout_sha256": "b" * 64}}
        with tempfile.TemporaryDirectory() as td:
            p1 = os.path.join(td, "linux.json")
            p2 = os.path.join(td, "windows.json")
            write_manifest(p1, "linux", c1)
            write_manifest(p2, "windows", c2)
            findings, _ = self.mod.compare_manifests([p1, p2])
            self.assertTrue(any("byte divergence" in f for f in findings), findings)

    def test_compare_rejects_missing_required_os(self):
        c = {"a": {"exit_code": 0, "bytes": 4, "stdout_sha256": "a" * 64}}
        with tempfile.TemporaryDirectory() as td:
            p1 = os.path.join(td, "linux.json")
            p2 = os.path.join(td, "windows.json")
            write_manifest(p1, "linux", c)
            write_manifest(p2, "windows", c)
            findings, _ = self.mod.compare_manifests(
                [p1, p2], required_oses=["linux", "macos", "windows"]
            )
            self.assertTrue(any("macos" in f for f in findings), findings)

    def test_compare_rejects_link_probe_failure(self):
        c = {"a": {"exit_code": 0, "bytes": 4, "stdout_sha256": "a" * 64}}
        with tempfile.TemporaryDirectory() as td:
            p1 = os.path.join(td, "linux.json")
            p2 = os.path.join(td, "windows.json")
            write_manifest(p1, "linux", c, link_fail_closed=True)
            write_manifest(p2, "windows", c, link_fail_closed=False)
            findings, _ = self.mod.compare_manifests([p1, p2])
            self.assertTrue(any("link_fail_closed" in f for f in findings), findings)

    def test_compare_rejects_missing_case(self):
        c1 = {
            "a": {"exit_code": 0, "bytes": 4, "stdout_sha256": "a" * 64},
            "b": {"exit_code": 0, "bytes": 4, "stdout_sha256": "b" * 64},
        }
        c2 = {"a": {"exit_code": 0, "bytes": 4, "stdout_sha256": "a" * 64}}
        with tempfile.TemporaryDirectory() as td:
            p1 = os.path.join(td, "linux.json")
            p2 = os.path.join(td, "macos.json")
            write_manifest(p1, "linux", c1)
            write_manifest(p2, "macos", c2)
            findings, _ = self.mod.compare_manifests([p1, p2])
            self.assertTrue(any("missing cases" in f for f in findings), findings)


if __name__ == "__main__":
    unittest.main()

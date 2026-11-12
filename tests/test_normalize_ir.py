"""Standalone raw function fingerprint contract tests (no compiler required)."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "normalize_ir.py"
FUNCTION = "define i32 @sample(i32 %x) {\nentry:\n  ret i32 %x\n}"


class RawFingerprintTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "_scratch"
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="fnhash-test-", dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "input.ll"
        self.output = self.directory / "output.hashes"

    def run_hash(self, data):
        self.source.write_bytes(data)
        return subprocess.run(
            [sys.executable, "-I", str(SCRIPT), "--fnhash",
             str(self.source), str(self.output)],
            capture_output=True, timeout=15)

    def test_original_bytes_and_sorting(self):
        second = "define void @other() {\n  ret void\n}"
        text = "; module comment\n" + FUNCTION + "\n\n" + second + "\n"
        result = self.run_hash(text.replace("\n", "\r\n").encode("utf-8"))
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = sorted(hashlib.sha256(body.encode()).hexdigest()[:16] + " " + name
                      for body, name in [(FUNCTION, "sample"), (second, "other")])
        self.assertEqual(self.output.read_bytes(), ("\n".join(rows) + "\n").encode())

    def test_raw_rename_is_sensitive(self):
        self.assertEqual(self.run_hash(FUNCTION.encode()).returncode, 0)
        original = self.output.read_bytes()
        renamed = FUNCTION.replace("%x", "%renamed").replace("entry:", "renamed:")
        self.assertEqual(self.run_hash(renamed.encode()).returncode, 0)
        self.assertNotEqual(original, self.output.read_bytes())

    def test_invalid_inputs_do_not_publish_partial_output(self):
        invalid = {
            "encoding": FUNCTION.encode() + b"\n; invalid \xff",
            "duplicate": (FUNCTION + "\n" + FUNCTION).encode(),
            "unterminated": (FUNCTION + "\ndefine void @broken() {\n  ret void").encode(),
            "nested": ("define void @broken() {\n" + FUNCTION).encode(),
            "header": b"define not-a-function\n}\n",
            "empty": b"; no definitions\n",
        }
        for name, data in invalid.items():
            for existing in (False, True):
                with self.subTest(name=name, existing=existing):
                    self.output.unlink(missing_ok=True)
                    if existing:
                        self.output.write_bytes(b"previous complete output\n")
                    result = self.run_hash(data)
                    self.assertNotEqual(result.returncode, 0)
                    if existing:
                        self.assertEqual(self.output.read_bytes(), b"previous complete output\n")
                    else:
                        self.assertFalse(self.output.exists())
                    self.assertEqual(sorted(p.name for p in self.directory.iterdir()),
                                     ["input.ll", "output.hashes"] if existing else ["input.ll"])


if __name__ == "__main__":
    unittest.main()

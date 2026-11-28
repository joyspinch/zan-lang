"""Unit tests for scripts/fuzz_mutate.py (B-ID186 seed-mutation fuzzer)."""

import importlib.util
import os
import random
import tempfile
import unittest


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT_PATH = os.path.join(REPO_ROOT, "scripts", "fuzz_mutate.py")


def load_fuzzer():
    spec = importlib.util.spec_from_file_location("fuzz_mutate", SCRIPT_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FuzzMutateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = load_fuzzer()

    def test_nullin_balances_parens_and_generics(self):
        lines = [
            "using System;",
            "class M {",
            "    static void Main() {",
            "        var b = new Box<int>(1, Foo(2, 3));",
            "        var a = new int[4] { 1, 2, 3, 4 };",
            "        throw new Exception(\"boom\");",
            "    }",
            "}",
        ]
        seen = set()
        for seed in range(20):
            out = self.mod._mut_nullin(list(lines), random.Random(seed))
            self.assertIsNotNone(out)
            text = "\n".join(out)
            # Must never leave dangling `)` or `]` from broken prefix-only replacement
            self.assertEqual(text.count("("), text.count(")"), text)
            self.assertEqual(text.count("["), text.count("]"), text)
            self.assertEqual(text.count("{"), text.count("}"), text)
            # Must never mutate `throw new Exception(...)` to `throw null`
            self.assertIn('throw new Exception("boom");', text)
            seen.add(text)
        self.assertTrue(any("var b = null;" in s for s in seen))
        self.assertTrue(any("var a = null;" in s for s in seen))

    def test_fplit_covers_nan_and_infinities(self):
        self.assertIn("double.NaN", self.mod.FP_BOUNDARIES)
        self.assertIn("double.PositiveInfinity", self.mod.FP_BOUNDARIES)
        self.assertIn("double.NegativeInfinity", self.mod.FP_BOUNDARIES)
        lines = ["class M { static void Main() { double x = 3.14; } }"]
        produced = set()
        for seed in range(100):
            out = self.mod._mut_fplit(list(lines), random.Random(seed))
            self.assertIsNotNone(out)
            produced.add(out[0])
        self.assertTrue(any("double.NaN" in s for s in produced))
        self.assertTrue(any("double.PositiveInfinity" in s for s in produced))
        self.assertTrue(any("double.NegativeInfinity" in s for s in produced))

    def test_castw_flips_narrow_and_preserves_arrays_and_main(self):
        lines = [
            "class M {",
            "    static int Main() {",
            "        int[] arr = new int[2];",
            "        short s = 7;",
            "        double d = 1.5;",
            "        return 0;",
            "    }",
            "}",
        ]
        saw_narrow = False
        saw_float = False
        for seed in range(40):
            out = self.mod._mut_castw(list(lines), random.Random(seed))
            self.assertIsNotNone(out)
            self.assertIn("static int Main()", out[1])
            self.assertIn("int[] arr = new int[2];", out[2])
            if out[3] != lines[3]:
                saw_narrow = True
            if "float d = 1.5;" in out[4]:
                saw_float = True
        self.assertTrue(saw_narrow)
        self.assertTrue(saw_float)

    def test_try_strip_unwraps_catch_and_skips_finally(self):
        lines = [
            "class M {",
            "    static void Main() {",
            "        try {",
            "            int x = 1 / 0;",
            "            Console.WriteLine(x);",
            "        } catch (Exception e) {",
            "            Console.WriteLine(e.Message);",
            "        }",
            "    }",
            "}",
        ]
        out = self.mod._mut_try_strip(list(lines), random.Random(1))
        self.assertIsNotNone(out)
        text = "\n".join(out)
        self.assertNotIn("try {", text)
        self.assertNotIn("catch", text)
        self.assertIn("int x = 1 / 0;", text)
        self.assertIn("Console.WriteLine(x);", text)

        # Must skip try/finally so cleanup semantics are not broken accidentally
        with_finally = [
            "class M {",
            "    static void Main() {",
            "        try {",
            "            int x = 1;",
            "        } finally {",
            "            int y = 2;",
            "        }",
            "    }",
            "}",
        ]
        self.assertIsNone(self.mod._mut_try_strip(list(with_finally), random.Random(1)))

    def test_cli_deterministic_under_seed(self):
        seed_src = (
            "using System;\n"
            "class M {\n"
            "    static void Main() {\n"
            "        int x = 42;\n"
            "        double y = 3.14;\n"
            "        Console.WriteLine(x + y);\n"
            "    }\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as td:
            src_path = os.path.join(td, "seed.zan")
            with open(src_path, "w", encoding="utf-8") as f:
                f.write(seed_src)
            out1 = os.path.join(td, "run1")
            out2 = os.path.join(td, "run2")
            rc1 = self.mod.main([src_path, "5", "--seed", "123", "--out", out1])
            rc2 = self.mod.main([src_path, "5", "--seed", "123", "--out", out2])
            self.assertEqual(rc1, 0)
            self.assertEqual(rc2, 0)
            for k in range(1, 6):
                with open(os.path.join(out1, f"seed_m{k}.zan"), encoding="utf-8") as fa:
                    a = fa.read()
                with open(os.path.join(out2, f"seed_m{k}.zan"), encoding="utf-8") as fb:
                    b = fb.read()
                self.assertEqual(a, b)


if __name__ == "__main__":
    unittest.main()

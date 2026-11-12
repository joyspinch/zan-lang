#!/usr/bin/env python3
"""Compiler-free checks: python -I tests/test_run_templates.py.

All generated fixtures, logs and fake artifacts live in a unique _scratch
subdirectory and are removed on completion. No build, CTest or lock is used.
"""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "tests/run_templates.cmake"
FAKE_COMPILER = '''import json
from pathlib import Path
import sys
import time

args = sys.argv[1:]
source = Path(args[0]).read_text(encoding="utf-8")
print(json.dumps(args), flush=True)
print("fake compiler stderr", file=sys.stderr, flush=True)
if "TRACK" in source:
    trace = Path(__file__).with_name("concurrency.jsonl")
    with trace.open("a") as stream:
        stream.write(json.dumps({"event": "start", "time": time.time()}) + "\\n")
    time.sleep(0.4)
    with trace.open("a") as stream:
        stream.write(json.dumps({"event": "end", "time": time.time()}) + "\\n")
if "SLOW" in source:
    time.sleep(20)
if "FAIL" in source:
    sys.exit(7)
if "NO_ARTIFACT" in source:
    sys.exit(0)
Path(args[args.index("-o") + 1]).write_text(
    "" if "EMPTY_ARTIFACT" in source else "fake artifact", encoding="utf-8"
)
'''


class TemplateGateTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "_scratch"
        scratch.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="template-harness-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        # Exercise spaces in both scaffolded inputs and the compiler command.
        self.templates = self.base / "template fixtures"
        self.work = self.base / "work results"
        compiler = self.base / "fake compiler.py"
        compiler.write_text(FAKE_COMPILER, encoding="utf-8")
        self.compiler = ";".join((Path(sys.executable).as_posix(), "-I", compiler.as_posix()))
        self.cmake = shutil.which("cmake")
        self.assertIsNotNone(self.cmake, "cmake must be on PATH")

    def project(self, name, source="PASS", manifest=None, external=False):
        directory = (self.base / name) if external else (self.templates / name)
        (directory / "src").mkdir(parents=True)
        (directory / "zan.proj").write_text(
            manifest or "name = {{NAME}}\ntype = console\ntarget = exe\nentry = src/main.zan\n",
            encoding="utf-8",
        )
        (directory / "src/main.zan").write_text(source + " {{NAME}}", encoding="utf-8")
        return directory / "zan.proj"

    def run_gate(self, filter=None, extra=None, timeout=5, compiler=None):
        command = [
            self.cmake,
            f"-DZANC={compiler or self.compiler}",
            f"-DTEMPLATES={self.templates.as_posix()}",
            f"-DWORK={self.work.as_posix()}",
            "-DEXE_EXT=.exe",
            f"-DCOMPILE_TIMEOUT={timeout}",
        ]
        if filter is not None:
            command.append(f"-DFILTER={filter}")
        if extra:
            command.append("-DEXTRA_PROJECTS=" + ";".join(p.as_posix() for p in extra))
        return subprocess.run(
            command + ["-P", str(GATE)], capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=15, cwd=ROOT,
        )

    def run_acceptance(self, *extra):
        command = [sys.executable, "-I", str(ROOT / "tests/run_template_acceptance.py"),
                   "--root", str(self.base), "--output", str(self.work),
                   "--zanc", self.compiler, "--gate", str(GATE),
                   "--timeout", "5", "--outer-timeout", "10", *extra]
        return subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", timeout=20, cwd=ROOT)

    def acceptance_fixtures(self, sources):
        self.templates = self.base / "templates"
        for name, source in sources:
            self.project(name, source)
        self.project("examples/html_gallery", "PASS", external=True)

    def test_acceptance_two_workers_and_journal(self):
        self.acceptance_fixtures([(f"console/item{i}", "TRACK") for i in range(4)])
        self.assert_exit(self.run_acceptance(), True)
        data = self.results()
        self.assertEqual(data["workers"], 2)
        self.assertEqual(data["selected"], 5)
        self.assertTrue(data["complete"])
        self.assertEqual({r["status"] for r in data["rows"]}, {"pass"})
        self.assertEqual(len({r["work"] for r in data["rows"]}), 5)
        events = [json.loads(line) for line in (self.base / "concurrency.jsonl").read_text().splitlines()]
        active = peak = 0
        for event in sorted(events, key=lambda e: e["time"]):
            active += 1 if event["event"] == "start" else -1
            peak = max(peak, active)
        self.assertEqual(active, 0)
        self.assertEqual(peak, 2)
        journal = [json.loads(line) for line in (self.work / "journal.jsonl").read_text().splitlines()]
        self.assertEqual(len(journal), 10)
        self.assertEqual(sum(e["status"] == "pass" for e in journal), 5)

    def test_acceptance_failures_and_exact_filter(self):
        self.acceptance_fixtures([("game/snake", "NO_ARTIFACT"),
                                  ("game/snake-extra", "PASS")])
        self.assert_exit(self.run_acceptance("--filter", "^game/snake$"), False)
        rows = self.results()["rows"]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["template"], "game/snake")
        self.assertEqual(rows[0]["status"], "fail")
        self.assertIn("artifact is missing", rows[0]["detail"])

    def test_acceptance_outer_timeout(self):
        self.acceptance_fixtures([("game/slow", "SLOW")])
        self.assert_exit(self.run_acceptance("--filter", "^game/slow$",
                                             "--outer-timeout", "0.5"), False)
        row = self.results()["rows"][0]
        self.assertEqual(row["status"], "timeout")
        self.assertIn("outer process timeout", row["detail"])

    def test_acceptance_zero_selection_and_worker_limit(self):
        self.acceptance_fixtures([("console/one", "PASS")])
        for arguments in (("--filter", "^missing$"), ("--workers", "3")):
            self.assert_exit(self.run_acceptance(*arguments), False)
            self.assertFalse(self.work.exists())

    def results(self):
        return json.loads((self.work / "results.json").read_text(encoding="utf-8"))

    def assert_exit(self, result, success):
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)

    def assert_row(self, row, status):
        self.assertEqual(row["status"], status)
        directory = Path(row["stdout"]).parent
        self.assertEqual(json.loads((directory / "result.json").read_text()), row)
        self.assertTrue(Path(row["stdout"]).is_file())
        self.assertTrue(Path(row["stderr"]).is_file())

    def test_default_all_and_extra_projects(self):
        self.project("console/one")
        self.project("console/two")
        extra = self.project("html_gallery", external=True)
        result = self.run_gate(extra=[extra])
        self.assert_exit(result, True)
        data = self.results()
        self.assertTrue(data["complete"])
        self.assertEqual(data["selected"], 3)
        self.assertEqual([r["template"] for r in data["rows"]],
                         ["console/one", "console/two", "html_gallery"])
        for row in data["rows"]:
            self.assert_row(row, "pass")
            self.assertTrue(Path(row["artifact"]).exists())

    def test_exact_filter_preserves_unrelated_work(self):
        self.project("game/snake")
        self.project("game/snake-extra", "FAIL")
        unrelated = self.work / "game_snake-extra/sentinel.txt"
        unrelated.parent.mkdir(parents=True)
        unrelated.write_text("keep")
        result = self.run_gate(filter="^game/snake$")
        self.assert_exit(result, True)
        self.assertEqual(unrelated.read_text(), "keep")
        self.assertEqual([r["template"] for r in self.results()["rows"]], ["game/snake"])
        # Re-selecting resets that project's stale files, without deleting others.
        stale = self.work / "game_snake/stale.txt"
        stale.write_text("remove")
        self.assert_exit(self.run_gate(filter="^game/snake$"), True)
        self.assertFalse(stale.exists())
        self.assertEqual(unrelated.read_text(), "keep")

    def test_group_regex_filters_extra_projects_too(self):
        self.project("gui/one")
        self.project("gui/two")
        self.project("server/failing", "FAIL")
        extra = self.project("html_gallery", "FAIL", external=True)
        self.assert_exit(self.run_gate(filter="^gui/", extra=[extra]), True)
        self.assertEqual(self.results()["selected"], 2)
        # EXTRA_PROJECTS can be selected on their own by basename.
        self.assert_exit(self.run_gate(filter="^html_gallery$", extra=[extra]), False)
        self.assertEqual(self.results()["rows"][0]["template"], "html_gallery")

    def test_zero_match_is_failure_without_cleanup(self):
        self.project("game/snake")
        sentinel = self.work / "game_snake/sentinel.txt"
        sentinel.parent.mkdir(parents=True)
        sentinel.write_text("keep")
        result = self.run_gate(filter="^Game/snake$")
        self.assert_exit(result, False)
        self.assertIn("no templates matched", result.stderr)
        self.assertEqual(sentinel.read_text(), "keep")
        self.assertEqual(self.results()["rows"], [])
        self.assertFalse(self.results()["complete"])

    def test_nonzero_logs_and_continues(self):
        self.project("console/a-fail", "FAIL")
        self.project("console/b-pass")
        self.assert_exit(self.run_gate(), False)
        data = self.results()
        self.assertTrue(data["complete"])
        failed, passed = data["rows"]
        self.assert_row(failed, "fail")
        self.assertEqual(failed["result"], "7")
        self.assertIn("fake compiler stderr", Path(failed["stderr"]).read_text())
        self.assertIn("-o", Path(failed["stdout"]).read_text())
        self.assert_row(passed, "pass")

    def test_zero_exit_requires_nonempty_artifact(self):
        self.project("console/a-missing", "NO_ARTIFACT")
        self.project("console/b-empty", "EMPTY_ARTIFACT")
        self.project("console/c-valid")
        self.assert_exit(self.run_gate(), False)
        data = self.results()
        self.assertTrue(data["complete"])
        missing, empty, valid = data["rows"]
        for row, reason in ((missing, "missing"), (empty, "empty")):
            self.assert_row(row, "fail")
            self.assertIn("compiler exited 0", row["result"])
            self.assertIn(reason, row["result"])
            self.assertIn(row["result"], Path(row["stderr"]).read_text())
        self.assertFalse(Path(missing["artifact"]).exists())
        self.assertEqual(Path(empty["artifact"]).stat().st_size, 0)
        self.assert_row(valid, "pass")
        self.assertGreater(Path(valid["artifact"]).stat().st_size, 0)

    def test_timeout_is_failure_and_keeps_partial_output(self):
        self.project("game/a-slow", "SLOW")
        self.project("game/b-pass")
        self.assert_exit(self.run_gate(timeout=0.5), False)
        slow, passed = self.results()["rows"]
        self.assert_row(slow, "timeout")
        self.assertIn("timeout", slow["result"].lower())
        self.assertIn("-o", Path(slow["stdout"]).read_text())
        self.assertIn("fake compiler stderr", Path(slow["stderr"]).read_text())
        self.assert_row(passed, "pass")

    def test_launch_error_is_failure(self):
        self.project("console/one")
        self.assert_exit(self.run_gate(compiler=(self.base / "missing-zanc").as_posix()), False)
        self.assert_row(self.results()["rows"][0], "fail")

    def test_invalid_timeout_rejected(self):
        self.project("console/one")
        for value in (0, -1, "invalid"):
            with self.subTest(timeout=value):
                result = self.run_gate(timeout=value)
                self.assert_exit(result, False)
                self.assertIn("COMPILE_TIMEOUT must be", result.stderr)

    def test_manifest_validation_is_persisted(self):
        self.project("console/bad-entry", manifest="type=console\nentry=src/missing.zan\n")
        self.assert_exit(self.run_gate(), False)
        row = self.results()["rows"][0]
        self.assert_row(row, "fail")
        self.assertIn("does not exist", row["result"])
        self.assertEqual(Path(row["stdout"]).read_text(), "")

    def test_gui_scaffolding_entry_order_and_dll_target(self):
        project = self.project("gui/one", manifest=(
            "name={{NAME}};type=gui;target=dll;entry=src/{{NAME}}.zan\n"
        ))
        (project.parent / "src/main.zan").rename(project.parent / "src/App.zan")
        (project.parent / "src/AFirst.zan").write_text("helper")
        self.assert_exit(self.run_gate(), True)
        row = self.results()["rows"][0]
        self.assert_row(row, "pass")
        args = json.loads(Path(row["stdout"]).read_text())
        self.assertTrue(args[0].endswith("/src/TplProbe.zan"))
        self.assertIn("--emit-lib", args)
        self.assertTrue(row["artifact"].endswith("/out.dll"))
        self.assertEqual(Path(args[0]).read_text(), "PASS TplProbe")


if __name__ == "__main__":
    unittest.main(verbosity=2)

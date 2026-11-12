"""Pure Python checks; no Zan compiler or network fixture is invoked.

Run: python -I tests/integration/test_net_interop_harness.py
"""
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

sys.dont_write_bytecode = True
SOURCE = Path(__file__).with_name("net_interop_test.py")
spec = importlib.util.spec_from_file_location("net_interop_harness", SOURCE)
net = importlib.util.module_from_spec(spec)
spec.loader.exec_module(net)


class HarnessSafetyTests(unittest.TestCase):
    def setUp(self):
        output = net.ROOT / "build" / "test-out"
        output.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="net-harness-unit-", dir=output)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.harness = object.__new__(net.Harness)
        self.harness.output = self.directory
        self.harness.args = SimpleNamespace(timeout=10, loops=1)
        self.harness.add = lambda name, status, detail, **extra: {
            "case": name, "status": status, "detail": detail, **extra}

    def test_command_collects_complete_output(self):
        code, text, evidence = self.harness.command(
            "normal", [sys.executable, "-I", "-c", "print('completed')"])
        self.assertEqual(code, 0)
        self.assertEqual(text.strip(), "completed")
        self.assertEqual(evidence["exit_code"], 0)
        self.assertEqual((self.directory / "normal.log").read_bytes(), text.encode("utf-8"))

    def test_timeout_kills_real_descendant_before_draining_output(self):
        # The descendant inherits stdout and writes a marker only if it survives.
        marker = self.directory / "descendant-survived"
        child = (
            "import pathlib,sys,time; print('descendant-ready',flush=True); "
            "time.sleep(3); pathlib.Path(sys.argv[1]).write_text('alive')"
        )
        parent = (
            "import subprocess,sys,time; "
            "subprocess.Popen([sys.executable,'-I','-c',sys.argv[1],sys.argv[2]]); "
            "time.sleep(30)"
        )
        started = time.monotonic()
        code, text, _ = self.harness.command(
            "timeout", [sys.executable, "-I", "-c", parent, child, marker], timeout=1)
        self.assertEqual(code, -1)
        self.assertIn("descendant-ready", text)
        self.assertIn("HARNESS TIMEOUT", text)
        self.assertLess(time.monotonic() - started, 6)
        time.sleep(2.3)
        self.assertFalse(marker.exists(), "descendant survived process-tree termination")

    def test_process_launch_isolates_descendants(self):
        with mock.patch.object(net.subprocess, "Popen") as launch, mock.patch.object(net, "WindowsJob") as job:
            net.start_process(["fixture"], env={"ZAN_RT_HARD": "1"})
        kwargs = launch.call_args.kwargs
        if os.name == "nt":
            self.assertEqual(kwargs["creationflags"], subprocess.CREATE_NEW_PROCESS_GROUP | 0x4)
            job.return_value.assign_and_resume.assert_called_once_with(launch.return_value)
        else:
            self.assertIs(kwargs["start_new_session"], True)
            job.assert_not_called()

    def test_posix_termination_uses_group(self):
        process = mock.Mock(pid=43210)
        with mock.patch.object(net.os, "name", "posix"), mock.patch.object(
                net.os, "killpg", create=True) as kill_group, mock.patch.object(
                net.signal, "SIGKILL", 9, create=True):
            net.terminate_process_tree(process)
        kill_group.assert_called_once_with(43210, 9)

    def test_windows_termination_closes_job_even_after_parent_exit(self):
        process = mock.Mock(pid=43210, returncode=0)
        with mock.patch.object(net.os, "name", "nt"):
            net.terminate_process_tree(process)
        process._interop_job.close.assert_called_once()
        process.poll.assert_not_called()

    def test_timeout_terminates_before_second_communicate(self):
        process = mock.Mock()
        order = []
        def communicate(timeout):
            order.append("communicate")
            if len(order) == 1:
                raise subprocess.TimeoutExpired("fixture", timeout)
            return b"final output", None
        process.communicate.side_effect = communicate
        with mock.patch.object(net, "terminate_process_tree", side_effect=lambda _p: order.append("killtree")):
            output, timed_out = net.finish_process(process, 1)
        self.assertEqual(order, ["communicate", "killtree", "communicate"])
        self.assertEqual(output, b"final output")
        self.assertTrue(timed_out)

    def test_exited_parent_cannot_leave_descendant_holding_stdout(self):
        marker = self.directory / "orphan-survived"
        child = (
            "import pathlib,sys,time; print('orphan-ready',flush=True); "
            "time.sleep(3); pathlib.Path(sys.argv[1]).write_text('alive')"
        )
        parent = (
            "import subprocess,sys; "
            "subprocess.Popen([sys.executable,'-I','-c',sys.argv[1],sys.argv[2]])"
        )
        process = net.start_process([sys.executable, "-I", "-c", parent, child, marker])
        deadline = time.monotonic() + 5
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertEqual(process.poll(), 0, "parent must exit before descendant cleanup")
        output, timed_out = net.finish_process(process, 0.2)
        self.assertTrue(timed_out)
        self.assertIn(b"orphan-ready", output)
        time.sleep(3.1)
        self.assertFalse(marker.exists(), "orphan outlived the Job/process group")

    def test_interrupt_and_error_cleanup_before_reraise(self):
        for error in (KeyboardInterrupt(), SystemExit(3), OSError("communicate failed")):
            with self.subTest(error=type(error).__name__):
                process = mock.Mock()
                order = []
                def communicate(timeout):
                    order.append("communicate")
                    if len(order) == 1:
                        raise error
                    return b"reaped", None
                process.communicate.side_effect = communicate
                with mock.patch.object(net, "terminate_process_tree", side_effect=lambda _p: order.append("killtree")):
                    with self.assertRaises(type(error)) as caught:
                        net.finish_process(process, 1)
                self.assertIs(caught.exception, error)
                self.assertEqual(order, ["communicate", "killtree", "communicate"])

    def test_real_interrupt_reaps_descendant(self):
        marker = self.directory / "interrupted-child-survived"
        child = "import pathlib,sys,time; time.sleep(3); pathlib.Path(sys.argv[1]).write_text('alive')"
        parent = (
            "import subprocess,sys,time; "
            "subprocess.Popen([sys.executable,'-I','-c',sys.argv[1],sys.argv[2]]); "
            "print('parent-ready',flush=True); time.sleep(30)"
        )
        process = net.start_process([sys.executable, "-I", "-c", parent, child, marker])
        self.assertIn(b"parent-ready", process.stdout.readline())
        original = process.communicate
        calls = 0
        def interrupt_once(timeout):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise KeyboardInterrupt()
            return original(timeout=timeout)
        with mock.patch.object(process, "communicate", side_effect=interrupt_once):
            with self.assertRaises(KeyboardInterrupt):
                net.finish_process(process, 1)
        self.assertIsNotNone(process.poll())
        time.sleep(3.1)
        self.assertFalse(marker.exists())

    def test_fixture_rejects_diagnostics_despite_success_result_and_exit(self):
        name = "binary_get"
        success = net.PREFIX + json.dumps({"case": name, "status": "pass", "detail": "ok"})
        for diagnostic in ("memory leak detected", "runtime error: bad access",
                           "Unhandled exception: IOException", "ARC guard violation"):
            with self.subTest(diagnostic=diagnostic):
                with mock.patch.object(self.harness, "command", return_value=(0, success + "\n" + diagnostic, {})) as command:
                    with mock.patch.dict(os.environ, {"ZAN_RT_HARD": "0"}):
                        result = self.harness.fixture("unused", name, 1, ZAN_RT_HARD="0")
                self.assertEqual(result["status"], "fail")
                self.assertTrue(result["runtime_diagnostics"])
                self.assertEqual(command.call_args.kwargs["env"]["ZAN_RT_HARD"], "1")

    def test_clean_fixture_passes_with_hard_runtime(self):
        text = net.PREFIX + json.dumps({"case": "binary_get", "status": "pass", "detail": "ok"})
        with mock.patch.object(self.harness, "command", return_value=(0, text, {})):
            result = self.harness.fixture("unused", "binary_get", 1)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["runtime_diagnostics"], [])

    def test_h2_shutdown_rejects_runtime_diagnostics(self):
        for diagnostic in ("MEMORY LEAK DETECTED", "Runtime Error: bad access",
                           "Unhandled exception", "Arc Guard violation"):
            with self.subTest(diagnostic=diagnostic):
                result = self.harness.server_shutdown_result(0, diagnostic, "unused.log")
                self.assertEqual(result["status"], "fail")
                self.assertTrue(result["runtime_diagnostics"])
        self.assertEqual(self.harness.server_shutdown_result(0, "", "unused.log")["status"], "pass")
        self.assertEqual(self.harness.server_shutdown_result(0, "", "unused.log", True)["status"], "fail")

    def test_h2_shutdown_interrupt_reaps_before_propagating(self):
        process = mock.Mock(returncode=0)
        process.poll.return_value = None
        order = []
        def control(_port, path):
            if path == "/shutdown":
                raise KeyboardInterrupt()
        with mock.patch.object(net, "start_process", return_value=process), mock.patch.object(
                net, "terminate_process_tree", side_effect=lambda _p: order.append("killtree")), mock.patch.object(
                net, "finish_process", side_effect=lambda _p, _t: (order.append("reaped") or (b"", False))), mock.patch.object(
                self.harness, "server_request", side_effect=control), mock.patch.object(net, "H2Peer"), mock.patch.object(
                self.harness, "h2_case"):
            self.harness.results = []
            with self.assertRaises(KeyboardInterrupt):
                self.harness.http2("unused")
        self.assertEqual(order, ["killtree", "reaped"])

    @unittest.skipUnless(os.name == "nt", "Windows suspended launch contract")
    def test_job_assignment_failure_never_resumes_child(self):
        process = mock.Mock()
        job = mock.Mock()
        job.assign_and_resume.side_effect = OSError("assignment denied")
        with mock.patch.object(net, "WindowsJob", return_value=job), mock.patch.object(
                net.subprocess, "Popen", return_value=process) as launch:
            with self.assertRaisesRegex(OSError, "assignment denied"):
                net.start_process(["unused"])
        self.assertTrue(launch.call_args.kwargs["creationflags"] & 0x4)
        job.close.assert_called_once()
        process.kill.assert_called_once()
        process.communicate.assert_called_once_with(timeout=10)

    def test_h2_launch_forces_hard_runtime_and_checks_shutdown(self):
        process = mock.Mock(returncode=0)
        process.poll.return_value = None
        # No networking: stub readiness/control and independent protocol peer.
        with mock.patch.dict(os.environ, {"ZAN_RT_HARD": "0"}), mock.patch.object(
                net, "start_process", return_value=process) as launch, mock.patch.object(
                net, "finish_process", return_value=(b"", False)), mock.patch.object(
                self.harness, "server_request"), mock.patch.object(net, "H2Peer"), mock.patch.object(
                self.harness, "h2_case"), mock.patch.object(self.harness, "server_shutdown_result") as shutdown:
            self.harness.results = []
            self.harness.http2("unused")
        self.assertEqual(launch.call_args.kwargs["env"]["ZAN_RT_HARD"], "1")
        self.assertEqual(shutdown.call_args.args[0], 0)
        self.assertFalse(shutdown.call_args.args[3])


if __name__ == "__main__":
    unittest.main(verbosity=2)

"""Validate actual Zig trace selection and recoverable runtime publication."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "stage_linux_sysroot", Path(__file__).resolve().parents[1] / "scripts/stage_linux_sysroot.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SysrootSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zan-sysroot-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "actual Zig cache with spaces"
        self.sources = []
        for index, name in enumerate(MODULE.REQUIRED):
            source = self.root / "o" / ("%032x" % (index + 1)) / name
            source.parent.mkdir(parents=True)
            source.write_bytes(b"runtime fixture")
            self.sources.append(source)

    def trace(self, sources=None):
        paths = self.sources if sources is None else sources
        # Zig prints raw argv, including native Windows backslashes and spaces.
        return "ignored compiler diagnostic with unmatched ' quote\nld.lld -static " + " ".join(
            str(path) for path in paths
        )

    def select(self, trace):
        return MODULE.selected_inputs(trace, self.root)

    def test_exact_inputs_with_raw_space_paths(self):
        self.assertIn(" ", str(self.root))
        self.assertEqual(self.select(self.trace()), dict(zip(MODULE.REQUIRED, self.sources)))

    def test_forward_slash_paths(self):
        trace = "ld.lld.exe -static " + " ".join(path.as_posix() for path in self.sources)
        self.assertEqual(set(self.select(trace)), set(MODULE.REQUIRED))

    def test_quoted_absolute_linker_and_inputs(self):
        trace = '"/tools/with spaces/ld.lld" -static ' + " ".join(
            '"' + path.as_posix() + '"' for path in self.sources
        )
        self.assertEqual(set(self.select(trace)), set(MODULE.REQUIRED))

    def test_missing_input_fails(self):
        with self.assertRaisesRegex(ValueError, "did not supply"):
            self.select(self.trace(self.sources[:-1]))

    def test_duplicate_input_fails(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.select(self.trace(self.sources + self.sources[:1]))

    def test_nonexistent_input_fails(self):
        self.sources[0].unlink()
        with self.assertRaisesRegex(ValueError, "missing absolute"):
            self.select(self.trace())

    def test_multiple_link_commands_fail(self):
        with self.assertRaisesRegex(ValueError, "exactly one"):
            self.select(self.trace() + "\n" + self.trace())

    def test_wrong_cache_root_fails(self):
        with self.assertRaisesRegex(ValueError, "did not supply"):
            MODULE.selected_inputs(self.trace(), self.root / "other")

    def test_non_hex_cache_segment_fails(self):
        trace = self.trace().replace(self.sources[0].parent.name, "not-a-cache-hash")
        with self.assertRaisesRegex(ValueError, "did not supply"):
            self.select(trace)

    def test_filename_suffix_does_not_match(self):
        with self.assertRaisesRegex(ValueError, "did not supply"):
            self.select(self.trace().replace("crt1.o", "crt1.o.old"))

    def test_unselected_cache_file_does_not_fill_missing_input(self):
        # Existing cache entries cannot substitute for absent actual-link inputs.
        with self.assertRaisesRegex(ValueError, "did not supply"):
            self.select(self.trace(self.sources[1:]))


class RuntimePublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zan-runtime-install-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.staged = self.root / "staged"
        self.staged.mkdir()
        (self.staged / "libc.a").write_bytes(b"new libc")
        (self.staged / "zanrt_io.o").write_bytes(b"new io")
        (self.staged / "zig-sysroot-inputs.json").write_text("{}", encoding="utf-8")
        (self.staged / "zig-sysroot-link.log").write_text("trace", encoding="utf-8")
        self.target = self.root / "toolchain" / "linux-musl"
        self.target.mkdir(parents=True)
        (self.target / "libc.a").write_bytes(b"old libc")
        (self.target / "zanrt_io.o").write_bytes(b"old io")
        (self.target / "optional-thirdparty.a").write_bytes(b"user library")
        (self.target / "user-file.txt").write_bytes(b"user file")

    def snapshot(self):
        return {p.name: p.read_bytes() for p in self.target.iterdir()}

    def assert_no_install_leftovers(self):
        self.assertEqual(list(self.target.parent.iterdir()), [self.target])

    def test_success_preserves_optional_files_and_excludes_evidence(self):
        MODULE.publish_target(self.staged, self.target)
        self.assertEqual(self.snapshot(), {
            "libc.a": b"new libc", "zanrt_io.o": b"new io",
            "optional-thirdparty.a": b"user library", "user-file.txt": b"user file",
        })
        self.assert_no_install_leftovers()

    def test_first_install(self):
        MODULE.publish_target(self.staged, self.target.parent / "linux-arm64")
        self.assertEqual((self.target.parent / "linux-arm64" / "libc.a").read_bytes(), b"new libc")

    def test_copy_failure_leaves_original_target_untouched(self):
        previous = self.snapshot()
        copy = MODULE.shutil.copy2

        def fail_second_artifact(source, destination, *args, **kwargs):
            if Path(source) == self.staged / "zanrt_io.o":
                raise OSError("copy failed")
            return copy(source, destination, *args, **kwargs)

        with mock.patch.object(MODULE.shutil, "copy2", side_effect=fail_second_artifact):
            with self.assertRaisesRegex(OSError, "copy failed"):
                MODULE.publish_target(self.staged, self.target)
        self.assertEqual(self.snapshot(), previous)
        self.assert_no_install_leftovers()

    def test_install_rename_failure_restores_original_target(self):
        previous = self.snapshot()
        rename = Path.rename

        def fail_install(source, destination):
            if ".install." in source.name:
                raise OSError("install rename failed")
            return rename(source, destination)

        with mock.patch.object(Path, "rename", fail_install):
            with self.assertRaisesRegex(OSError, "install rename failed"):
                MODULE.publish_target(self.staged, self.target)
        self.assertEqual(self.snapshot(), previous)
        self.assert_no_install_leftovers()

    def test_interruption_after_rename_restores_original_target(self):
        previous = self.snapshot()
        rename = Path.rename

        def interrupt_install(source, destination):
            result = rename(source, destination)
            if ".install." in source.name:
                raise KeyboardInterrupt()
            return result

        with mock.patch.object(Path, "rename", interrupt_install):
            with self.assertRaises(KeyboardInterrupt):
                MODULE.publish_target(self.staged, self.target)
        self.assertEqual(self.snapshot(), previous)
        self.assert_no_install_leftovers()

    def test_failed_rollback_retains_identifiable_backup(self):
        previous = self.snapshot()
        rename = Path.rename

        def fail_install_and_rollback(source, destination):
            if ".install." in source.name or ".backup." in source.name:
                raise OSError("rename failed")
            return rename(source, destination)

        with mock.patch.object(Path, "rename", fail_install_and_rollback):
            with self.assertRaisesRegex(RuntimeError, "previous target retained at"):
                MODULE.publish_target(self.staged, self.target)
        backups = list(self.target.parent.glob(".linux-musl.backup.*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual({p.name: p.read_bytes() for p in backups[0].iterdir()}, previous)


if __name__ == "__main__":
    unittest.main()

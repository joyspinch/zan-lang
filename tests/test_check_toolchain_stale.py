"""Runtime source/CI trigger contracts; run with python -I -B, no build required."""
import os
from pathlib import Path
import re
import runpy
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
previous_directory = Path.cwd()
try:
    CHECK = runpy.run_path(str(ROOT / "scripts" / "check_toolchain_stale.py"))
finally:
    os.chdir(previous_directory)

FAMILIES = {
    "zanrt_io.o": ("src/runtime/rt_io.c", "RT_IO"),
    "zanrt_io_mt.o": ("src/runtime/rt_io.c", "RT_IO"),
    "zanrt_sync.o": ("src/runtime/rt_sync.c", "RT_SYNC"),
    "zanrt_file.o": ("src/runtime/rt_file.c", "RT_FILE"),
    "zanrt_timer.o": ("src/runtime/rt_timer.c", "RT_TIMER"),
    "zanrt_crash.o": ("src/runtime/rt_crash.c", "RT_CRASH"),
    "libzancrypto.a": ("src/accel/zan_hw_accel.c", "RT_CRYPTO"),
}


def included_sources(source):
    """Follow quoted includes, including conditional branches for all targets."""
    pending = [ROOT / source]
    include_directory = pending[0].parent
    seen = set()
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        text = path.read_text(encoding="utf-8")
        for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
            for candidate in (path.parent / name, include_directory / name):
                if candidate.is_file():
                    pending.append(candidate.resolve())
                    break
            else:
                raise AssertionError(f"unresolved quoted include in {path}: {name}")
    return {path.relative_to(ROOT).as_posix() for path in seen}


class RuntimeFreshnessTests(unittest.TestCase):
    def test_nested_implementation_expansion(self):
        scratch = ROOT / "_scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="rt-freshness-test-", dir=scratch) as temporary:
            directory = Path(temporary)
            nested = directory / "nested" / "deeper"
            nested.mkdir(parents=True)
            paths = [directory / "part.c", nested / "part.c", nested / "part.h"]
            for path in paths + [nested / "ignored.txt"]:
                path.write_text("", encoding="utf-8")
            self.assertEqual(CHECK["_implementation_sources"](directory.as_posix()),
                             sorted(path.as_posix() for path in paths))

    def test_included_implementations_and_headers_are_dependencies(self):
        for source, family in set(FAMILIES.values()):
            with self.subTest(source=source):
                self.assertFalse(included_sources(source) - set(CHECK[family]))

    def test_all_linux_bundles_use_expanded_dependencies(self):
        artifacts = {path: sources for path, sources, _ in CHECK["ARTIFACTS"]}
        for target in ("linux-musl", "linux-arm64", "linux-riscv64"):
            for artifact, (source, _) in FAMILIES.items():
                path = f"toolchain/{target}/{artifact}"
                with self.subTest(artifact=path):
                    self.assertIn(path, artifacts)
                    self.assertFalse(included_sources(source) - set(artifacts[path]))

    def test_cross_runtime_recipes_fail_fast_and_build_crash(self):
        for name in ("build_linux_rt.sh", "build_macos_rt.sh"):
            text = (ROOT / "scripts" / name).read_text(encoding="utf-8")
            commands = "\n".join(line for line in text.splitlines()
                                 if not line.lstrip().startswith("#"))
            commands = commands.replace("\\\n", " ")
            with self.subTest(recipe=name):
                self.assertRegex(commands, r"(?m)^set -eu$")
                self.assertNotRegex(commands, r"\|\|\s*(?:true|:|echo|printf)")
                self.assertRegex(commands,
                                 r'"\$ZIG" cc [^\n]*-c "\$RT/rt_crash.c" -o "\$out/zanrt_crash.o"')
                self.assertIn('-fPIC', commands)
                self.assertNotIn('cmake -B', commands)
                self.assertNotIn('git clone', commands)

    def test_cross_runtime_output_and_selection_contracts(self):
        linux = (ROOT / "scripts/build_linux_rt.sh").read_text(encoding="utf-8")
        macos = (ROOT / "scripts/build_macos_rt.sh").read_text(encoding="utf-8")
        self.assertIn('selection=${2:-all}', linux)
        self.assertIn('all|linux-musl|linux-arm64|linux-riscv64)', linux)
        self.assertIn('work="$ROOT/build/linux-runtime-staging"', linux)
        self.assertIn("trap 'rm -rf -- \"$stage\"' EXIT", linux)
        self.assertIn('"$out" "$ROOT/toolchain/$sub"', linux)
        self.assertIn('--zig-env "$stage/zig-env.json"', linux)
        self.assertIn('for pair in arm64:aarch64 x64:x86_64', macos)
        self.assertIn('out="$ROOT/toolchain/macos/$sub"', macos)
        self.assertIn('"$ROOT/toolchain/ios/arm64/zanrt_gui.o"', macos)

    def test_workflow_covers_dependencies_and_future_nested_splits(self):
        text = (ROOT / ".github" / "workflows" / "toolchain.yml").read_text(encoding="utf-8")
        leg = {"triggers": [CHECK["_pattern_to_regex"](pattern)
                            for pattern in CHECK["_extract_trigger_paths"](text)]}
        sources = {path for _, family in FAMILIES.values() for path in CHECK[family]}
        sources.update(("scripts/build_linux_rt.sh", "scripts/stage_linux_sysroot.py",
                        "scripts/check_toolchain_stale.py"))
        for directory in ("src/runtime/io", "src/runtime/sync", "src/runtime/file",
                          "src/runtime/timer", "src/runtime/crash", "src/accel/hw"):
            sources.update(f"{directory}/nested/deeper/future.{suffix}" for suffix in ("c", "h"))
        for source in sorted(sources):
            with self.subTest(source=source):
                self.assertTrue(CHECK["_leg_covers"](leg, source))


if __name__ == "__main__":
    unittest.main()

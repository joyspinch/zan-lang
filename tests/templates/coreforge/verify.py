#!/usr/bin/env python3
"""Compile and run Coreforge's native, headless logic suite."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build" / "coreforge-tests"
LOGIC_MODULES = ("Model.zan", "Factory.zan", "Combat.zan", "Simulation.zan", "Persistence.zan")
GUI_MODULES = {"main.zan", "Interface.zan", "Renderer.zan", "AutoTest.zan"}
ERROR_TEXT = re.compile(r"\berror\b|\bfatal\b|\bpanic\b|\bexception\b|COREFORGE_TESTS_FAIL|ASSERT_FAIL", re.IGNORECASE)
# The Zan runtime emits CRLF on Windows; tolerate it before the end anchor.
SUCCESS = re.compile(r"^COREFORGE_TESTS_OK assertions=([1-9][0-9]*)\r?$", re.MULTILINE)


def fingerprint(paths: list[Path]) -> dict[str, str]:
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}


def run(command: list[str], timeout: int, log: Path) -> subprocess.CompletedProcess[bytes]:
    try:
        result = subprocess.run(command, cwd=BUILD, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or b""
        log.write_bytes(output + b"\nVERIFY_TIMEOUT\n")
        raise RuntimeError(f"Timed out after {timeout}s; see {log}") from exc
    log.write_bytes(result.stdout)
    print(result.stdout.decode("utf-8", errors="replace"), end="")
    return result


def verify(compiler: Path, timeout: int) -> None:
    source_dir = ROOT / "templates" / "game" / "coreforge" / "src"
    sources = [source_dir / name for name in LOGIC_MODULES]
    sources.append(Path(__file__).with_name("logic.zan"))
    for path in [compiler, *sources]:
        if not path.is_file():
            raise RuntimeError(f"Required input is missing: {path}")
    extra = [path for path in source_dir.rglob("*.zan") if path not in sources and path.name not in GUI_MODULES]
    if extra:
        raise RuntimeError("Unclassified logical sources must be included explicitly: " + ", ".join(map(str, extra)))
    BUILD.mkdir(parents=True, exist_ok=True)
    saves = BUILD / "test-saves"
    saves.mkdir(exist_ok=True)
    executable = BUILD / ("coreforge-logic.exe" if os.name == "nt" else "coreforge-logic")
    # Delete the old executable before compiling: zanc may report diagnostics with exit status zero.
    executable.unlink(missing_ok=True)
    before = fingerprint(sources)
    started_ns = time.time_ns()
    result = run([str(compiler), *(str(path) for path in sources), "--auto-stdlib", "-o", str(executable)], timeout, BUILD / "compile.log")
    diagnostics = result.stdout.decode("utf-8", errors="replace")
    if result.returncode != 0 or ERROR_TEXT.search(diagnostics):
        raise RuntimeError(f"Compilation failed (exit {result.returncode}); see {BUILD / 'compile.log'}")
    if not executable.is_file() or executable.stat().st_size == 0:
        raise RuntimeError("Compiler produced no nonempty executable")
    if executable.stat().st_mtime_ns < started_ns - 2_000_000_000:
        raise RuntimeError("Compiler output timestamp is stale")
    if fingerprint(sources) != before:
        raise RuntimeError("Logical sources changed while compiling; rerun with an exclusive compile window")
    result = run([str(executable)], timeout, BUILD / "run.log")
    output = result.stdout.decode("utf-8", errors="replace")
    matches = SUCCESS.findall(output)
    if result.returncode != 0 or ERROR_TEXT.search(output) or len(matches) != 1:
        raise RuntimeError(f"Logic suite failed (exit {result.returncode}, success markers {len(matches)}); see {BUILD / 'run.log'}")
    if int(matches[0]) < 100:
        raise RuntimeError("Logic suite reported too few assertions; a test group may have been skipped")
    if fingerprint(sources) != before:
        raise RuntimeError("Logical sources changed while running; results do not describe the current files")
    print(f"COREFORGE_VERIFY_OK assertions={matches[0]} log={BUILD / 'run.log'}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("zanc.exe" if os.name == "nt" else "zanc"))
    parser.add_argument("--timeout", type=int, default=240, help="Per-process timeout in seconds")
    args = parser.parse_args()
    try:
        verify(args.compiler.resolve(), args.timeout)
    except (OSError, RuntimeError) as exc:
        print(f"COREFORGE_VERIFY_FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

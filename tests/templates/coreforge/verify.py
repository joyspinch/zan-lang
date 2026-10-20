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
# 包根：仓库内跑时就是仓库根；项目搬到仓库外后由 $ZAN_HOME / --package-project
# 指回 zan-lang 仓库（公共包是仓库的资产，不复制进项目——复制就会分叉）。
PACKAGE_ROOT = Path(os.environ.get("ZAN_HOME") or ROOT).resolve()
# 逻辑层已抽成包 packages/Zan.Game.Factory（namespace Game.Factory）：
# 测试源里一句 `using Game.Factory;` 就会把整个命名空间目录自动带进编译，
# 因此这里只需喂测试源本身，不必逐个列举包内文件。
TEMPLATE_SRC = ROOT / "templates" / "game" / "coreforge" / "src"
# 模板目录里只剩"皮肤"（渲染/界面/入口），它们不进逻辑测试。
GUI_MODULES = {
    "main.zan", "ForgeInterface.zan", "ForgeRenderer.zan", "ForgeCamera.zan",
    "CoreforgeHud.zan", "CoreforgeAutoTest.zan",
}
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


def verify(compiler: Path, timeout: int, package_root: Path) -> None:
    sources = [Path(__file__).with_name("logic.zan")]
    for path in [compiler, *sources]:
        if not path.is_file():
            raise RuntimeError(f"Required input is missing: {path}")
    # 包的命名空间目录必须存在且有源码：测试靠 `using Game.Factory;` 自动引入它。
    package_src = package_root / "packages" / "Zan.Game.Factory" / "src" / "Game" / "Factory"
    package_files = sorted(package_src.glob("*.zan"))
    if not package_files:
        raise RuntimeError(
            "Package sources are missing: %s（公共包住在 zan-lang 仓库的 packages/ 下："
            "用 --package-project 或 $ZAN_HOME 指过去）" % package_src)
    # 模板目录里不该再有没归类的逻辑源码——它们属于包，不属于模板。
    extra = [path for path in TEMPLATE_SRC.rglob("*.zan") if path.name not in GUI_MODULES]
    if extra:
        raise RuntimeError("Unclassified logical sources must live in the package: " + ", ".join(map(str, extra)))
    BUILD.mkdir(parents=True, exist_ok=True)
    saves = BUILD / "test-saves"
    saves.mkdir(exist_ok=True)
    executable = BUILD / ("coreforge-logic.exe" if os.name == "nt" else "coreforge-logic")
    # Delete the old executable before compiling: zanc may report diagnostics with exit status zero.
    executable.unlink(missing_ok=True)
    before = fingerprint(sources)
    started_ns = time.time_ns()
    result = run([str(compiler), *(str(path) for path in sources), "--auto-stdlib",
                  "--package-project", str(package_root), "-o", str(executable)],
                 timeout, BUILD / "compile.log")
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
    parser.add_argument("--package-project", type=Path, default=PACKAGE_ROOT,
                        help="zan-lang 仓库根：公共包在它的 packages/ 下（默认取 $ZAN_HOME）")
    args = parser.parse_args()
    try:
        verify(args.compiler.resolve(), args.timeout, args.package_project.resolve())
    except (OSError, RuntimeError) as exc:
        print(f"COREFORGE_VERIFY_FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

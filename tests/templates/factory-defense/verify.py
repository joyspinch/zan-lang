import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
TESTS = Path(__file__).resolve().parent
OUT = ROOT / "build" / "factory-defense-verify"


def run(command, name, cwd=ROOT, env=None, timeout=120):
    result = subprocess.run([str(arg) for arg in command], cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout)
    text = result.stdout.decode("utf-8", errors="replace")
    (OUT / (name + ".log")).write_text(text, encoding="utf-8")
    print(text, end="")
    if result.returncode or re.search(r"\berror(?:\s+[A-Z]*\d+)?\s*:", text, re.I):
        raise RuntimeError(f"{name} failed; see {OUT / (name + '.log')}")
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zanc", type=Path, default=ROOT / "build" / "zanc.exe")
    parser.add_argument("--gui", action="store_true")
    args = parser.parse_args()
    zanc = args.zanc.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="factory-defense-", dir=ROOT / "_scratch") as staging:
        templates = Path(staging) / "templates"
        destination = templates / "game" / "factory-defense"
        shutil.copytree(ROOT / "templates" / "game" / "factory-defense", destination)
        run(["cmake", f"-DZANC={zanc}", f"-DTEMPLATES={templates}",
             f"-DWORK={OUT / 'scaffold'}", f"-DSTDLIB={ROOT / 'stdlib'}",
             "-DEXE_EXT=.exe", f"-DDRIVER_DIR={ROOT / 'build'}",
             "-P", ROOT / "tests" / "run_templates.cmake"], "scaffold")
    project = OUT / "scaffold" / "game_factory-defense"
    source = project / "src"
    files = sorted(p for p in source.rglob("*.zan") if p.name != "main.zan")
    exe = project / "out.exe"
    if not exe.is_file() or "{{NAME}}" in (project / "zan.proj").read_text(encoding="utf-8"):
        raise AssertionError("Template scaffolding did not produce an instantiated executable")
    core = OUT / "core.exe"
    run([zanc, TESTS / "core.zan", *files, "--auto-stdlib", "--subsystem", "console",
         "--driver-dir", ROOT / "build", "-o", core], "core-build", cwd=project)
    text = run([core], "core-run", cwd=project, timeout=30)
    if "FACTORY_DEFENSE_CORE_PASS" not in text:
        raise AssertionError("Core tests did not reach their success marker")
    if args.gui:
        run([os.sys.executable, TESTS / "ui.py", exe], "ui-run", timeout=90)
    print("FACTORY_DEFENSE_STAGE1_PASS")


if __name__ == "__main__":
    main()

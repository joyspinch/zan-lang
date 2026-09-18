#!/usr/bin/env python3
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def normalized(data: str) -> str:
    return data.replace("\r\n", "\n").replace("\r", "\n")


def run(command, cwd: Path, timeout: int):
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
        return completed.returncode, normalized(completed.stdout), normalized(completed.stderr), False
    except subprocess.TimeoutExpired as exc:
        stdout = normalized(exc.stdout or "") if isinstance(exc.stdout, str) else ""
        stderr = normalized(exc.stderr or "") if isinstance(exc.stderr, str) else ""
        return None, stdout, stderr, True


def verify_csharp(case_dir: Path, scratch: Path, name: str, dotnet: str, expected: str):
    project = scratch / name / "csharp"
    project.mkdir(parents=True)
    (project / "Oracle.csproj").write_text(
        '<Project Sdk="Microsoft.NET.Sdk">\n'
        '  <PropertyGroup>\n'
        '    <OutputType>Exe</OutputType>\n'
        '    <TargetFramework>net10.0</TargetFramework>\n'
        '    <ImplicitUsings>disable</ImplicitUsings>\n'
        '    <Nullable>disable</Nullable>\n'
        '  </PropertyGroup>\n'
        '</Project>\n',
        encoding="utf-8",
    )
    shutil.copyfile(case_dir / f"{name}.cs", project / "Program.cs")
    rc, stdout, stderr, timed_out = run(
        [dotnet, "run", "--project", str(project / "Oracle.csproj"), "--configuration", "Release", "--nologo"],
        project,
        60,
    )
    if timed_out:
        return False, "C# oracle timed out"
    if rc != 0:
        return False, f"C# oracle exited {rc}\n{stdout}{stderr}"
    if stdout != expected:
        return False, f"checked-in expected output differs from C#\n--- expected\n{expected}--- C#\n{stdout}"
    return True, "C# oracle matches"


def verify_zan(case_dir: Path, scratch: Path, name: str, zanc: str, expected: str):
    case_scratch = scratch / name / "zan"
    case_scratch.mkdir(parents=True)
    exe = case_scratch / (name + (".exe" if sys.platform == "win32" else ""))
    rc, stdout, stderr, timed_out = run(
        [zanc, str(case_dir / f"{name}.zan"), "--auto-stdlib", "-o", str(exe)],
        case_dir,
        60,
    )
    if timed_out:
        return False, "Zan compile timed out"
    if rc != 0:
        return False, f"Zan compile exited {rc}\n{stdout}{stderr}"
    rc, stdout, stderr, timed_out = run([str(exe)], case_scratch, 5)
    if timed_out:
        return False, f"Zan program timed out\n{stdout}{stderr}"
    if rc != 0:
        return False, f"Zan program exited {rc}\n{stdout}{stderr}"
    if stdout != expected:
        return False, f"Zan output differs\n--- expected\n{expected}--- Zan\n{stdout}"
    return True, "Zan matches C#"


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare Zan behavior with checked-in C# oracle results")
    parser.add_argument("--dotnet", default="dotnet")
    parser.add_argument("--zanc", required=True)
    parser.add_argument("--require-zan-pass", action="store_true")
    args = parser.parse_args()

    case_dir = Path(__file__).resolve().parent
    repo = case_dir.parents[1]
    scratch = repo / "_scratch" / "csharp-compat"
    manifest = json.loads((case_dir / "manifest.json").read_text(encoding="utf-8"))
    zanc = str(Path(args.zanc).resolve())
    failures = []

    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    try:
        for case in manifest["cases"]:
            name = case["name"]
            expected = normalized((case_dir / f"{name}.expected").read_text(encoding="utf-8"))
            cs_ok, cs_detail = verify_csharp(case_dir, scratch, name, args.dotnet, expected)
            if not cs_ok:
                print(f"FAIL  {name}: {cs_detail}")
                failures.append(name)
                continue

            zan_ok, zan_detail = verify_zan(case_dir, scratch, name, zanc, expected)
            status = case["zanStatus"]
            if status == "pass":
                if zan_ok:
                    print(f"PASS  {name}: {cs_detail}; {zan_detail}")
                else:
                    print(f"FAIL  {name}: {zan_detail}")
                    failures.append(name)
            elif status == "xfail":
                if zan_ok:
                    print(f"XPASS {name}: remove xfail from manifest.json")
                    failures.append(name)
                else:
                    print(f"XFAIL {name}: {case['reason']}\n      {zan_detail.splitlines()[0]}")
                    if args.require_zan_pass:
                        failures.append(name)
            else:
                print(f"FAIL  {name}: unknown zanStatus {status!r}")
                failures.append(name)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    if failures:
        print("compatibility failures: " + ", ".join(failures))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

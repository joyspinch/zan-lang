"""Compile and run a Windows x64 blocking-dispatch case with an injected OOM."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command, stdout=subprocess.PIPE):
    result = subprocess.run(
        command, stdout=stdout, stderr=subprocess.PIPE, timeout=30,
        encoding="utf-8", errors="replace",
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{command[0]} exited with {result.returncode}\n"
            f"{result.stdout or ''}{result.stderr}"
        )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=("await_extern_dispatch", "socket_dispatch"))
    for option in ("zanc", "cc", "clang", "lld", "out-exe"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, help="defaults to the directory containing zanc")
    args = parser.parse_args()

    tests = Path(__file__).resolve().parent
    root = tests.parents[1]
    runtime = args.runtime_dir or args.zanc.resolve().parent
    mingw = runtime / "mingw" / "lib"
    out_exe = args.out_exe.resolve()
    out_exe.parent.mkdir(parents=True, exist_ok=True)
    package_args = ["--no-packages"]
    if args.case == "socket_dispatch":
        package_args = ["--package-project", root]

    with tempfile.TemporaryDirectory(prefix=f"{out_exe.stem}-", dir=out_exe.parent) as temp:
        work = Path(temp)
        ir = work / "case.ll"
        program = work / "case.obj"
        fixture = work / "fixture.obj"
        run([
            args.cc, "-std=c11", "-O2", "-ffunction-sections",
            "-DZAN_IO_STACKLESS_ONLY", "-DZAN_CO_DRIVER", "-c",
            tests / "await_extern_dispatch_fixture.c", "-o", fixture,
        ])
        # Link the generated program with one I/O object, the fault-injecting fixture.
        with ir.open("wb") as output:
            run([
                args.zanc, tests / f"{args.case}.zan", "--auto-stdlib",
                "--stdlib-path", root / "stdlib", *package_args,
                "--no-fast-alloc", "--no-icon", "--arc-guard", "--check-leaks",
                "-O2", "--quiet", "--emit-ir",
            ], stdout=output)
        run([args.clang, "--target=x86_64-w64-windows-gnu", "-c", ir, "-o", program])
        objects = (
            "zanrt_sync", "zanrt_file", "zanrt_decimal", "zanrt_timer",
            "zanrt_crash", "zan_embed_api", "zan_inflate",
        )
        libraries = (
            "mingw32", "moldname", "mingwex", "msvcrt", "kernel32", "advapi32",
            "shell32", "user32", "ws2_32", "mswsock", "bcrypt", "gcc", "gcc_eh",
            "supc++", ":libwinpthread.a",
        )
        run([
            args.lld, "-m", "i386pep", "--gc-sections", "-o", out_exe,
            mingw / "crt2.o", mingw / "crtbegin.o", program, fixture,
            *(runtime / f"{name}.obj" for name in objects),
            f"-L{mingw}", "--start-group", *(f"-l{name}" for name in libraries), "--end-group",
        ])

    result = run([out_exe])
    if result.stdout != "true\n" or result.stderr:
        raise RuntimeError(f"unexpected stdout {result.stdout!r} or stderr {result.stderr!r}")
    print(f"{args.case} passed")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))

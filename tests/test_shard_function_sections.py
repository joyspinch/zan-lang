"""Windows x64 GNU/COFF regression for publish shard function sections.

Use --zanc --cc --llvm-tools --out <exe> --log <file>; CTest name:
conformance_shard_function_sections. Run under the caller's build/test lease.
The manifest excludes --emit-lib from sharding. Deliberately fail the final
Zan link to retain real shard objects, then link their C consumer with LLD.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


LIVE, UNUSED = "ShardGc_Live", "ShardGc_Unused"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, env, work, log, expected=0):
    command = [str(part) for part in command]
    log.write("$ " + subprocess.list2cmdline(command) + "\n")
    log.flush()
    result = subprocess.run(command, cwd=work, env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="replace",
                            timeout=180)
    log.write(result.stdout + result.stderr + f"\nexit={result.returncode}\n")
    log.flush()
    if expected is not None:
        require(result.returncode == expected,
                f"command exited {result.returncode}; inspect {log.name}")
    return result


def symbols(nm, artifact, env, work, log):
    result = run([nm, "--defined-only", "--format=posix", artifact],
                 env, work, log)
    return {match.group(1) for match in
            re.finditer(r"^(\S+)\s+[TtDdBbRr]\s+[0-9a-fA-F]+\b", result.stdout, re.M)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("zanc", "cc", "llvm-tools", "out", "log"):
        parser.add_argument("--" + flag, type=Path, required=True)
    args = parser.parse_args()
    require(sys.platform == "win32", "requires Windows x64 GNU/COFF tools")
    zanc, cc = args.zanc.resolve(), args.cc.resolve()
    tools, out, log_path = (args.llvm_tools.resolve(), args.out.resolve(),
                            args.log.resolve())
    require(out.suffix.lower() == ".exe", "--out must name an .exe")
    require(out != log_path, "--out and --log must differ")
    nm, objdump = tools / "llvm-nm.exe", tools / "llvm-objdump.exe"
    objcopy = tools / "llvm-objcopy.exe"
    lld, crt = zanc.parent / "ld.lld.exe", zanc.parent / "mingw" / "lib"
    runtime = [zanc.parent / name for name in
               ("zanrt_io_mt.obj", "zanrt_sync.obj", "zanrt_file.obj",
                "zanrt_decimal.obj", "zan_inflate.obj", "zanrt_timer.obj",
                "zanrt_crash.obj", "libzancrypto.a", "zanrt_mem.obj")]
    for path in (zanc, cc, nm, objdump, objcopy, lld, crt / "crt2.o",
                 crt / "crtbegin.o", crt / "crtend.o", *runtime):
        require(path.is_file(), f"missing prerequisite: {path}")
    out.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    # Objects live beside the one final executable; logs stay in test-out.
    work = Path(tempfile.mkdtemp(prefix="shard-sections-", dir=out.parent))
    fixtures = Path(__file__).resolve().parent
    fixture = fixtures / "conformance" / "shard_function_sections.zan"
    env = os.environ.copy()
    env.update(ZAN_SHARD="1", ZAN_NO_SHARD="0", ZAN_SHARD_MAX_FN="2",
               ZAN_SHARD_MAX_INSN="1000000000", ZAN_KEEP_FAILED_OBJ="1",
               ZAN_CODEGEN_MANIFEST_JSON=str(log_path.with_suffix(".manifest.json")),
               ZAN_SHARD_DUMP=str(log_path.with_suffix(".ll")))
    with log_path.open("w", encoding="utf-8") as log:
        try:
            missing = work / "intentional-missing-link-input.o"
            result = run([zanc, fixture,
                          "--publish", "--no-stdlib", "--no-icon",
                          "--no-check-leaks", "--no-arc-guard", "--link-input",
                          missing, "-o", work / "retain.exe"],
                         env, work, log, expected=None)
            diagnostics = result.stdout + result.stderr
            require(result.returncode == 1 and missing.name in diagnostics
                    and "error: linking failed" in diagnostics,
                    "expected only the deliberate final-link failure")
            require(re.search(r"shard: [1-9][0-9]* objects emitted", diagnostics)
                    and "falling back to single module" not in diagnostics,
                    "fixture did not exercise successful shard emission")
            retained = re.findall(r"failed-link object retained at '([^']+)'",
                                  diagnostics)
            shards = [Path(path) for path in retained
                      if re.search(r"\.shard\d+\.o$", path)]
            require(len(shards) >= 2, "expected at least two retained shards")
            owners = [path for path in shards
                      if {LIVE, UNUSED} <= symbols(nm, path, env, work, log)]
            require(len(owners) == 1,
                    "Live and Unused must be global definitions in one shard")
            shard = owners[0]
            coordinator = Path(str(work / "retain.exe") + ".o")
            require(coordinator in [Path(path) for path in retained],
                    "expected the coordinator with generated EH support")
            run([objcopy, "--redefine-sym", "main=ShardGc_ZanEntry", coordinator],
                env, work, log)
            headers = run([objdump, "-x", shard],
                          env, work, log).stdout
            require("file format coff-x86-64" in headers, "expected x64 COFF")
            sections = {}
            for name in (LIVE, UNUSED):
                match = re.search(r"^\s*(\d+)\s+(\.text[.$]" + name
                                  + r")\s+([0-9a-fA-F]+)\s", headers, re.M)
                require(match and int(match.group(3), 16) > 0,
                        f"missing nonempty function section for {name}")
                sections[name] = match.group(1)
                require(re.search(re.escape(match.group(2))
                                  + r"\nAUX [^\n]*\bcomdat 1\b", headers),
                        f"{name} must keep NODUPLICATES selection")
                for kind in ("pdata", "xdata"):
                    require(re.search(r"^\s*\d+\s+\." + kind + r"\$"
                                      + name + r"\s+[0-9a-fA-F]+\s", headers, re.M),
                            f"missing per-function {kind} section for {name}")
            require(sections[LIVE] != sections[UNUSED],
                    "Live and Unused share a section")
            consumer = work / "consumer.o"
            # Match the production PE runtime link, but retain symbols for nm.
            # LLD resolves undefined symbols before GC, including discarded code.
            out.unlink(missing_ok=True)
            link = [lld, "-m", "i386pep", "--stack", "268435456",
                    "--gc-sections", "-o", out,
                    crt / "crt2.o", crt / "crtbegin.o", consumer, coordinator,
                    *shards, *runtime, "--wrap=malloc", "--wrap=free",
                    "--wrap=calloc", "--wrap=realloc", "-L" + str(crt),
                    "--start-group", "-lmingw32", "-lgcc", "-lgcc_eh", "-lsupc++",
                    "-lmoldname", "-lmingwex", "-lmsvcrt", "-lkernel32",
                    "-ladvapi32", "-lshell32", "-luser32", "-lversion",
                    "-l:libwinpthread.a", "-lws2_32", "--end-group", crt / "crtend.o"]
            # Reuse the objects; a second strong definition must remain an error.
            run([cc, "--target=x86_64-w64-windows-gnu", "-fno-lto",
                 "-DSHARD_GC_DUPLICATE", "-c",
                 fixtures / "size" / "shard_function_sections_main.c",
                 "-o", consumer], env, work, log)
            duplicate = run(link, env, work, log, expected=None)
            require(duplicate.returncode != 0
                    and re.search(r"duplicate symbol:.*ShardGc_Live",
                                  duplicate.stdout + duplicate.stderr),
                    "duplicate strong definition must be rejected")
            run([cc, "--target=x86_64-w64-windows-gnu", "-fno-lto", "-c",
                 fixtures / "size" / "shard_function_sections_main.c",
                 "-o", consumer], env, work, log)
            run(link, env, work, log)
            linked = symbols(nm, out, env, work, log)
            require(LIVE in linked and UNUSED not in linked
                    and "ShardGc_Pad" not in linked
                    and "ShardGc_ZanEntry" not in linked,
                    "link GC must retain Live and remove unused shard/main bodies")
            require("__zan_sf_ShardGc_LiveOffset" in linked
                    and "__zan_sf_ShardGc_UnusedOffset" not in linked,
                    "link GC must retain live static data and remove unused data")
            result = run([out], env, work, log)
            require(result.stdout.replace("\r\n", "\n") == "42\n",
                    "C consumer must print 42 and exit zero")
            log.write("PASS: same shard, independent sections, GC, run42\n")
        except Exception as error:
            log.write(f"FAIL: {error}\n")
            raise
    shutil.rmtree(work)
    print(f"shard function sections: PASS; log={log_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit(f"shard function sections: FAIL: {error}")

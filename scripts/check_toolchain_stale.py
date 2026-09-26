#!/usr/bin/env python3
"""Report checked-in binaries that are older than the sources they were built
from.

Cross-compilation ships prebuilt runtime objects (toolchain/<target>/*.o) and a
prebuilt GUI driver (stdlib/Gui/drivers/<target>/), because neither can be
produced on a machine that is not that target. Nothing rebuilds them, so a
runtime edit silently leaves every cross target on the old code -- twice now
that has cost days: a pre-A2-0b zan_gui.dll made one test take 331 seconds, and
rt_sync.c changes do not reach any non-Windows build until someone reruns the
per-platform build there.

Staleness is decided by git history, not mtimes: an artifact is stale when a
source it is built from has a newer last commit.

    python3 scripts/check_toolchain_stale.py

Exits non-zero when anything is stale. Rebuild on the target platform
(scripts/build_macos_rt.sh, scripts/build_gui_driver.ps1, or the platform's
CMake build) and commit the artifact to clear it.

With --verify, every date-stale artifact that this machine can rebuild is
actually rebuilt and byte-compared: an identical rebuild proves the committed
content is already current (downgrades to ok), a differing one is real
staleness. Artifacts with no local builder stay reported as stale.
"""
import subprocess
import sys
import os
import glob
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

RT_IO = ["src/runtime/rt_io.c", "src/runtime/rt_sched.c"]
RT_SYNC = ["src/runtime/rt_sync.c"]
RT_FILE = ["src/runtime/rt_file.c"]
RT_TIMER = ["src/runtime/rt_timer.c"]
RT_WASM = ["src/runtime/rt_wasm.c"]   # zanrt_wasm.o compiles rt_wasm.c only; rt_file.c
                                     # links alongside as its own object (build_cross_rt.cmd)
RT_GUI_MAC = ["src/runtime/gui_compat_mac.c"]
ANDROID_NDK = []  # NDK-derived: no repo source drives it; never "stale" by src
GUI = ["src/runtime/gui_runtime.c", "src/runtime/gui_runtime_text.c",
       "src/runtime/gui_runtime_font.c", "src/runtime/gui_runtime_x11.c",
       "src/runtime/gui_runtime_tray.c", "src/runtime/gui_runtime_sdl.c",
       "src/runtime/gui_runtime_shims.c"]

# (path, sources, group). The groups exist so CI can hold the part it can
# rebuild -- `--group runtime`, which zig cc produces for every target from one
# Linux runner -- to a hard failure, while the GUI drivers, whose archives
# bundle the platform's X11/freetype and are still built by hand, only report.
ARTIFACTS = [
    ("toolchain/linux-musl/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/linux-musl/zanrt_io_mt.o", RT_IO, "runtime"),
    ("toolchain/linux-musl/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/linux-musl/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/linux-arm64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/linux-arm64/zanrt_io_mt.o", RT_IO, "runtime"),
    ("toolchain/linux-arm64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/linux-arm64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/linux-riscv64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/linux-riscv64/zanrt_io_mt.o", RT_IO, "runtime"),
    ("toolchain/linux-riscv64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/linux-riscv64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/macos/arm64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/macos/arm64/zanrt_io_mt.o", RT_IO, "runtime"),
    ("toolchain/macos/arm64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/macos/arm64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/macos/arm64/zanrt_timer.o", RT_TIMER, "runtime"),
    ("toolchain/macos/arm64/zanrt_gui.o", RT_GUI_MAC, "runtime"),
    ("toolchain/macos/x64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/macos/x64/zanrt_io_mt.o", RT_IO, "runtime"),
    ("toolchain/macos/x64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/macos/x64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/macos/x64/zanrt_timer.o", RT_TIMER, "runtime"),
    ("toolchain/macos/x64/zanrt_gui.o", RT_GUI_MAC, "runtime"),
    ("toolchain/ios/arm64/zanrt_gui.o", RT_GUI_MAC, "runtime"),
    ("toolchain/wasm32/zanrt_wasm.o", RT_WASM, "runtime"),
    ("toolchain/wasm32/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/wasm32/zanrt_timer.o", RT_TIMER, "runtime"),
    # Built with mozbuild clang -fexceptions -mllvm -wasm-enable-eh (zig's
    # clang cannot emit the tag); source lives beside the object. The commit
    # below it keeps the artifact fresh by hand -- see build_cross_rt.cmd.
    ("toolchain/wasm32/zanrt_ehtag.o",
     ["toolchain/wasm32/zanrt_ehtag.c"], "manual"),
    # Android (bionic) runtime objects: built with the NDK's clang per the
    # recipe in build_cross_rt.cmd -- zig cc has no bionic target.
    ("toolchain/android-x64/zanrt_io.o", RT_IO, "manual"),
    ("toolchain/android-x64/zanrt_sync.o", RT_SYNC, "manual"),
    ("toolchain/android-x64/zanrt_file.o", RT_FILE, "manual"),
    ("toolchain/android-x64/zanrt_timer.o", RT_TIMER, "manual"),
    ("toolchain/android-arm64/zanrt_io.o", RT_IO, "manual"),
    ("toolchain/android-arm64/zanrt_sync.o", RT_SYNC, "manual"),
    ("toolchain/android-arm64/zanrt_file.o", RT_FILE, "manual"),
    ("toolchain/android-arm64/zanrt_timer.o", RT_TIMER, "manual"),
    # The NDK sysroot subset (crt + libc/libm/libdl + compiler-rt builtins)
    # tracks the NDK itself, not repo sources -- report-only, refreshed by hand.
    ("toolchain/android-x64/libc.a", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libm.a", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libdl.a", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/crtbegin_static.o", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/crtend_android.o", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libclang_rt.builtins.a", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libc.a", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libm.a", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libdl.a", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/crtbegin_static.o", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/crtend_android.o", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libclang_rt.builtins.a", ANDROID_NDK, "manual"),
    # OpenHarmony (OHOS) runtime objects: built with the OHOS NDK's clang per
    # the recipe in build_cross_rt.cmd (target *-linux-ohos, musl sysroot).
    ("toolchain/ohos-x64/zanrt_io.o", RT_IO, "manual"),
    ("toolchain/ohos-x64/zanrt_sync.o", RT_SYNC, "manual"),
    ("toolchain/ohos-x64/zanrt_file.o", RT_FILE, "manual"),
    ("toolchain/ohos-x64/zanrt_timer.o", RT_TIMER, "manual"),
    ("toolchain/ohos-arm64/zanrt_io.o", RT_IO, "manual"),
    ("toolchain/ohos-arm64/zanrt_sync.o", RT_SYNC, "manual"),
    ("toolchain/ohos-arm64/zanrt_file.o", RT_FILE, "manual"),
    ("toolchain/ohos-arm64/zanrt_timer.o", RT_TIMER, "manual"),
    # The OHOS NDK sysroot subset (crt + libc.a + compiler-rt builtins/unwind)
    # tracks the NDK itself, not repo sources -- report-only, refreshed by
    # hand. libm.a/libdl.a are empty archives in the OHOS sysroot and are not
    # committed.
    ("toolchain/ohos-x64/libc.a", ANDROID_NDK, "manual"),
    ("toolchain/ohos-x64/crt1.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-x64/crti.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-x64/crtn.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-x64/clang_rt.crtbegin.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-x64/clang_rt.crtend.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-x64/libclang_rt.builtins.a", ANDROID_NDK, "manual"),
    ("toolchain/ohos-x64/libunwind.a", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/libc.a", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/crt1.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/crti.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/crtn.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/clang_rt.crtbegin.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/clang_rt.crtend.o", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/libclang_rt.builtins.a", ANDROID_NDK, "manual"),
    ("toolchain/ohos-arm64/libunwind.a", ANDROID_NDK, "manual"),
    ("stdlib/Gui/drivers/win-x64/zan_gui.dll", GUI, "gui"),
    ("stdlib/Gui/drivers/linux-x64/static/libzan_gui.a", GUI, "gui"),
    ("stdlib/Gui/drivers/linux-arm64/static/libzan_gui.a", GUI, "gui"),
    ("stdlib/Gui/drivers/macos-arm64/libzan_gui.dylib", GUI, "gui"),
    ("stdlib/Gui/drivers/macos-x64/libzan_gui.dylib", GUI, "gui"),
]


_commit_cache = {}

def preload_commit_times(paths):
    needed = {p.replace("\\", "/"): p for p in paths}
    try:
        proc = subprocess.Popen(["git", "log", "--format=COMMIT:%ct", "--name-only"],
                                stdout=subprocess.PIPE, text=True, encoding="utf-8", errors="ignore")
    except Exception:
        return
    current_time = None
    found_count = 0
    total_needed = len(needed)
    for line in proc.stdout:
        line = line.strip()
        if not line:
            continue
        if line.startswith("COMMIT:"):
            try:
                current_time = int(line[7:])
            except ValueError:
                current_time = None
        else:
            norm = line.replace("\\", "/")
            if norm in needed:
                orig = needed[norm]
                if orig not in _commit_cache and current_time is not None:
                    _commit_cache[orig] = current_time
                    found_count += 1
                    if found_count == total_needed:
                        break
    try:
        proc.terminate()
    except Exception:
        pass


def last_commit(path):
    """Unix timestamp of the last commit touching `path`, or None if untracked."""
    if path in _commit_cache:
        return _commit_cache[path]
    out = subprocess.run(["git", "log", "-1", "--format=%ct", "--", path],
                         capture_output=True, text=True).stdout.strip()
    val = int(out) if out else None
    _commit_cache[path] = val
    return val


def _find_zig():
    zig = shutil.which("zig")
    if not zig:
        for c in [
            r"D:\tools\zig-x86_64-windows-0.15.1\zig.exe",
            os.path.expanduser(r"~/.mozbuild/zig/zig-x86_64-windows-0.14.1/zig.exe"),
            r"C:\zig\zig.exe"
        ]:
            if os.path.isfile(c):
                return c
    return zig


def _find_ndk():
    ndk = os.environ.get("ANDROID_NDK")
    if not ndk:
        ndks = glob.glob(os.path.expanduser(r"~\AppData\Local\Android\Sdk\ndk\*"))
        if ndks:
            ndk = ndks[-1]
    return ndk


def rebuild_cmd(artifact, zig, ndk):
    """The exact compile that (re)produces `artifact` locally, or None when
    this machine has no builder for it (GUI drivers need the target platform;
    ohos needs its NDK, whose sysroot headers zig does not carry). Mirrors
    do_rebuild / build_cross_rt.cmd flag for flag."""
    rt = "src/runtime"
    d, name = os.path.split(artifact.replace("\\", "/"))
    if d.startswith("toolchain/"):
        d = d[len("toolchain/"):]
    src = name.replace("zanrt_", "rt_").replace(".o", ".c")
    c11 = ["-std=c11"]
    if d == "wasm32":
        target, std = "wasm32-wasi", c11
    elif d.startswith("linux-"):
        arch = {"linux-musl": "x86_64", "linux-arm64": "aarch64",
                "linux-riscv64": "riscv64"}[d]
        target, std = f"{arch}-linux-musl", c11
    elif d.startswith("macos/"):
        arch = {"arm64": "aarch64", "x64": "x86_64"}[d.split("/")[1]]
        target, std = f"{arch}-macos.11.0", c11
    elif d.startswith("android-"):
        if not ndk:
            return None
        arch = {"x64": "x86_64", "arm64": "aarch64"}[d.split("-")[1]]
        clang = os.path.join(ndk, r"toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe")
        sysroot = os.path.join(ndk, r"toolchains\llvm\prebuilt\windows-x86_64\sysroot")
        if not os.path.isfile(clang):
            return None
        return ([clang, "--sysroot", sysroot, "-target",
                 f"{arch}-linux-android28", "-g0", "-fPIC", "-I", rt, "-O2"]
                + ([] if src == "rt_io.c" else c11)
                + (["-DZAN_IO_STACKLESS_ONLY"] if src == "rt_io.c" else [])
                + ["-c", f"{rt}/{src}"])
    else:
        return None
    if not zig:
        return None
    cmd = [zig, "cc", "-target", target, "-g0", "-fPIC", "-I", rt, "-O2"] + std
    if src == "rt_io.c":
        cmd += ["-DZAN_IO_STACKLESS_ONLY"]
    if name == "zanrt_io_mt.o":
        cmd += ["-DZAN_CO_DRIVER"]
    return cmd + ["-c", f"{rt}/{src}"]


def do_verify(stale_entries):
    """Second opinion for date-detected staleness: actually rebuild each
    artifact and byte-compare. A rebuild identical to the committed object
    proves the committed content already matches current sources (the source
    commits since then changed nothing this object embeds) and downgrades the
    finding; a differing rebuild is real staleness."""
    zig = _find_zig()
    ndk = _find_ndk()
    import hashlib
    still = 0
    for artifact, sources in stale_entries:
        cmd = rebuild_cmd(artifact, zig, ndk)
        if not cmd:
            print(f"STALE {artifact}")
            for src, when in sorted(sources.items(), key=lambda p: -p[1]):
                print(f"        behind {src} by {(when - _commit_cache.get(artifact, when)) // 86400} day(s)")
            print(f"        (no local builder to verify; rebuild on its platform)")
            still += 1
            continue
        tmp = artifact + ".verify"
        r = subprocess.run([c for c in cmd if c] + ["-o", tmp],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"STALE {artifact}  (verify build failed; see below)")
            print(r.stderr.strip()[-400:])
            still += 1
            continue
        def sha(p):
            with open(p, "rb") as f:
                return hashlib.sha256(f.read()).hexdigest()
        same = os.path.isfile(tmp) and sha(tmp) == sha(artifact)
        if os.path.isfile(tmp):
            os.remove(tmp)
        if same:
            print(f"ok    {artifact}  (date-stale but rebuild is byte-identical)")
        else:
            print(f"STALE {artifact}  (rebuild differs -- commit the fresh object)")
            still += 1
    return still


def do_rebuild():
    import os, glob, shutil
    with open("_scratch/check_result.txt", "a", encoding="utf-8") as f:
        f.write("do_rebuild started\n")
    zig = shutil.which("zig")
    if not zig:
        for c in [
            r"D:\tools\zig-x86_64-windows-0.15.1\zig.exe",
            os.path.expanduser(r"~/.mozbuild/zig/zig-x86_64-windows-0.14.1/zig.exe"),
            r"C:\zig\zig.exe"
        ]:
            if os.path.isfile(c):
                zig = c
                break
    print(f"Using ZIG: {zig}")

    ndk = os.environ.get("ANDROID_NDK")
    if not ndk:
        ndks = glob.glob(os.path.expanduser(r"~\AppData\Local\Android\Sdk\ndk\*"))
        if ndks:
            ndk = ndks[-1]
    print(f"Using NDK: {ndk}")

    rt = "src/runtime"

    if zig:
        for name, arch in [("linux-musl", "x86_64"), ("linux-arm64", "aarch64"), ("linux-riscv64", "riscv64")]:
            outdir = f"toolchain/{name}"
            os.makedirs(outdir, exist_ok=True)
            print(f"Building {name} ({arch})...")
            subprocess.run([zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-DZAN_IO_STACKLESS_ONLY", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_io.c", "-o", f"{outdir}/zanrt_io.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-DZAN_IO_STACKLESS_ONLY", "-DZAN_CO_DRIVER", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_io.c", "-o", f"{outdir}/zanrt_io_mt.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_sync.c", "-o", f"{outdir}/zanrt_sync.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_file.c", "-o", f"{outdir}/zanrt_file.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)

        for name, arch in [("arm64", "aarch64"), ("x64", "x86_64")]:
            outdir = f"toolchain/macos/{name}"
            os.makedirs(outdir, exist_ok=True)
            print(f"Building macos/{name} ({arch})...")
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-DZAN_IO_STACKLESS_ONLY", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_io.c", "-o", f"{outdir}/zanrt_io.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-DZAN_IO_STACKLESS_ONLY", "-DZAN_CO_DRIVER", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_io.c", "-o", f"{outdir}/zanrt_io_mt.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_sync.c", "-o", f"{outdir}/zanrt_sync.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_file.c", "-o", f"{outdir}/zanrt_file.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)

        outdir = "toolchain/wasm32"
        os.makedirs(outdir, exist_ok=True)
        print("Building wasm32...")
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_wasm.c", "-o", f"{outdir}/zanrt_wasm.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_file.c", "-o", f"{outdir}/zanrt_file.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=gnu11", "-I", rt, "-I", f"{rt}/libwebp/src", "-O2", "-c", f"{rt}/gui_runtime.c", "-o", f"{outdir}/zanrt_gui.o", "-DZAN_GUI_WASM"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=gnu11", "-I", rt, "-O2", "-c", f"{rt}/rt_sync_wasm.c", "-o", f"{outdir}/zanrt_syncw.o"], check=True)

    if ndk:
        clang = os.path.join(ndk, r"toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe")
        sysroot = os.path.join(ndk, r"toolchains\llvm\prebuilt\windows-x86_64\sysroot")
        if os.path.isfile(clang):
            for name, arch in [("android-x64", "x86_64"), ("android-arm64", "aarch64")]:
                outdir = f"toolchain/{name}"
                os.makedirs(outdir, exist_ok=True)
                print(f"Building {name} ({arch})...")
                base_cmd = [clang, "--sysroot", sysroot, "-target", f"{arch}-linux-android28", "-g0", "-fPIC", "-I", rt, "-O2"]
                subprocess.run(base_cmd + ["-DZAN_IO_STACKLESS_ONLY", "-c", f"{rt}/rt_io.c", "-o", f"{outdir}/zanrt_io.o"], check=True)
                subprocess.run(base_cmd + ["-std=c11", "-c", f"{rt}/rt_sync.c", "-o", f"{outdir}/zanrt_sync.o"], check=True)
                subprocess.run(base_cmd + ["-std=c11", "-c", f"{rt}/rt_file.c", "-o", f"{outdir}/zanrt_file.o"], check=True)
                subprocess.run(base_cmd + ["-std=c11", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)
                subprocess.run(base_cmd + ["-std=c11", "-I", "src/common", "-c", f"{rt}/zan_embed_api.c", "-o", f"{outdir}/zan_embed_api.o"], check=True)
                subprocess.run(base_cmd + ["-std=c11", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_ZIP_APIS", "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-I", "src/common", "-c", f"{rt}/zan_inflate.c", "-o", f"{outdir}/zan_inflate.o"], check=True)

    print("Rebuild completed successfully.")
    with open("_scratch/check_result.txt", "a", encoding="utf-8") as f:
        f.write("do_rebuild finished\n")
    return 0


def main():
    with open("_scratch/check_result.txt", "a", encoding="utf-8") as f:
        f.write("main called with: " + " ".join(sys.argv) + "\n")
    if "--rebuild" in sys.argv:
        return do_rebuild()
    group = "all"
    for arg in sys.argv[1:]:
        if arg.startswith("--group="):
            group = arg.split("=", 1)[1]
        elif arg == "--verify":
            pass
        else:
            print(f"usage: {sys.argv[0]} [--group=runtime|gui|all] [--verify]")
            return 2

    all_paths = set()
    for artifact, sources, kind in ARTIFACTS:
        if group != "all" and kind != group:
            continue
        all_paths.add(artifact)
        all_paths.update(sources)
    preload_commit_times(all_paths)

    stale = 0
    stale_list = []
    for artifact, sources, kind in ARTIFACTS:
        if group != "all" and kind != group:
            continue
        built = last_commit(artifact)
        if built is None:
            print(f"skip  {artifact} (not in the repo)")
            continue
        newer = {s: t for s in sources
                 for t in [last_commit(s)] if t and t > built}
        if not newer:
            print(f"ok    {artifact}")
            continue
        stale += 1
        stale_list.append((artifact, newer))
        print(f"STALE {artifact}")
        for src, when in sorted(newer.items(), key=lambda p: -p[1]):
            print(f"        behind {src} by {(when - built) // 86400} day(s)")
    if "--verify" in sys.argv:
        print("\nverifying date-stale artifacts by byte-comparing a fresh rebuild...")
        stale = do_verify(stale_list)
        print(f"(verify verdicts above replace the date findings)")
    if stale:
        print(f"\n{stale} artifact(s) need a rebuild on their own platform.")
    with open("_scratch/check_result.txt", "w", encoding="utf-8") as f:
        f.write(f"stale_count={stale}\n")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())

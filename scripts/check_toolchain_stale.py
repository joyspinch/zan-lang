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
"""
import subprocess
import sys

RT_IO = ["src/runtime/rt_io.c", "src/runtime/rt_sched.c"]
RT_SYNC = ["src/runtime/rt_sync.c"]
RT_FILE = ["src/runtime/rt_file.c"]
RT_TIMER = ["src/runtime/rt_timer.c"]
RT_WASM = ["src/runtime/rt_wasm.c", "src/runtime/rt_file.c"]
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
    ("toolchain/linux-musl/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/linux-musl/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/linux-arm64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/linux-arm64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/linux-arm64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/linux-riscv64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/linux-riscv64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/linux-riscv64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/macos/arm64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/macos/arm64/zanrt_io_mt.o", RT_IO, "runtime"),
    ("toolchain/macos/arm64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/macos/arm64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/macos/x64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/macos/x64/zanrt_io_mt.o", RT_IO, "runtime"),
    ("toolchain/macos/x64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/macos/x64/zanrt_file.o", RT_FILE, "runtime"),
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
    ("stdlib/Gui/drivers/win-x64/zan_gui.dll", GUI, "gui"),
    ("stdlib/Gui/drivers/linux-x64/static/libzan_gui.a", GUI, "gui"),
    ("stdlib/Gui/drivers/linux-arm64/static/libzan_gui.a", GUI, "gui"),
    ("stdlib/Gui/drivers/macos-arm64/libzan_gui.dylib", GUI, "gui"),
    ("stdlib/Gui/drivers/macos-x64/libzan_gui.dylib", GUI, "gui"),
]


def last_commit(path):
    """Unix timestamp of the last commit touching `path`, or None if untracked."""
    out = subprocess.run(["git", "log", "-1", "--format=%ct", "--", path],
                         capture_output=True, text=True).stdout.strip()
    return int(out) if out else None


def main():
    group = "all"
    for arg in sys.argv[1:]:
        if arg.startswith("--group="):
            group = arg.split("=", 1)[1]
        else:
            print(f"usage: {sys.argv[0]} [--group=runtime|gui|all]")
            return 2
    stale = 0
    for artifact, sources, kind in ARTIFACTS:
        if group != "all" and kind != group:
            continue
        built = last_commit(artifact)
        if built is None:
            print(f"skip  {artifact} (not in the repo)")
            continue
        newer = [(s, t) for s in sources
                 for t in [last_commit(s)] if t and t > built]
        if not newer:
            print(f"ok    {artifact}")
            continue
        stale += 1
        print(f"STALE {artifact}")
        for src, when in sorted(newer, key=lambda p: -p[1]):
            print(f"        behind {src} by {(when - built) // 86400} day(s)")
    if stale:
        print(f"\n{stale} artifact(s) need a rebuild on their own platform.")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())

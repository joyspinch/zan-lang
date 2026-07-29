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
GUI = ["src/runtime/gui_runtime.c", "src/runtime/gui_runtime_text.c",
       "src/runtime/gui_runtime_font.c", "src/runtime/gui_runtime_x11.c"]

ARTIFACTS = [
    ("toolchain/linux-musl/zanrt_io.o", RT_IO),
    ("toolchain/linux-musl/zanrt_sync.o", RT_SYNC),
    ("toolchain/linux-arm64/zanrt_io.o", RT_IO),
    ("toolchain/linux-arm64/zanrt_sync.o", RT_SYNC),
    ("toolchain/linux-riscv64/zanrt_io.o", RT_IO),
    ("toolchain/linux-riscv64/zanrt_sync.o", RT_SYNC),
    ("toolchain/macos/arm64/zanrt_io.o", RT_IO),
    ("toolchain/macos/arm64/zanrt_io_mt.o", RT_IO),
    ("toolchain/macos/arm64/zanrt_sync.o", RT_SYNC),
    ("toolchain/macos/x64/zanrt_io.o", RT_IO),
    ("toolchain/macos/x64/zanrt_io_mt.o", RT_IO),
    ("toolchain/macos/x64/zanrt_sync.o", RT_SYNC),
    ("stdlib/Gui/drivers/win-x64/zan_gui.dll", GUI),
    ("stdlib/Gui/drivers/linux-x64/static/libzan_gui.a", GUI),
    ("stdlib/Gui/drivers/linux-arm64/static/libzan_gui.a", GUI),
    ("stdlib/Gui/drivers/macos-arm64/libzan_gui.dylib", GUI),
    ("stdlib/Gui/drivers/macos-x64/libzan_gui.dylib", GUI),
]


def last_commit(path):
    """Unix timestamp of the last commit touching `path`, or None if untracked."""
    out = subprocess.run(["git", "log", "-1", "--format=%ct", "--", path],
                         capture_output=True, text=True).stdout.strip()
    return int(out) if out else None


def main():
    stale = 0
    for artifact, sources in ARTIFACTS:
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

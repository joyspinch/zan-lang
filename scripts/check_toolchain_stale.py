#!/usr/bin/env python3
"""Report checked-in binaries that are older than the sources they were built
from.

Cross-compilation ships prebuilt runtime objects (toolchain/<target>/*.o) and a
prebuilt GUI driver (packages/Zan.Gui/src/Gui/drivers/<target>/), because neither can be
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
RT_MEM = ["src/runtime/rt_mem.c"]
EMBED = ["src/runtime/zan_embed_api.c", "src/runtime/rt_timer.h",
         "src/common/host_oom.h"]
INFLATE = ["src/runtime/zan_inflate.c", "src/common/miniz_tinfl.c",
           "src/common/miniz.h"]
WASM_GUI = ["src/runtime/gui_runtime.c", "src/runtime/libwebp"]  # unity object
ANDROID_NDK = []  # NDK-derived: no repo source drives it; never "stale" by src
OHOS_NDK = []     # same for the OHOS SDK sysroot subset (libc.a, crt*, builtins)
ZIG_BUNDLED = []  # zig/wasi/musl sysroot subsets + external prebuilts: no repo source
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
    # The rest of each linux bundle (build_cross_rt.cmd's zig block): the timer
    # object every program links, the embedded-resource pair, and --fast-alloc's
    # allocator object (musl-only so far).
    ("toolchain/linux-musl/zanrt_timer.o", RT_TIMER, "runtime"),
    ("toolchain/linux-musl/zan_embed_api.o", EMBED, "runtime"),
    ("toolchain/linux-musl/zan_inflate.o", INFLATE, "runtime"),
    ("toolchain/linux-musl/zanrt_mem.o", RT_MEM, "runtime"),
    ("toolchain/linux-arm64/zanrt_timer.o", RT_TIMER, "runtime"),
    ("toolchain/linux-arm64/zan_embed_api.o", EMBED, "runtime"),
    ("toolchain/linux-arm64/zan_inflate.o", INFLATE, "runtime"),
    ("toolchain/linux-riscv64/zanrt_timer.o", RT_TIMER, "runtime"),
    ("toolchain/linux-riscv64/zan_embed_api.o", EMBED, "runtime"),
    ("toolchain/linux-riscv64/zan_inflate.o", INFLATE, "runtime"),
    # musl sysroot subset (crt + libc.a; libgcc.a on arm64/riscv64) tracks the
    # external sysroot, not repo sources.
    ("toolchain/linux-musl/crt1.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-musl/crti.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-musl/crtn.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-musl/libc.a", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-arm64/crt1.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-arm64/crti.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-arm64/crtn.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-arm64/libc.a", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-arm64/libgcc.a", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-riscv64/crt1.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-riscv64/crti.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-riscv64/crtn.o", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-riscv64/libc.a", ZIG_BUNDLED, "manual"),
    ("toolchain/linux-riscv64/libgcc.a", ZIG_BUNDLED, "manual"),
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
    ("toolchain/macos/arm64/zan_embed_api.o", EMBED, "runtime"),
    ("toolchain/macos/arm64/zan_inflate.o", INFLATE, "runtime"),
    ("toolchain/macos/x64/zan_embed_api.o", EMBED, "runtime"),
    ("toolchain/macos/x64/zan_inflate.o", INFLATE, "runtime"),
    ("toolchain/ios/arm64/zanrt_gui.o", RT_GUI_MAC, "runtime"),
    # The other five ios objects came from the ios pipeline commit (3e284401),
    # built off-host with no committed recipe -- zig's ios target ships no libc
    # headers on Windows, so no local builder exists; report staleness only.
    ("toolchain/ios/arm64/zanrt_io.o", RT_IO, "manual"),
    ("toolchain/ios/arm64/zanrt_io_mt.o", RT_IO, "manual"),
    ("toolchain/ios/arm64/zanrt_sync.o", RT_SYNC, "manual"),
    ("toolchain/ios/arm64/zanrt_file.o", RT_FILE, "manual"),
    ("toolchain/ios/arm64/zanrt_timer.o", RT_TIMER, "manual"),
    ("toolchain/ios/libSystem.tbd", ZIG_BUNDLED, "manual"),
    ("toolchain/wasm32/zanrt_wasm.o", RT_WASM, "runtime"),
    ("toolchain/wasm32/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/wasm32/zanrt_timer.o", RT_TIMER, "runtime"),
    # Built with mozbuild clang -fexceptions -mllvm -wasm-enable-eh (zig's
    # clang cannot emit the tag); source lives beside the object. The commit
    # below it keeps the artifact fresh by hand -- see build_cross_rt.cmd.
    ("toolchain/wasm32/zanrt_ehtag.o",
     ["toolchain/wasm32/zanrt_ehtag.c"], "manual"),
    # GUI software rasterizer as one unity object (gui_runtime.c pulls text/
    # font/shims and the libwebp tree via #include), plus rt_sync_wasm for
    # GUI-sized thread/atomic pull-ins. Both built by build_cross_rt.cmd.
    ("toolchain/wasm32/zanrt_gui.o", WASM_GUI, "runtime"),
    ("toolchain/wasm32/zanrt_syncw.o", ["src/runtime/rt_sync_wasm.c"], "runtime"),
    # wasi-libc subset + external prebuilts (freetype archive): no repo source.
    ("toolchain/wasm32/crt1.o", ZIG_BUNDLED, "manual"),
    ("toolchain/wasm32/libc.a", ZIG_BUNDLED, "manual"),
    ("toolchain/wasm32/libm.a", ZIG_BUNDLED, "manual"),
    ("toolchain/wasm32/libzigc.a", ZIG_BUNDLED, "manual"),
    ("toolchain/wasm32/libclang_rt.builtins-wasm32.a", ZIG_BUNDLED, "manual"),
    ("toolchain/wasm32/libfreetype.a", ZIG_BUNDLED, "manual"),
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
    # Embedded-resource pair, NDK clang recipe (same block as above).
    ("toolchain/android-x64/zan_embed_api.o", EMBED, "manual"),
    ("toolchain/android-x64/zan_inflate.o", INFLATE, "manual"),
    ("toolchain/android-arm64/zan_embed_api.o", EMBED, "manual"),
    ("toolchain/android-arm64/zan_inflate.o", INFLATE, "manual"),
    # app-glue object + dynamic crt + the NDK stub/link .so set: NDK-derived.
    ("toolchain/android-x64/android_native_app_glue.o", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/crtbegin_dynamic.o", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libc.so", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libdl.so", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libm.so", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/liblog.so", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libEGL.so", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libGLESv2.so", ANDROID_NDK, "manual"),
    ("toolchain/android-x64/libaaudio.so", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/android_native_app_glue.o", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/crtbegin_dynamic.o", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libc.so", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libdl.so", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libm.so", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/liblog.so", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libEGL.so", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libGLESv2.so", ANDROID_NDK, "manual"),
    ("toolchain/android-arm64/libaaudio.so", ANDROID_NDK, "manual"),
    # win-arm64/win-x64 PE objects: built by scripts/build_win_rt.sh with the
    # matching MSYS2 CLANGARM64/MINGW64 clang of the CI drivers.yml job (mingw
    # ABI; zig's own mingw headers are a different vintage, so no byte-stable
    # local builder). Report staleness; rebuild in that environment.
    ("toolchain/win-arm64/zanrt_io.o", RT_IO, "manual"),
    ("toolchain/win-arm64/zanrt_io_mt.o", RT_IO, "manual"),
    ("toolchain/win-arm64/zanrt_sync.o", RT_SYNC, "manual"),
    ("toolchain/win-arm64/zanrt_file.o", RT_FILE, "manual"),
    ("toolchain/win-arm64/zanrt_timer.o", RT_TIMER, "manual"),
    ("toolchain/win-arm64/zan_embed_api.o", EMBED, "manual"),
    ("toolchain/win-arm64/zan_inflate.o", INFLATE, "manual"),
    ("toolchain/win-x64/zanrt_io.o", RT_IO, "manual"),
    ("toolchain/win-x64/zanrt_io_mt.o", RT_IO, "manual"),
    ("toolchain/win-x64/zanrt_sync.o", RT_SYNC, "manual"),
    ("toolchain/win-x64/zanrt_file.o", RT_FILE, "manual"),
    ("toolchain/win-x64/zanrt_timer.o", RT_TIMER, "manual"),
    ("toolchain/win-x64/zan_embed_api.o", EMBED, "manual"),
    ("toolchain/win-x64/zan_inflate.o", INFLATE, "manual"),
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

    # OHOS: the source-derived objects rebuild on any host via the zig
    # fallback (scripts/build_ohos_rt_zig.sh: zig's bundled musl headers +
    # -D__OHOS__, arm64 timer -mcpu=generic+sha2; proven 2026-10-02). The
    # NDK/sysroot subset below tracks the OHOS SDK, not repo sources.
    ("toolchain/ohos-arm64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/ohos-arm64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/ohos-arm64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/ohos-arm64/zanrt_timer.o", RT_TIMER, "runtime"),
    ("toolchain/ohos-arm64/zan_embed_api.o",
     ["src/runtime/zan_embed_api.c", "src/runtime/rt_timer.h"], "runtime"),
    ("toolchain/ohos-arm64/zan_inflate.o",
     ["src/runtime/zan_inflate.c", "src/common/miniz_tinfl.c"], "runtime"),
    ("toolchain/ohos-arm64/zap_main.o", ["toolchain/ohos-x64/zap_main.c"], "runtime"),
    ("toolchain/ohos-arm64/libEGL.so", ["scripts/ohos_stub.c"], "manual"),
    ("toolchain/ohos-arm64/libGLESv3.so", ["scripts/ohos_stub.c"], "manual"),
    ("toolchain/ohos-x64/zanrt_io.o", RT_IO, "runtime"),
    ("toolchain/ohos-x64/zanrt_sync.o", RT_SYNC, "runtime"),
    ("toolchain/ohos-x64/zanrt_file.o", RT_FILE, "runtime"),
    ("toolchain/ohos-x64/zanrt_timer.o", RT_TIMER, "runtime"),
    ("toolchain/ohos-x64/zan_embed_api.o",
     ["src/runtime/zan_embed_api.c", "src/runtime/rt_timer.h"], "runtime"),
    ("toolchain/ohos-x64/zan_inflate.o",
     ["src/runtime/zan_inflate.c", "src/common/miniz_tinfl.c"], "runtime"),
    ("toolchain/ohos-x64/zap_main.o", ["toolchain/ohos-x64/zap_main.c"], "runtime"),
    ("toolchain/ohos-x64/libEGL.so", ["scripts/ohos_stub.c"], "manual"),
    ("toolchain/ohos-x64/libGLESv3.so", ["scripts/ohos_stub.c"], "manual"),
    # The OHOS NDK sysroot subset (crt + libc.a + compiler-rt builtins/unwind)
    # tracks the SDK itself, not repo sources -- report-only, refreshed by
    # hand. libm.a/libdl.a are empty archives in the OHOS sysroot and are not
    # committed.
    ("toolchain/ohos-arm64/libc.a", OHOS_NDK, "manual"),
    ("toolchain/ohos-arm64/crt1.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-arm64/crti.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-arm64/crtn.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-arm64/clang_rt.crtbegin.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-arm64/clang_rt.crtend.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-arm64/libclang_rt.builtins.a", OHOS_NDK, "manual"),
    ("toolchain/ohos-arm64/libunwind.a", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/libc.a", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/crt1.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/crti.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/crtn.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/clang_rt.crtbegin.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/clang_rt.crtend.o", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/libclang_rt.builtins.a", OHOS_NDK, "manual"),
    ("toolchain/ohos-x64/libunwind.a", OHOS_NDK, "manual"),
    ("packages/Zan.Gui/src/Gui/drivers/win-x64/zan_gui.dll", GUI, "gui"),
    ("packages/Zan.Gui/src/Gui/drivers/linux-x64/static/libzan_gui.a", GUI, "gui"),
    ("packages/Zan.Gui/src/Gui/drivers/linux-arm64/static/libzan_gui.a", GUI, "gui"),
    ("packages/Zan.Gui/src/Gui/drivers/macos-arm64/libzan_gui.dylib", GUI, "gui"),
    ("packages/Zan.Gui/src/Gui/drivers/macos-x64/libzan_gui.dylib", GUI, "gui"),
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
    this machine has no builder for it (GUI drivers and the ios objects have
    off-host builders; win-* needs the matching MSYS2 arch clang of
    build_win_rt.sh; the ohos stub .so pair is NDK-built by hand). Mirrors
    do_rebuild / build_cross_rt.cmd flag for flag."""
    rt = "src/runtime"
    d, name = os.path.split(artifact.replace("\\", "/"))
    if d.startswith("toolchain/"):
        d = d[len("toolchain/"):]
    src = name.replace("zanrt_", "rt_").replace(".o", ".c")
    c11 = ["-std=c11"]
    if d == "wasm32":
        # wasi objects are built WITHOUT -fPIC (build_cross_rt.cmd's wasm
        # block; a PIC rebuild differs byte-wise even when semantically same).
        # zanrt_ehtag.o needs mozbuild clang's wasm EH backend -- no builder.
        if not zig or name == "zanrt_ehtag.o":
            return None
        if name == "zanrt_gui.o":
            # Unity GUI object; the freetype variant only when both inputs the
            # .cmd checks are present, else the bitmap-font fallback build.
            cmd = [zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=gnu11",
                   "-I", rt, "-I", f"{rt}/libwebp/src", "-O2", "-DZAN_GUI_WASM"]
            ft_inc = os.environ.get("FREETYPE_INC") or "D:/project/firefox/modules/freetype2/include"
            if (os.path.isfile(os.path.join(ft_inc, "ft2build.h"))
                    and os.path.isfile("toolchain/wasm32/libfreetype.a")):
                cmd += ["-I", ft_inc, "-I", "toolchain/wasm32/freetype-shim",
                        "-DZAN_GUI_FREETYPE"]
            return cmd + ["-c", f"{rt}/gui_runtime.c"]
        if name == "zanrt_syncw.o":
            return [zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=gnu11",
                    "-I", rt, "-O2", "-c", f"{rt}/rt_sync_wasm.c"]
        return ([zig, "cc", "-target", "wasm32-wasi", "-g0"] + std
                + ["-I", rt, "-O2", "-c", f"{rt}/{src}"])
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
        if name == "zan_embed_api.o":
            return [clang, "--sysroot", sysroot, "-target", f"{arch}-linux-android28",
                    "-g0", "-std=c11", "-fPIC", "-I", rt, "-I", "src/common",
                    "-O2", "-c", f"{rt}/zan_embed_api.c"]
        if name == "zan_inflate.o":
            return [clang, "--sysroot", sysroot, "-target", f"{arch}-linux-android28",
                    "-g0", "-std=c11", "-fPIC", "-DMINIZ_NO_ARCHIVE_APIS",
                    "-DMINIZ_NO_ZIP_APIS", "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME",
                    "-I", rt, "-I", "src/common", "-O2", "-c", f"{rt}/zan_inflate.c"]
        base = [clang, "--sysroot", sysroot, "-target", f"{arch}-linux-android28"]
        if src == "rt_io.c":
            # io_mt compiles rt_io.c (+ZAN_CO_DRIVER), never rt_io_mt.c.
            cmd = base + ["-g0", "-fPIC", "-I", rt, "-O2", "-DZAN_IO_STACKLESS_ONLY"]
            if name == "zanrt_io_mt.o":
                cmd += ["-DZAN_CO_DRIVER"]
            return cmd + ["-c", f"{rt}/rt_io.c"]
        return (base + ["-g0", "-fPIC", "-I", rt, "-O2"] + c11
                + ["-c", f"{rt}/{src}"])
    elif d.startswith("ohos-"):
        # Mirror scripts/build_ohos_rt_zig.sh flag for flag (the NDK path in
        # build_cross_rt.cmd must stay identical too): zig's musl headers plus
        # -D__OHOS__ stand in for the NDK sysroot, and the objects link clean
        # against the committed OHOS musl libc.a subset. zig 0.15 confirms the
        # rebuild is byte-identical regardless of source-path style.
        if not zig:
            return None
        arch = "aarch64" if d.endswith("arm64") else "x86_64"
        if name == "zap_main.o":
            # HAP shell adapter: no -D__OHOS__, no runtime includes.
            return [zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-std=c11",
                    "-fPIC", "-O2", "-c", "toolchain/ohos-x64/zap_main.c"]
        table = {
            "zanrt_io.o":
                (["-DZAN_IO_STACKLESS_ONLY", "-I", rt, "-c", f"{rt}/rt_io.c"]),
            "zanrt_sync.o":
                (c11 + ["-I", rt, "-c", f"{rt}/rt_sync.c"]),
            "zanrt_file.o":
                (c11 + ["-I", rt, "-c", f"{rt}/rt_file.c"]),
            "zanrt_timer.o":
                (c11 + (["-mcpu=generic+sha2"] if arch == "aarch64" else [])
                    + ["-I", rt, "-c", f"{rt}/rt_timer.c"]),
            "zan_embed_api.o":
                (c11 + ["-I", rt, "-I", "src/common", "-c", f"{rt}/zan_embed_api.c"]),
            "zan_inflate.o":
                (c11 + ["-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_ZIP_APIS",
                        "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-I", rt, "-I", "src/common",
                        "-c", f"{rt}/zan_inflate.c"]),
        }
        if name not in table:
            return None  # stub libEGL.so/libGLESv3.so: NDK-built by hand
        return ([zig, "cc", "-target", f"{arch}-linux-musl", "-D__OHOS__",
                 "-g0", "-fPIC", "-O2"] + table[name])
    else:
        # ios/*: built off-host by the ios pipeline, no committed recipe.
        # win-*: needs the matching MSYS2 arch clang (build_win_rt.sh).
        return None
    # The embedded-resource pair for linux/macos: names the zanrt_*→rt_*
    # mapping accidentally gets right, but whose flags the generic tail gets
    # wrong -- inflate drops -I rt and adds -DMINIZ_NO_ARCHIVE_WRITERS.
    if not zig:
        return None
    if d.startswith("macos/") and name == "zanrt_gui.o":
        # macOS GUI shim object compiles gui_compat_mac.c, not rt_gui.c.
        return [zig, "cc", "-target", target, "-g0", "-std=c11", "-fPIC",
                "-I", rt, "-O2", "-c", f"{rt}/gui_compat_mac.c"]
    if name == "zan_embed_api.o":
        return [zig, "cc", "-target", target, "-g0", "-std=c11", "-fPIC",
                "-I", rt, "-O2", "-c", f"{rt}/zan_embed_api.c"]
    if name == "zan_inflate.o":
        return [zig, "cc", "-target", target, "-g0", "-std=c11", "-fPIC", "-O2",
                "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_ZIP_APIS", "-DMINIZ_NO_STDIO",
                "-DMINIZ_NO_TIME", "-DMINIZ_NO_ARCHIVE_WRITERS", "-I", "src/common",
                "-c", f"{rt}/zan_inflate.c"]
    if not zig:
        return None
    cmd = [zig, "cc", "-target", target, "-g0", "-fPIC", "-I", rt, "-O2"]
    if name in ("zanrt_io.o", "zanrt_io_mt.o"):
        # io/io_mt compile rt_io.c with the default gnu dialect -- -std=c11
        # hides the POSIX decls (sigemptyset, clock_gettime) and shifts every
        # byte; io_mt only adds -DZAN_CO_DRIVER (never an rt_io_mt.c).
        cmd += ["-DZAN_IO_STACKLESS_ONLY"]
        if name == "zanrt_io_mt.o":
            cmd += ["-DZAN_CO_DRIVER"]
        return cmd + ["-c", f"{rt}/rt_io.c"]
    return cmd + std + ["-c", f"{rt}/{src}"]


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
            subprocess.run([zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/zan_embed_api.c", "-o", f"{outdir}/zan_embed_api.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-linux-musl", "-g0", "-std=c11", "-fPIC", "-O2", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_ZIP_APIS", "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-DMINIZ_NO_ARCHIVE_WRITERS", "-I", "src/common", "-c", f"{rt}/zan_inflate.c", "-o", f"{outdir}/zan_inflate.o"], check=True)
            if name == "linux-musl":
                subprocess.run([zig, "cc", "-target", "x86_64-linux-musl", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_mem.c", "-o", f"{outdir}/zanrt_mem.o"], check=True)

        for name, arch in [("arm64", "aarch64"), ("x64", "x86_64")]:
            outdir = f"toolchain/macos/{name}"
            os.makedirs(outdir, exist_ok=True)
            print(f"Building macos/{name} ({arch})...")
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-DZAN_IO_STACKLESS_ONLY", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_io.c", "-o", f"{outdir}/zanrt_io.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-DZAN_IO_STACKLESS_ONLY", "-DZAN_CO_DRIVER", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_io.c", "-o", f"{outdir}/zanrt_io_mt.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_sync.c", "-o", f"{outdir}/zanrt_sync.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_file.c", "-o", f"{outdir}/zanrt_file.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/gui_compat_mac.c", "-o", f"{outdir}/zanrt_gui.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-I", rt, "-O2", "-c", f"{rt}/zan_embed_api.c", "-o", f"{outdir}/zan_embed_api.o"], check=True)
            subprocess.run([zig, "cc", "-target", f"{arch}-macos.11.0", "-g0", "-std=c11", "-fPIC", "-O2", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_ZIP_APIS", "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-DMINIZ_NO_ARCHIVE_WRITERS", "-I", "src/common", "-c", f"{rt}/zan_inflate.c", "-o", f"{outdir}/zan_inflate.o"], check=True)

        outdir = "toolchain/wasm32"
        os.makedirs(outdir, exist_ok=True)
        print("Building wasm32...")
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_wasm.c", "-o", f"{outdir}/zanrt_wasm.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_file.c", "-o", f"{outdir}/zanrt_file.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=c11", "-I", rt, "-O2", "-c", f"{rt}/rt_timer.c", "-o", f"{outdir}/zanrt_timer.o"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=gnu11", "-I", rt, "-I", f"{rt}/libwebp/src", "-O2", "-c", f"{rt}/gui_runtime.c", "-o", f"{outdir}/zanrt_gui.o", "-DZAN_GUI_WASM"], check=True)
        subprocess.run([zig, "cc", "-target", "wasm32-wasi", "-g0", "-std=gnu11", "-I", rt, "-O2", "-c", f"{rt}/rt_sync_wasm.c", "-o", f"{outdir}/zanrt_syncw.o"], check=True)

        # ohos: delegate to the proven NDK-free recipe rather than duplicating
        # its flag table here (same call shape as build_cross_rt.cmd's fallback).
        print("Building ohos (via scripts/build_ohos_rt_zig.sh)...")
        subprocess.run(["bash", "scripts/build_ohos_rt_zig.sh", zig.replace("\\", "/")], check=True)

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

@echo off
rem Windows companion to build_linux_rt.sh + build_macos_rt.sh: rebuilds the
rem committed cross-target runtime objects with zig cc. Those .sh scripts need a
rem POSIX shell, and the WSL bash on a Windows box cannot drive the Windows
rem zig.exe (it hands it /mnt/... paths), so keep this in step with them.
rem
rem   scripts\build_cross_rt.cmd [path-to-zig.exe]
rem
rem Run it after touching src\runtime\rt_{io,sync,file,timer}.c -- a stale object
rem fails the cross link with an undefined runtime symbol (a program compiled
rem for --target linux-x64 wanting zan_co_live_add, say), not with a warning.
rem Verify with: python scripts\check_toolchain_stale.py
setlocal
set ZIG=%~1
if "%ZIG%"=="" set ZIG=zig
set RT=src\runtime
where %ZIG% >nul 2>nul || if not exist "%ZIG%" (
  echo zig not found: pass the path to zig.exe as the first argument
  exit /b 1
)

for %%P in (linux-musl:x86_64 linux-arm64:aarch64 linux-riscv64:riscv64) do (
  for /f "tokens=1,2 delims=:" %%A in ("%%P") do (
    if not exist toolchain\%%A mkdir toolchain\%%A
    "%ZIG%" cc -target %%B-linux-musl -g0 -DZAN_IO_STACKLESS_ONLY -fPIC -I %RT% -O2 -c %RT%\rt_io.c    -o toolchain\%%A\zanrt_io.o    || exit /b 1
    "%ZIG%" cc -target %%B-linux-musl -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_sync.c  -o toolchain\%%A\zanrt_sync.o  || exit /b 1
    "%ZIG%" cc -target %%B-linux-musl -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_file.c  -o toolchain\%%A\zanrt_file.o  || exit /b 1
    "%ZIG%" cc -target %%B-linux-musl -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_timer.c -o toolchain\%%A\zanrt_timer.o || exit /b 1
    echo built toolchain\%%A
  )
)

rem -target <arch>-macos.11.0 stamps LC_BUILD_VERSION minos 11.0 to match the
rem -platform_version zanc passes to ld64.lld.
for %%P in (arm64:aarch64 x64:x86_64) do (
  for /f "tokens=1,2 delims=:" %%A in ("%%P") do (
    if not exist toolchain\macos\%%A mkdir toolchain\macos\%%A
    "%ZIG%" cc -target %%B-macos.11.0 -g0 -DZAN_IO_STACKLESS_ONLY -fPIC -I %RT% -O2 -c %RT%\rt_io.c -o toolchain\macos\%%A\zanrt_io.o || exit /b 1
    "%ZIG%" cc -target %%B-macos.11.0 -g0 -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER -fPIC -I %RT% -O2 -c %RT%\rt_io.c -o toolchain\macos\%%A\zanrt_io_mt.o || exit /b 1
    "%ZIG%" cc -target %%B-macos.11.0 -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_sync.c  -o toolchain\macos\%%A\zanrt_sync.o  || exit /b 1
    "%ZIG%" cc -target %%B-macos.11.0 -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_file.c  -o toolchain\macos\%%A\zanrt_file.o  || exit /b 1
    "%ZIG%" cc -target %%B-macos.11.0 -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_timer.c -o toolchain\macos\%%A\zanrt_timer.o || exit /b 1
    echo built toolchain\macos\%%A
  )
)

rem wasm32 (WASI): single-threaded, so no rt_io / rt_sync -- the wasm link
rem rejects those programs before the object would be needed (see main.c's
rem wasm_obj_refs_any gates). zanrt_wasm.o holds the libc ABI adapters
rem (rt_wasm.c) and is compiled by clang from the same sysroot; the file/timer
rem objects here are what every program links unconditionally.
rem zanrt_ehtag.o is NOT built here: it needs mozbuild clang's wasm EH backend
rem (zig's clang fails the same compile). Recipe:
rem   <mozbuild>\clang\bin\clang.exe --target=wasm32-unknown-wasi ^
rem     -fexceptions -mllvm -wasm-enable-eh -O2 -c ^
rem     toolchain\wasm32\zanrt_ehtag.c -o toolchain\wasm32\zanrt_ehtag.o
rem It defines the __cpp_exception tag and __cxa_throw for try/catch programs
rem (main.c links it only when wasm_eh_used).
if not exist toolchain\wasm32 mkdir toolchain\wasm32
"%ZIG%" cc -target wasm32-wasi -g0 -std=c11 -I %RT% -O2 -c %RT%\rt_file.c  -o toolchain\wasm32\zanrt_file.o  || exit /b 1
"%ZIG%" cc -target wasm32-wasi -g0 -std=c11 -I %RT% -O2 -c %RT%\rt_timer.c -o toolchain\wasm32\zanrt_timer.o || exit /b 1
echo built toolchain\wasm32

rem Android (bionic): zig cc has no bionic target, so this block needs the
rem Android NDK (set ANDROID_NDK to its root, e.g.
rem %LOCALAPPDATA%\Android\Sdk\ndk\<version>; defaults to the newest under the
rem SDK). API 28 minimum: rt_sync.c uses glob(), which bionic added in 28.
rem bionic has no shm_open -- the __ANDROID__ shim inside rt_sync.c backs it
rem with files under $ZAN_SHM_DIR (default /data/local/tmp). The NDK sysroot
rem subset itself (crtbegin_static.o, crtend_android.o, libc.a, libm.a,
rem libdl.a, libclang_rt.builtins-<arch>-android.a) is copied from
rem <NDK>\sysroot\usr\lib\<triple>\ and
rem <NDK>\lib\clang\<ver>\lib\linux\ into toolchain\android-<arch>\; libc.a is
rem run through llvm-objcopy --decompress-debug-sections (zstd) so any lld
rem links it. main.c never wraps malloc on Android (zanrt_mem.o + --wrap
rem crashes under bionic's TLS bootstrap), so rt_mem is not built here.
if "%ANDROID_NDK%"=="" (
  for /d %%D in ("%LOCALAPPDATA%\Android\Sdk\ndk\*") do set ANDROID_NDK=%%D
)
if not exist "%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe" (
  echo Android NDK not found: set ANDROID_NDK to the NDK root directory
  exit /b 1
)
set NDKBIN=%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin
for %%P in (android-x64:x86_64 android-arm64:aarch64) do (
  for /f "tokens=1,2 delims=:" %%A in ("%%P") do (
    if not exist toolchain\%%A mkdir toolchain\%%A
    "%NDKBIN%\clang.exe" --sysroot="%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot" -target %%B-linux-android28 -g0 -DZAN_IO_STACKLESS_ONLY -fPIC -I %RT% -O2 -c %RT%\rt_io.c    -o toolchain\%%A\zanrt_io.o    || exit /b 1
    "%NDKBIN%\clang.exe" --sysroot="%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot" -target %%B-linux-android28 -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_sync.c  -o toolchain\%%A\zanrt_sync.o  || exit /b 1
    "%NDKBIN%\clang.exe" --sysroot="%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot" -target %%B-linux-android28 -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_file.c  -o toolchain\%%A\zanrt_file.o  || exit /b 1
    "%NDKBIN%\clang.exe" --sysroot="%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot" -target %%B-linux-android28 -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_timer.c -o toolchain\%%A\zanrt_timer.o || exit /b 1
    "%NDKBIN%\clang.exe" --sysroot="%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot" -target %%B-linux-android28 -g0 -std=c11 -fPIC -I %RT% -I src\common -O2 -c %RT%\zan_embed_api.c -o toolchain\%%A\zan_embed_api.o || exit /b 1
    echo built toolchain\%%A
  )
)

rem OpenHarmony (OHOS): the NDK layout matches Android's (toolchains\llvm\
rem prebuilt\windows-x86_64\{bin,sysroot}) but the target triples end in
rem -linux-ohos and the sysroot libc is musl, so the same clang invocation
rem works with -target <arch>-linux-ohos (no API level). OHOS musl libc.a
rem exposes pthread/epoll; it lacks shm_open -- the __OHOS__ shim inside
rem rt_sync.c (shared with Android) backs shared tables with files under
rem $ZAN_SHM_DIR (default /data/local/tmp). Set OHOS_NDK to the NDK root
rem (the directory containing native\). The sysroot subset itself
rem (crt1.o crti.o crtn.o libc.a from sysroot\usr\lib\<triple>\,
rem clang_rt.crtbegin.o clang_rt.crtend.o libclang_rt.builtins.a from
rem llvm\lib\clang\<ver>\lib\<triple>\, libunwind.a from
rem llvm\lib\<triple>\) is copied into toolchain\ohos-<arch>\; libm.a and
rem libdl.a in the OHOS sysroot are empty 8-byte archives (math/dl symbols
rem live in libc.a), so they are not committed.
if "%OHOS_NDK%"=="" set OHOS_NDK=%ZAN_OHOS_SDK%
if not exist "%OHOS_NDK%\native\llvm\bin\clang.exe" (
  echo OHOS NDK not found: set OHOS_NDK to the OHOS SDK directory containing native
  exit /b 1
)
set OHOSBIN=%OHOS_NDK%\native\llvm\bin
set OHOSSYS=%OHOS_NDK%\native\sysroot
for %%P in (ohos-x64:x86_64-unknown-linux-ohos ohos-arm64:aarch64-unknown-linux-ohos) do (
  for /f "tokens=1,2 delims=:" %%A in ("%%P") do (
    if not exist toolchain\%%A mkdir toolchain\%%A
    "%OHOSBIN%\clang.exe" --sysroot="%OHOSSYS%" -target %%B -g0 -DZAN_IO_STACKLESS_ONLY -fPIC -I %RT% -O2 -c %RT%\rt_io.c    -o toolchain\%%A\zanrt_io.o    || exit /b 1
    "%OHOSBIN%\clang.exe" --sysroot="%OHOSSYS%" -target %%B -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_sync.c  -o toolchain\%%A\zanrt_sync.o  || exit /b 1
    "%OHOSBIN%\clang.exe" --sysroot="%OHOSSYS%" -target %%B -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_file.c  -o toolchain\%%A\zanrt_file.o  || exit /b 1
    "%OHOSBIN%\clang.exe" --sysroot="%OHOSSYS%" -target %%B -g0 -std=c11 -fPIC -I %RT% -O2 -c %RT%\rt_timer.c -o toolchain\%%A\zanrt_timer.o || exit /b 1
    "%OHOSBIN%\clang.exe" --sysroot="%OHOSSYS%" -target %%B -g0 -std=c11 -fPIC -I %RT% -I src\common -O2 -c %RT%\zan_embed_api.c -o toolchain\%%A\zan_embed_api.o || exit /b 1
    echo built toolchain\%%A
  )
)

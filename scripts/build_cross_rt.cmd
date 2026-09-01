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
if not exist toolchain\wasm32 mkdir toolchain\wasm32
"%ZIG%" cc -target wasm32-wasi -g0 -std=c11 -I %RT% -O2 -c %RT%\rt_file.c  -o toolchain\wasm32\zanrt_file.o  || exit /b 1
"%ZIG%" cc -target wasm32-wasi -g0 -std=c11 -I %RT% -O2 -c %RT%\rt_timer.c -o toolchain\wasm32\zanrt_timer.o || exit /b 1
echo built toolchain\wasm32

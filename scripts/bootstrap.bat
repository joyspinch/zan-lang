@echo off
rem Reproduce the self-hosting closure on Windows.
rem
rem   gen0  = the C host compiler (build\zanc.exe), already built via CMake
rem   gen1  = gen0 compiling src\selfhost\*.zan into a native executable
rem   g2.ll = gen1 compiling its own source to LLVM IR text
rem   gen2  = clang linking g2.ll into a native executable
rem   g3.ll = gen2 compiling the same source to LLVM IR text
rem
rem Success criterion: g2.ll and g3.ll are byte-identical (gen2 == gen3).
rem
rem Requires: a built build\zanc.exe (gen0) and clang on PATH. The large stack
rem reservation matches the one the host bakes into its own link step.
rem
rem The source list is read from scripts\selfhost_sources.txt -- the same file
rem the ctest harnesses use -- so the closure always covers exactly the
rem sources the project ships (the hand-copied list here used to lag behind
rem and silently covered only a 9-file subset).
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set CLANG=clang
if defined SELFHOST_CLANG set CLANG=%SELFHOST_CLANG%
set STACK=-Xlinker /STACK:268435456

rem ---- source list (one canonical list: scripts\selfhost_sources.txt) ----
set SRCS=
for /f "usebackq eol=# tokens=*" %%L in ("scripts\selfhost_sources.txt") do (
  set "L=%%L"
  set "L=!L: =!"
  if not "!L!"=="" set "SRCS=!SRCS! src\!L!.zan"
)
if "%SRCS%"=="" (
  echo no sources listed in scripts\selfhost_sources.txt 1>&2
  exit /b 1
)

rem ---- runtime objects for gen2 (same set the host links for a program) ----
call :rt rt_file
call :rt rt_io
call :rt rt_sched
call :rt rt_timer
call :rt rt_decimal
call :rt rt_sync
call :rt zan_embed_api
set RTOBJS=%RTOBJS% build\zanrt_sh_%1.obj

echo [1/5] gen0 -^> gen1
build\zanc.exe %SRCS% --auto-stdlib -o build\zanc1.exe || exit /b 1

echo [2/5] gen1 -^> g2.ll
del build\g2.ll 2>nul
build\zanc1.exe build\g2.ll %SRCS% || exit /b 1

echo [3/5] clang g2.ll -^> gen2
%CLANG% build\g2.ll %RTOBJS% -lkernel32 -lshell32 -luser32 -ladvapi32 -lversion -o build\zanc2.exe %STACK% || exit /b 1

echo [4/5] gen2 -^> g3.ll
del build\g3.ll 2>nul
build\zanc2.exe build\g3.ll %SRCS% || exit /b 1

echo [5/5] compare g2.ll and g3.ll
fc /b build\g2.ll build\g3.ll >nul
if errorlevel 1 (
  echo FAILURE: gen2 != gen3
  exit /b 1
) else (
  echo SUCCESS: gen2 == gen3 (byte-identical^)
)
exit /b 0

:rt
if not exist "build\zanrt_sh_%1.obj" (
  %CLANG% -O2 -c src\runtime\%1.c -o build\zanrt_sh_%1.obj || exit /b 1
)
goto :eof

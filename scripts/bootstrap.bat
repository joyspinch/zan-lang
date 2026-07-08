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
setlocal
cd /d "%~dp0.."
set SRC=src\selfhost\main.zan src\selfhost\irgen.zan src\selfhost\checker.zan src\selfhost\binder.zan src\selfhost\diag.zan src\selfhost\parser.zan src\selfhost\lexer.zan src\selfhost\ast.zan src\selfhost\token.zan
set STACK=-Xlinker /STACK:268435456

echo [1/5] gen0 -^> gen1
build\zanc.exe %SRC% -o build\zanc1.exe || exit /b 1

echo [2/5] gen1 -^> g2.ll
del build\g2.ll 2>nul
build\zanc1.exe build\g2.ll %SRC% || exit /b 1

echo [3/5] clang g2.ll -^> gen2
clang build\g2.ll -o build\zanc2.exe %STACK% || exit /b 1

echo [4/5] gen2 -^> g3.ll
del build\g3.ll 2>nul
build\zanc2.exe build\g3.ll %SRC% || exit /b 1

echo [5/5] compare g2.ll and g3.ll
fc /b build\g2.ll build\g3.ll >nul
if errorlevel 1 (
  echo FAILURE: gen2 != gen3
  exit /b 1
) else (
  echo SUCCESS: gen2 == gen3 (byte-identical^)
)

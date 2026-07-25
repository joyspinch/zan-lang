# Builds build\mge.exe (tools\mge -- the Legend/Mir data editor) as a single
# self-contained Windows app: the native GUI runtime is linked statically, so
# the tool carries no zan_gui.dll dependency and shows no console window.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Output "[1/2] Building native GUI runtime (static, mingw ABI)..."
clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
    -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 `
    -c src\runtime\gui_runtime.c -o build\zan_gui_mge_gnu.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_COMPILE_FAILED"; exit 1 }
llvm-ar rcs build\libzan_gui_mge_gnu.a build\zan_gui_mge_gnu.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_LIB_FAILED"; exit 1 }

Write-Output "[2/2] Compiling and linking tools\mge\Mge.zan ..."
build\zanc.exe tools\mge\Mge.zan --auto-stdlib -o build\mge.exe `
    --subsystem windows `
    --libpath build --link-lib zan_gui_mge_gnu `
    --link-lib gdi32 --link-lib imm32 --link-lib dwmapi `
    --link-lib user32 --link-lib msimg32
if ($LASTEXITCODE -ne 0) { Write-Output "MGE_LINK_FAILED"; exit 1 }

Write-Output "MGE_BUILD_OK: build\mge.exe"

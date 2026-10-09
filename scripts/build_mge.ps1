# Builds build\mge.exe (tools\mge -- the Legend/Mir data editor) as a single
# self-contained Windows app: the native GUI runtime is linked statically, so
# the tool carries no zan_gui.dll dependency and shows no console window.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Output "[1/2] Building independent native drivers (static, mingw ABI)..."
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
    -Drivers @("zan_gui", "zan_image", "zan_audio")

Write-Output "[2/2] Compiling and linking tools\mge\Mge.zan ..."
build\zanc.exe tools\mge\Mge.zan --auto-stdlib -o build\mge.exe `
    --subsystem windows `
    --link-mode static `
    --link-lib gdi32 --link-lib imm32 --link-lib dwmapi `
    --link-lib user32 --link-lib msimg32
if ($LASTEXITCODE -ne 0) { Write-Output "MGE_LINK_FAILED"; exit 1 }

Write-Output "MGE_BUILD_OK: build\mge.exe"

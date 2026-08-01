# Zgm UI backend build profiles. Selects the render backend at compile time.
#
#   .\scripts\ui_build.ps1 -mode gpu -src <your.zan>       # SDL3 + GPU
#   .\scripts\ui_build.ps1 -mode software -src <your.zan>  # pure CPU rasterizer
#   .\scripts\ui_build.ps1 -mode headless -src <your.zan>  # no window (CI/tools)
#
# `-src` is required: the old examples/ui_backend/ui_render_demo.zan smoke
# demo was removed, so pass the Zan program you want to build against the
# backend.
#
# Only the -mode gpu profile passes the SDL3 backend file (UiRenderSdl.zan) and
# defines UI_GPU, so software/headless binaries never pull in stdlib/SDL3.
param(
    [ValidateSet("gpu", "software", "headless")]
    [string]$mode = "headless",
    [string]$src  = "",
    [string]$out  = ""
)

$ErrorActionPreference = "Stop"
if ($src -eq "") {
    Write-Error "ui_build.ps1: -src is required (e.g. -src examples\my_demo.zan)"
    exit 1
}
$root = Split-Path -Parent $PSScriptRoot
$zanc = Join-Path $root "build\zanc.exe"
$sdlBackend = Join-Path $root "stdlib\Game\Zgm\optional\UiRenderSdl.zan"
if ($out -eq "") { $out = Join-Path $root ("build\ui_" + $mode + ".exe") }
$srcPath = Join-Path $root $src

switch ($mode) {
    "gpu" {
        & $zanc $srcPath $sdlBackend --auto-stdlib -DUI_GPU -o $out
    }
    "software" {
        & $zanc $srcPath --auto-stdlib -DUI_SOFTWARE -o $out
    }
    default {
        & $zanc $srcPath --auto-stdlib -o $out
    }
}
Write-Output ("mode=" + $mode + " exit=" + $LASTEXITCODE + " out=" + $out)

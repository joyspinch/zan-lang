# Zgm UI backend build profiles. Selects the render backend at compile time.
#
#   .\build\ui_build.ps1 -mode gpu       # SDL3 + GPU-accelerated renderer
#   .\build\ui_build.ps1 -mode software  # pure CPU rasterizer, no SDL3
#   .\build\ui_build.ps1 -mode headless  # no window, no SDL3 (CI / tools)
#
# Only the -mode gpu profile passes the SDL3 backend file (UiRenderSdl.zan) and
# defines UI_GPU, so software/headless binaries never pull in stdlib/SDL3.
param(
    [ValidateSet("gpu", "software", "headless")]
    [string]$mode = "headless",
    [string]$src  = "examples\ui_backend\ui_render_demo.zan",
    [string]$out  = ""
)

$ErrorActionPreference = "Stop"
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

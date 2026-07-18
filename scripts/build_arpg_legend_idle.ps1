param(
    [switch]$Run,
    [switch]$Smoke
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$zanc = Join-Path $root "build\zanc.exe"
$source = Join-Path $root "examples\arpg_legend_idle\main.zan"
$output = Join-Path $root "build\arpg_legend_idle_demo.exe"
$driver = Join-Path $root "stdlib\SDL3\drivers\win-x64"

if (!(Test-Path -LiteralPath $zanc)) {
    throw "zanc was not found: $zanc"
}

& $zanc $source -o $output
if ($LASTEXITCODE -ne 0) {
    throw "ARPG Legend Idle demo compilation failed."
}

foreach ($name in @("zan_sdl3.dll", "SDL3.dll")) {
    $path = Join-Path $driver $name
    if (!(Test-Path -LiteralPath $path)) {
        throw "SDL3 runtime was not staged: $path"
    }
    Copy-Item -LiteralPath $path -Destination (Join-Path $root "build\$name") -Force
}

Write-Output "ARPG_LEGEND_IDLE_COMPILED=$output"
if ($Run) {
    if ($Smoke) {
        & $output --smoke
    } else {
        & $output
    }
    if ($LASTEXITCODE -ne 0) {
        throw "ARPG Legend Idle demo exited with code $LASTEXITCODE."
    }
}

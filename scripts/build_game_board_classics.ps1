param(
    [switch]$Run,
    [switch]$Smoke
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$zanc = Join-Path $root "build\zanc.exe"
$sourceRoot = Join-Path $root "examples\game\board\classics"
$sources = @(
    (Join-Path $sourceRoot "Shared.zan"),
    (Join-Path $sourceRoot "Xiangqi.zan"),
    (Join-Path $sourceRoot "Gomoku.zan"),
    (Join-Path $sourceRoot "main.zan")
)
$output = Join-Path $root "build\game_board_classics.exe"
$driver = Join-Path $root "stdlib\SDL3\drivers\win-x64"

if (!(Test-Path -LiteralPath $zanc)) {
    throw "zanc was not found: $zanc"
}

& $zanc $sources -o $output
if ($LASTEXITCODE -ne 0) {
    throw "Classic board hall compilation failed."
}

foreach ($name in @("zan_sdl3.dll", "SDL3.dll")) {
    $path = Join-Path $driver $name
    if (!(Test-Path -LiteralPath $path)) {
        throw "SDL3 runtime was not staged: $path"
    }
    Copy-Item -LiteralPath $path -Destination (Join-Path $root "build\$name") -Force
}

Write-Output "GAME_BOARD_CLASSICS_COMPILED=$output"
if ($Run) {
    if ($Smoke) {
        & $output --smoke
    } else {
        & $output
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Classic board hall exited with code $LASTEXITCODE."
    }
}

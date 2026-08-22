# Standard Zan GUI project build for oneplus\app (群爆款优化神器).
#
# oneplus\app is a normal zan.proj project: `entry` names the program's Main
# (src\OnePlusApp.zan), every .zform under src\ is a designed document whose
# code-behind is the same-named .zan, and the custom components (pages,
# toolbars, the command bar) are discovered by scan_components.ps1 so the
# designer palette and the JSON loader know them.
#
#   -Console  build a console-subsystem exe so Console.WriteLine (CEF bootstrap
#             diagnostics) shows up in a redirected log.
param(
    [switch]$Console
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Output "ONEPLUS_BUILD_START"

$proj = Join-Path $root "oneplus\app"
$zanc = if (Test-Path "build\zanc.exe") { "build\zanc.exe" } else { "dist\win-x64\toolchain\zanc.exe" }
Write-Output "[zanc] $zanc"

# Project components (pages/toolbars marked with `/// @component`): the registry
# is what lets ControlFactory build them from a .zform `kind`, so an empty one
# means every designed page silently loses its custom children.
$registryPath = Join-Path $root "build\ProjectComponents.oneplus.zan"
& powershell -ExecutionPolicy Bypass -File scripts\scan_components.ps1 `
    -Source "oneplus\app\src" -Out "build\ProjectComponents.oneplus.zan"
if ($LASTEXITCODE -ne 0) { Write-Output "COMPONENT_SCAN_FAILED"; exit 1 }
$registryText = [System.IO.File]::ReadAllText($registryPath)
$componentCount = ([regex]::Matches($registryText, 'k\.Add\(')).Count
if ($componentCount -lt 1) { Write-Output "COMPONENT_REGISTRY_EMPTY"; exit 1 }
Write-Output "[components] $componentCount"

# Entry FIRST: zanc gives the generated Main() to the first document on the
# command line, so the manifest's `entry` has to lead the input list.
$entryRel = ([regex]::Match(
    [System.IO.File]::ReadAllText((Join-Path $proj "zan.proj")),
    '(?m)^\s*entry\s*=\s*(.+?)\s*$')).Groups[1].Value
if ($entryRel -eq "") { Write-Output "PROJECT_ENTRY_MISSING"; exit 1 }
$entry = Join-Path $proj ($entryRel -replace '/', '\')
if (!(Test-Path -LiteralPath $entry)) { Write-Output "PROJECT_ENTRY_NOT_FOUND $entry"; exit 1 }
Write-Output "[entry] $entry"

$files = @()
# -File, not -Include: oneplus\app\.zan is the IDE's project-state DIRECTORY and
# -Include would hand it to the compiler as a source path.
$files += @(Get-ChildItem $proj -Recurse -File -Filter *.zan |
    Where-Object { $_.FullName -ne $entry } |
    ForEach-Object { $_.FullName })
$files += $registryPath
$files += @(Get-ChildItem $proj -Recurse -File -Filter *.zform |
    ForEach-Object { $_.FullName })

$exeOut = Join-Path $root "build\oneplus.exe"
$zanArgs = @($entry) + $files
$zanArgs += @("--auto-stdlib", "--stdlib-path", (Join-Path $root "stdlib"))
$zanArgs += @("-DZAN_PROJECT_COMPONENTS")
$zanArgs += @("-o", $exeOut)
if (-not $Console) { $zanArgs += @("--subsystem", "windows") }
$zanArgs += @("--no-arc-guard", "--no-check-leaks")
$zanArgs += @("--icon", (Join-Path $root "assets\zan.ico"))
if ($env:ZAN_ONEPLUS_ZANC_ARGS) {
    $zanArgs += ($env:ZAN_ONEPLUS_ZANC_ARGS -split '\s+' | Where-Object { $_ })
}

# A running instance holds a lock on its own image; park it aside so a rebuild
# never needs the app to be closed first.
$exeOld = Join-Path $root "build\oneplus.prev.exe"
if (Test-Path -LiteralPath $exeOld) {
    Remove-Item -LiteralPath $exeOld -Force -ErrorAction SilentlyContinue
}
if (Test-Path -LiteralPath $exeOut) {
    Move-Item -LiteralPath $exeOut -Destination $exeOld -Force -ErrorAction SilentlyContinue
}

$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$out = & $zanc @zanArgs 2>&1
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($code -ne 0) {
    $out | Select-String -Pattern 'error|warning' | ForEach-Object { $_.Line }
    $out | Select-Object -Last 10
    Write-Output "ONEPLUS_LINK_FAILED code=$code"
    exit 1
}
$out | Select-Object -Last 6

Write-Output "ONEPLUS_BUILD_OK build\oneplus.exe"

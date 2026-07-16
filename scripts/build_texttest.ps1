$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $root 'build')
$src = Join-Path $root 'tests\conformance\text_test.zan'
$dep = Join-Path $root 'stdlib\Gui\Text.zan'
$ir = & .\zanc.exe --emit-ir $src $dep
if ($LASTEXITCODE -ne 0) { Write-Output "IR_FAILED"; $ir; exit 1 }
[System.IO.File]::WriteAllLines((Join-Path (Get-Location) "text_test.ll"), $ir)
clang text_test.ll -o text_test.exe -O2
if ($LASTEXITCODE -ne 0) { Write-Output "LINK_FAILED"; exit 1 }
& .\text_test.exe

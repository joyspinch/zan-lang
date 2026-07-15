$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $root 'build')
$src = Join-Path $root 'examples\json_test.zan'
$dep = Join-Path $root 'stdlib\System\Json\JsonValue.zan'
$ir = & .\zanc.exe --emit-ir $src $dep
if ($LASTEXITCODE -ne 0) { Write-Output "IR_FAILED"; $ir; exit 1 }
[System.IO.File]::WriteAllLines((Join-Path (Get-Location) "json_test.ll"), $ir)
clang json_test.ll -o json_test.exe -O2
if ($LASTEXITCODE -ne 0) { Write-Output "LINK_FAILED"; exit 1 }
& .\json_test.exe

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $root 'build')
$src = Join-Path $root 'examples\proc_test.zan'
$deps = @(
  (Join-Path $root 'stdlib\System\Diagnostics\Process.zan'),
  (Join-Path $root 'stdlib\System\IO\File.zan'),
  (Join-Path $root 'stdlib\System\IO\Directory.zan')
)
$ir = & .\zanc.exe --emit-ir $src @deps
if ($LASTEXITCODE -ne 0) { Write-Output "IR_FAILED"; $ir; exit 1 }
[System.IO.File]::WriteAllLines((Join-Path (Get-Location) "proc_test.ll"), $ir)
clang proc_test.ll -o proc_test.exe -O2
if ($LASTEXITCODE -ne 0) { Write-Output "LINK_FAILED"; exit 1 }
& .\proc_test.exe

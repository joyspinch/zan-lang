# scripts/pkg_tests.ps1 — 包内无头测试运行器（规则 14：包的测试跑在包上）。
#
# 约定：packages/<Pkg>/tests/*.zan 每个文件是一个独立 Zan.Test 程序
#   （using System.Test + 断言 + Test.Finish() 收尾，以失败数为退出码）。
#   本脚本逐个编译并运行，以退出码与非零失败数判定红绿。
#
# 用法：
#   pwsh -File scripts/pkg_tests.ps1                 # 跑全部包测试
#   pwsh -File scripts/pkg_tests.ps1 -Filter Zan.Csv # 只跑名字匹配的包
#
# 说明：直接调 zanc 编译（不经 cmake --build），不与构建闸互斥；
#   可执行文件落在 _scratch\pkg_tests\（git-ignored，固定名可复用）。
param([string]$Filter = "")
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot
$zanc = Join-Path $repo "build\zanc.exe"
if (-not (Test-Path $zanc)) {
    Write-Error "zanc not found: $zanc (先跑 scripts\build.ps1)"
    exit 1
}
$work = Join-Path $repo "_scratch\pkg_tests"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$tests = Get-ChildItem (Join-Path $repo "packages\*\tests\*.zan") |
    Where-Object { $Filter -eq "" -or $_.FullName -match [regex]::Escape($Filter) }
if ($tests.Count -eq 0) {
    Write-Host "no package tests found (filter: '$Filter')"
    exit 0
}
$failed = 0
$ran = 0
foreach ($t in $tests) {
    $pkg = Split-Path -Parent (Split-Path -Parent $t.FullName)
    $pkg = Split-Path -Leaf $pkg
    $exe = Join-Path $work ("$pkg." + $t.BaseName + ".exe")
    $ran = $ran + 1
    Write-Host "== $pkg : $($t.Name)"
    $clog = & $zanc $t.FullName --package-project $repo --auto-stdlib -o $exe 2>&1
    if ($LASTEXITCODE -ne 0) {
        $failed = $failed + 1
        $clog | ForEach-Object { Write-Host "  COMPILE: $_" }
        continue
    }
    $out = & $exe 2>&1
    $out | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { $failed = $failed + 1 }
}
Write-Host ""
Write-Host "pkg-tests: $ran files, $failed failed"
if ($failed -gt 0) { exit 1 }
exit 0

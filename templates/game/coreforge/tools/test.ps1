# coreforge 逻辑套件（无头）：编译 tests/ 里的 logic.zan，跑 240 条断言。
#
# 断言全在 packages/Zan.Game.Factory（逻辑层）里，界面壳不参与——所以这套用例
# 与 GUI 无关，改逻辑层后随时可以跑。
param(
    [string]$Compiler = $env:ZANC,
    [string]$PackageProject = ''
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$repo = [IO.Path]::GetFullPath((Join-Path $project '../../..'))
$tests = Join-Path $repo 'tests/templates/coreforge'
if (-not $Compiler) {
    $local = Join-Path $repo 'build/zanc.exe'
    if (Test-Path $local) { $Compiler = $local } else { $Compiler = (Get-Command zanc -ErrorAction Stop).Source }
}
if (-not $PackageProject) { $PackageProject = $repo }
& python (Join-Path $tests 'verify.py') --compiler $Compiler --package-project $PackageProject
if ($LASTEXITCODE -ne 0) { throw "coreforge logic suite failed ($LASTEXITCODE)" }

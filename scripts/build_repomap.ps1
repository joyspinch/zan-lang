# build_repomap.ps1 - 重建 tools/repomap 并生成 .zanmap（稳定入口）。
#
# 为什么要有这个脚本：RepoMap 在 c6413641 / 85938c5a 被拆成 1:1 单体类多文件
# （RepoMap.zan + Str.zan + Models/*.zan），而 zanc **不**自动拉取同项目的兄弟
# .zan。任何还在宣传 `zanc tools/repomap/RepoMap.zan ...` 单文件形式的提示都已
# 失效（会报一堆 undefined type 'RefBucket'）。枚举源这件事只该有一份实现。
#
#   scripts\build_repomap.ps1            # 生成 .zanmap（根 = 仓库根）
#   scripts\build_repomap.ps1 -Target stdlib   # 只索引子目录
[CmdletBinding()]
param(
    [string]$Target = '.',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

$Zanc = Join-Path $Root 'build\zanc.exe'
if (-not (Test-Path $Zanc)) { $Zanc = Join-Path $Root 'toolchain\zanc.exe' }
if (-not (Test-Path $Zanc)) { Write-Error "zanc not found (build/ or toolchain/)"; exit 2 }

$ToolDir = Join-Path $Root 'tools\repomap'
$Exe = Join-Path $Root 'build\repomap.exe'

if (-not $SkipBuild) {
    $srcs = @(Get-ChildItem -Recurse -File -Filter *.zan $ToolDir |
              ForEach-Object { $_.FullName -replace '\\', '/' })
    if ($srcs.Count -eq 0) { Write-Error "no .zan under $ToolDir"; exit 2 }

    # zanc 在 PS 5.1 下用 Start-Process 取不到 ExitCode，直接调用即可
    $out = & $Zanc @srcs --auto-stdlib -o $Exe 2>&1 | Out-String
    $bad = ($LASTEXITCODE -ne 0) -or ($out -match '\b[1-9]\d*\s+error\(s\)') -or ($out -match '(?m)^\S.*?:\d+:\d+:\s*error:')
    if ($bad) {
        Write-Output ($out -split "`n" | Where-Object { $_ -match 'error' } | Select-Object -First 10)
        Write-Error "repomap failed to build ($($srcs.Count) sources)"
        exit 1
    }
    Write-Output ("[repomap] built from {0} sources -> {1}" -f $srcs.Count, $Exe)
}

if (-not (Test-Path $Exe)) { Write-Error "no repomap.exe at $Exe"; exit 2 }

Push-Location $Root
try {
    & $Exe $Target
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $code

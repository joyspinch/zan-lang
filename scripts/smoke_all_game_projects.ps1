<#
    smoke_all_game_projects.ps1 — 批量跑 D:/project/games 下"阶段一骨架"项目的
    构建 + 无头冒烟 + 存档落盘验证。

    为什么要有它：这十个项目同构（单文件 main.zan + Canvas 直绘 + smoke 冒烟），
    改动其中一处（例如公共包升级）后要十个一起验；逐个手敲命令容易漏。
    每个项目的 test.ps1 自己会先构建再跑断言，所以本脚本只负责串联与汇总。

    用法：
      pwsh -File scripts\smoke_all_game_projects.ps1
      pwsh -File scripts\smoke_all_game_projects.ps1 -Projects arena,crawler
      pwsh -File scripts\smoke_all_game_projects.ps1 -Json
#>
[CmdletBinding()]
param(
    [string]$GamesRoot = 'D:\project\games',
    # 要跑的项目目录名（默认：阶段一骨架的十个单文件项目）
    [string[]]$Projects = @('arena', 'cardarena', 'crawler', 'hacker', 'mahjong',
        'platformer', 'powder', 'racekart', 'rail', 'voxel'),
    # zan-lang 仓库根（编译器与公共包在它下面）
    [string]$ZanHome = 'D:\project\zan-lang',
    # 公共包解析根（默认 = zan-lang 仓库根；公共包正被并行改动时，
    # 可指向一个干净的 git worktree，避免用半改动的包树编译）
    [string]$PackageProject = '',
    [switch]$Json
)

$ErrorActionPreference = 'Continue'
$env:ZAN_HOME = $ZanHome
if (-not $PackageProject) { $PackageProject = $ZanHome }
$results = New-Object System.Collections.Generic.List[object]

foreach ($p in $Projects) {
    $proj = Join-Path $GamesRoot $p
    $test = Join-Path $proj 'client\tools\test.ps1'
    if (-not (Test-Path $test)) {
        $results.Add([pscustomobject]@{ project = $p; ok = $false; note = 'no test.ps1'; log = '' })
        Write-Host "== $p : SKIP (no test.ps1)"
        continue
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $out = & pwsh -NoProfile -File $test -PackageProject $PackageProject 2>&1
    $sw.Stop()
    $text = ($out | ForEach-Object { $_.ToString() }) -join "`n"
    $ok = $LASTEXITCODE -eq 0 -and $text -match '_SMOKE_OK'
    $note = if ($text -match '_SAVE_OK') { 'smoke+save' } elseif ($ok) { 'smoke' } else { 'FAILED' }
    $results.Add([pscustomobject]@{
            project = $p
            ok = $ok
            note = $note
            log = (($out | Select-String -Pattern '_SMOKE_OK|_SAVE_OK|error|Exception' |
                    Select-Object -First 6) | ForEach-Object { $_.ToString() }) -join ' | '
        })
    $mark = if ($ok) { 'OK  ' } else { 'FAIL' }
    Write-Host ("== {0} {1} ({2}) {3}" -f $p, $mark, $note, [int]$sw.Elapsed.TotalSeconds)
    if (-not $ok) {
        ($out | Select-Object -Last 12) | ForEach-Object { Write-Host "   $_" }
    }
}

$failed = @($results | Where-Object { -not $_.ok })
Write-Host ''
Write-Host ("projects={0} passed={1} failed={2}" -f $results.Count,
    ($results.Count - $failed.Count), $failed.Count)

if ($Json) {
    $results | ConvertTo-Json -Depth 4
}
exit $(if ($failed.Count -gt 0) { 1 } else { 0 })

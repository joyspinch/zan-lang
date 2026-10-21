<#
.SYNOPSIS
    外部游戏项目的"商业级四口径"体检。

.DESCRIPTION
    口径（就这四条，平台侧能力如内购/联网/成就不在范围内）：
      1. 能跑     —— 有 client/tools/build.ps1 且能构建出可执行文件；
      2. 有存档   —— 源码里有持久化（写文件 / Save / 存档）；
      3. 有设置   —— 源码里有可改选项（设置 / Settings / Options / Config）；
      4. 单文件发布 —— build.ps1 支持透传 --publish，产出自包含 exe。

    静态扫描源码给出"缺口在哪"，不替代实跑；发布能力用
    `-ZancArgs '--publish'` 实跑验证更可靠（脚本会提示）。

.EXAMPLE
    pwsh -File scripts\audit_game_projects.ps1
    pwsh -File scripts\audit_game_projects.ps1 -GamesRoot D:\project\games -Json
#>
[CmdletBinding()]
param(
    [string]$GamesRoot = 'D:\project\games',
    [switch]$Json
)
$ErrorActionPreference = 'Continue'

if (-not (Test-Path $GamesRoot)) { Write-Error "games root not found: $GamesRoot"; exit 2 }

# 目录名不在此列表的算游戏项目（工具/临时目录排除）。
$skip = @('_mercury3', '_scratch', 'tools', 'node_modules')

$projects = Get-ChildItem $GamesRoot -Directory |
    Where-Object { $skip -notcontains $_.Name } |
    Sort-Object Name

$rows = @()
foreach ($p in $projects) {
    $files = Get-ChildItem $p.FullName -Recurse -File -Include *.zan -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'build|obj|bin|\.git' }
    $text = ($files | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"

    $save = ($text -match 'File\.WriteAllText|File\.WriteAllBytes|Serialize|Save\(|SaveGame|存档|\.Save\(')
    $setting = ($text -match '设置|Settings|Options|Config|音量|musicVol|soundVol')

    $build = Get-ChildItem $p.FullName -Recurse -File -Filter 'build.ps1' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'obj' } | Select-Object -First 1
    $publish = $false
    if ($build) {
        $publish = ([IO.File]::ReadAllText($build.FullName) -match '--publish')
    }
    $test = Get-ChildItem $p.FullName -Recurse -File -Filter 'test.ps1' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'obj' } | Select-Object -First 1

    $rows += [pscustomobject]@{
        project = $p.Name
        zan = $files.Count
        save = $save
        setting = $setting
        publish = $publish
        build = [bool]$build
        test = [bool]$test
    }
}

if ($Json) {
    $rows | ConvertTo-Json -Depth 3
}
else {
    $rows | Format-Table -AutoSize
    Write-Host ''
    Write-Host ("projects={0}  build={1}  test={2}  save={3}  setting={4}  publish={5}" -f `
            $rows.Count,
            ($rows | Where-Object { $_.build }).Count,
            ($rows | Where-Object { $_.test }).Count,
            ($rows | Where-Object { $_.save }).Count,
            ($rows | Where-Object { $_.setting }).Count,
            ($rows | Where-Object { $_.publish }).Count)
    $missing = $rows | Where-Object { -not $_.publish -or -not $_.save -or -not $_.setting }
    foreach ($m in $missing) {
        $gap = @()
        if (-not $m.save) { $gap += '存档' }
        if (-not $m.setting) { $gap += '设置' }
        if (-not $m.publish) { $gap += '单文件发布' }
        Write-Host ("GAP {0,-12} {1}" -f $m.project, ($gap -join '/'))
    }
}

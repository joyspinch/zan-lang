<#
.SYNOPSIS
    外部游戏项目的"基础设施四口径"体检 + 美术资产盘点。

.DESCRIPTION
    **先说清这是什么**：下面四条量的是**基础设施**，不是游戏品质。
      1. 能跑     —— 有 client/tools/build.ps1 且能构建出可执行文件；
      2. 有存档   —— 源码里有持久化（写文件 / Save / 存档）；
      3. 有设置   —— 源码里有可改选项（设置 / Settings / Options / Config）；
      4. 单文件发布 —— build.ps1 支持透传 --publish，产出自包含 exe。

    这四条全绿 **只说明"这是个能交付的程序"**，不说明"这是个游戏"——
    给一个没有玩法的占位骨架加上存档和设置，它同样四条全绿。
    所以本脚本另起一列 `art`（位图/音频资产数）：**有真实美术资产才谈得
    上"游戏级"**。两个数一起看：基础设施全绿但 art=0 的，是原型，不是游戏。

    静态扫描源码给出"缺口在哪"，不替代实跑；发布能力用
    `-ZancArgs '--publish'` 实跑验证更可靠（脚本会提示）。

.EXAMPLE
    pwsh -File scripts\audit_game_projects.ps1
    pwsh -File scripts\audit_game_projects.ps1 -GamesRoot D:\project\games -Json
#>
$defaultRepoRoot = Split-Path -Parent $PSScriptRoot
$defaultGamesRoot = if ($env:ZAN_GAMES_ROOT) { $env:ZAN_GAMES_ROOT } elseif (Test-Path (Join-Path (Split-Path -Parent $defaultRepoRoot) 'games')) { (Join-Path (Split-Path -Parent $defaultRepoRoot) 'games') } else { 'D:\project\games' }

[CmdletBinding()]
param(
    [string]$GamesRoot = $defaultGamesRoot,
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
        Where-Object { $_.FullName -notmatch 'build|obj|bin|\.git|[\\/]_scratch[\\/]' }
    $text = ($files | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"

    $save = ($text -match 'File\.WriteAllText|File\.WriteAllBytes|Serialize|Save\(|SaveGame|存档|\.Save\(')
    $setting = ($text -match '设置|Settings|Options|Config|音量|musicVol|soundVol')

    $build = Get-ChildItem $p.FullName -Recurse -File -Filter 'build.ps1' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'obj' } | Select-Object -First 1
    $publish = $false
    $passthrough = $false
    if ($build) {
        $bt = [IO.File]::ReadAllText($build.FullName)
        # 发布能力看编译命令行里有没有 --publish（硬编码或经 -ZancArgs 透传都算）。
        # 另记"有没有参数透传口"：没有的话要发布只能改脚本，命令行调不出来。
        $publish = ($bt -match '--publish')
        $passthrough = ($bt -match '\+=\s*\$ZancArgs|@ZancArgs')
    }
    $test = Get-ChildItem $p.FullName -Recurse -File -Filter 'test.ps1' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'obj' } | Select-Object -First 1

    # 美术资产数：位图 + 音频。**这是"像不像游戏"的第一道硬门槛**——
    # 四条基础设施全绿但 art=0 的项目只是可运行的原型（几何图形 + TODO）。
    # 只数**入库资产**：build/（编译产物）与 _scratch/（一次性原图与脚本）
    # 都不算——原图是 1024² 的未处理素材，算进来会把"实际入库 10 张"报成 21 张。
    $artFiles = @(Get-ChildItem $p.FullName -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.FullName -notmatch 'build|obj|bin|\.git|[\\/]_scratch[\\/]' -and
            $_.Extension -match '^\.(png|jpg|jpeg|webp|bmp|wav|ogg|mp3)$'
        })
    $art = $artFiles.Count
    $bitmaps = @($artFiles | Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|webp|bmp)$' }).Count
    $audio = @($artFiles | Where-Object { $_.Extension -match '^\.(wav|ogg|mp3)$' }).Count

    # 位图**真的进了渲染路径**吗？资产目录里躺着图、源码里一次都没提过，
    # 和没有资产是一回事（几何色块 + 摆设素材）。wired 数源码里的图片路径
    # 字面量：直绘贴图（CDraw.BlitImage）、精灵批（SpriteHandle）、
    # HTML 面板背景图（CSS background-image）走的都是这条路径。
    $wired = ($text -match '\.png|\.jpg|\.webp')
    # 玩法断言用例：有 tests/ 下的 .zan 断言文件，说明玩法被规则锁住了，
    # 不只是"能跑起来"。
    $playtest = @(Get-ChildItem $p.FullName -Recurse -File -Filter *.zan -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '[\\/]tests[\\/]' -and $_.FullName -notmatch 'build|obj|bin|\.git' }).Count

    $rows += [pscustomobject]@{
        project = $p.Name
        zan = $files.Count
        art = $art
        bitmap = $bitmaps
        audio = $audio
        wired = $wired
        playtest = $playtest
        save = $save
        setting = $setting
        publish = $publish
        passthrough = $passthrough
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
    Write-Host ("projects={0}  build={1}  test={2}  save={3}  setting={4}  publish={5}  art>0={6}  wired={7}  playtest={8}" -f `
            $rows.Count,
            ($rows | Where-Object { $_.build }).Count,
            ($rows | Where-Object { $_.test }).Count,
            ($rows | Where-Object { $_.save }).Count,
            ($rows | Where-Object { $_.setting }).Count,
            ($rows | Where-Object { $_.publish }).Count,
            ($rows | Where-Object { $_.art -gt 0 }).Count,
            ($rows | Where-Object { $_.wired }).Count,
            ($rows | Where-Object { $_.playtest -gt 0 }).Count)
    $missing = $rows | Where-Object { -not $_.publish -or -not $_.save -or -not $_.setting }
    foreach ($m in $missing) {
        $gap = @()
        if (-not $m.save) { $gap += '存档' }
        if (-not $m.setting) { $gap += '设置' }
        if (-not $m.publish) { $gap += '单文件发布' }
        Write-Host ("GAP {0,-12} {1}" -f $m.project, ($gap -join '/'))
    }
    # 基础设施全绿但不是游戏的：零美术资产（可运行的原型），或者资产
    # 躺着没进渲染路径（摆设素材），或者没有玩法断言用例（玩法没被锁住）。
    $proto = $rows | Where-Object {
        $_.build -and $_.test -and $_.save -and $_.setting -and $_.publish -and
        ($_.art -eq 0 -or -not $_.wired -or $_.playtest -eq 0)
    }
    foreach ($m in $proto) {
        $why = @()
        if ($m.art -eq 0) { $why += '零美术资产' }
        if (-not $m.wired) { $why += '位图没进渲染路径' }
        if ($m.playtest -eq 0) { $why += '无玩法断言用例' }
        Write-Host ("PROTO {0,-12} 基础设施四口径全绿但 {1} —— 可运行的原型，不是游戏" -f $m.project, ($why -join '/'))
    }
}

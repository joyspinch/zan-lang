# 设计|代码 标签条的实机探针（IDE 级，不进 ctest）。
#
# 标签条是 IDE 外壳的一部分，只有真 IDE 跑起来才验得到：headless 用例
# 拼不出 dock / 设计器 / 文档标签那一整套。所以这个探针直接驱动
# scripts\build_ide.ps1 产出的 build\ZanIDE.exe：
#
#   1. 把夹具工程复制到临时目录（IDE 会往工程里写东西，别污染夹具）；
#   2. 用 ZAN_IDE_CONFIG_DIR 指向临时配置目录，写入 lastproject.txt，
#      让 IDE 启动即打开该工程（不碰用户自己的 IDE 配置）；
#   3. 用 ZAN_UI_SCRIPT / ZAN_UI_OUT 让 IDE 在 UiDriver 下自跑：先 dump
#      命中区与像素，再按语义标签点「代码」标签，再 dump 一次；
#   4. 断言两件事：标签条的两个标签都在（ide-design-tab / ide-code-tab），
#      点完「代码」后设计器画布的命中区消失（说明真的切到了代码视图）。
#
# 用法：
#   pwsh -File scripts/build_ide.ps1            # 先有 build\ZanIDE.exe
#   pwsh -File tests/ide/design_tabs_probe.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root 'build\ZanIDE.exe'
if (!(Test-Path -LiteralPath $exe)) {
    Write-Output "PROBE_FAIL: build\ZanIDE.exe 不存在，先跑 scripts\build_ide.ps1"
    exit 1
}

$work = Join-Path $root '_scratch\design_tabs_probe'
$cfg = Join-Path $work 'cfg'
$out = Join-Path $work 'out'
$proj = Join-Path $work 'probe'
foreach ($d in @($cfg, $out)) {
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
# 目标目录不要预先建：Copy-Item 撞上已存在的目录会把源**塞进去一层**，
# 工程就少了层目录，IDE 认不出 zan.proj（表现为启动页而不是工程）。
if (Test-Path -LiteralPath $proj) { Remove-Item -LiteralPath $proj -Recurse -Force }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'design_tabs_fixture') -Destination $proj -Recurse -Force
if (!(Test-Path -LiteralPath (Join-Path $proj 'zan.proj'))) {
    Write-Output "PROBE_FAIL: 夹具工程没复制到位（$proj\zan.proj 不存在）"
    exit 1
}

$projPath = ($proj -replace '\\', '/')
[IO.File]::WriteAllText((Join-Path $cfg 'lastproject.txt'), $projPath)

$script = Join-Path $work 'script.txt'
[IO.File]::WriteAllText($script, @"
wait 7000
dump hitregions hits_design.json
clickid @ide-code-tab
wait 2000
dump hitregions hits_code.json
quit
"@)

$env:ZAN_UI_SCRIPT = $script
$env:ZAN_UI_OUT = $out
$env:ZAN_IDE_CONFIG_DIR = $cfg
$p = Start-Process -FilePath $exe -PassThru -NoNewWindow
if (!$p.WaitForExit(90000)) {
    Write-Output "PROBE_FAIL: IDE 90 秒没退出"
    $p.Kill()
    exit 1
}

function Get-Regions($file) {
    $path = Join-Path $out $file
    if (!(Test-Path -LiteralPath $path)) { return $null }
    return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json).regions
}

$before = Get-Regions 'hits_design.json'
$after = Get-Regions 'hits_code.json'
if ($before -eq $null -or $after -eq $null) {
    Write-Output "PROBE_FAIL: 没拿到命中区 dump（out\$hits_*.json）"
    exit 1
}

$designTab = $before | Where-Object { $_.label -eq 'ide-design-tab' }
$codeTab = $before | Where-Object { $_.label -eq 'ide-code-tab' }
if ($designTab -eq $null -or $codeTab -eq $null) {
    Write-Output "PROBE_FAIL: 设计视图里没有 设计|代码 标签条"
    exit 1
}
Write-Output ("PROBE_OK 标签条: 设计@(" +
    $designTab.x + "," + $designTab.y + " " + $designTab.w + "x" + $designTab.h +
    ") 代码@(" + $codeTab.x + "," + $codeTab.y + " " + $codeTab.w + "x" + $codeTab.h + ")")

$codeTabAfter = $after | Where-Object { $_.label -eq 'ide-code-tab' }
if ($codeTabAfter -eq $null) {
    Write-Output "PROBE_FAIL: 切到代码视图后标签条不见了"
    exit 1
}

# 设计器画布在编辑区里；切到代码视图后画布让位给代码编辑器。
# 命中区的 id 每帧都在变（WidgetId 每帧分配），所以比对要看**几何**：
# 编辑区里至少有一块矩形消失、也至少有一块新矩形出现。
$editorLeft = $designTab.x
function Get-Rects($regions) {
    $set = @{}
    foreach ($r in $regions) {
        if ($r.x -lt $editorLeft) { continue }
        $set["$($r.x),$($r.y),$($r.w),$($r.h)"] = $true
    }
    return $set
}
$rectsBefore = Get-Rects $before
$rectsAfter = Get-Rects $after
$gone = @($rectsBefore.Keys | Where-Object { !$rectsAfter.ContainsKey($_) })
$fresh = @($rectsAfter.Keys | Where-Object { !$rectsBefore.ContainsKey($_) })
if ($gone.Count -eq 0 -or $fresh.Count -eq 0) {
    Write-Output "PROBE_FAIL: 点「代码」后编辑区画面没变（没切视图）"
    exit 1
}
Write-Output ("PROBE_OK 点「代码」后编辑区换画面：少了 " + $gone.Count +
    " 块、多出 " + $fresh.Count + " 块（设计器画布让位给代码编辑器）")
Write-Output "PROBE_PASS"
exit 0

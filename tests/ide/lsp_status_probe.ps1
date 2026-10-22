# 语言服务状态栏的实机探针（IDE 级，不进 ctest）。
#
# 断言三件事（全部读探针值，不靠截图反推）：
#   1. 工程打开后语言服务真的起来了（lsp.state == 2 = 已连接）——
#      此前 EnsureLsp 定义了却没有任何调用点，整套智能感知是死代码；
#   2. 状态栏那一项注册了带语义标签的命中区（ide-lsp-status），点它
#      能触发重连（lsp.restarts 递增，且重连后仍回到"已连接"）；
#   3. 服务起不来时状态变成"不可用"且带原因（ZAN_IDE_LSP_EXE 指向
#      不存在的路径 → lsp.state == 3、lsp.error 非空）。
#
# 用法：
#   pwsh -File scripts/build_ide.ps1            # 先有 build\ZanIDE.exe
#   pwsh -File tests/ide/lsp_status_probe.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root 'build\ZanIDE.exe'
if (!(Test-Path -LiteralPath $exe)) {
    Write-Output "PROBE_FAIL: build\ZanIDE.exe 不存在，先跑 scripts\build_ide.ps1"
    exit 1
}

$work = Join-Path $root '_scratch\lsp_probe'
$cfg = Join-Path $work 'cfg'
$out = Join-Path $work 'out'
$proj = Join-Path $work 'probe'
foreach ($d in @($cfg, $out)) {
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
# 目标目录不要预先建：Copy-Item 撞上已存在的目录会把源**塞进去一层**，
# 工程就少了层目录，IDE 认不出 zan.proj。
if (Test-Path -LiteralPath $proj) { Remove-Item -LiteralPath $proj -Recurse -Force }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'design_tabs_fixture') -Destination $proj -Recurse -Force
if (!(Test-Path -LiteralPath (Join-Path $proj 'zan.proj'))) {
    Write-Output "PROBE_FAIL: 夹具工程没复制到位（$proj\zan.proj 不存在）"
    exit 1
}

$projPath = ($proj -replace '\\', '/')
[IO.File]::WriteAllText((Join-Path $cfg 'lastproject.txt'), $projPath)

function Get-Probe($file) {
    $path = Join-Path $out $file
    if (!(Test-Path -LiteralPath $path)) { return $null }
    return (Get-Content -LiteralPath $path -Raw).Trim()
}

function Invoke-Session($name, $scriptText, $envLspExe) {
    $script = Join-Path $work ($name + '_script.txt')
    [IO.File]::WriteAllText($script, $scriptText)
    $env:ZAN_UI_SCRIPT = $script
    $env:ZAN_UI_OUT = $out
    $env:ZAN_IDE_CONFIG_DIR = $cfg
    if ($envLspExe -eq $null) {
        Remove-Item Env:\ZAN_IDE_LSP_EXE -ErrorAction SilentlyContinue
    } else {
        $env:ZAN_IDE_LSP_EXE = $envLspExe
    }
    $p = Start-Process -FilePath $exe -PassThru -NoNewWindow
    # 冷启动要 11~13 秒（zan-lsp 同步索引 stdlib + 全部可见包仓库），
    # 探针里等两次连接（首次 + 点状态项重连），给足 5 分钟。
    if (!$p.WaitForExit(300000)) {
        Write-Output "PROBE_FAIL: IDE 300 秒没退出（$name）"
        $p.Kill()
        exit 1
    }
}

# ---- 会话 A：正常路径 ----
# 等语言服务起来（冷启动 11~13 秒，机器忙时更久——按 45 秒留余量），
# 然后点状态栏那一项，验证"点了会重连"。
Invoke-Session 'ok' @'
wait 45000
dump probe lsp.state a_state1.txt
dump probe lsp.text a_text1.txt
dump probe lsp.error a_error1.txt
dump probe lsp.restarts a_restarts1.txt
dump probe lsp.connectMs a_connect1.txt
dump hitregions a_hits.json
clickid @ide-lsp-status
wait 45000
dump probe lsp.state a_state2.txt
dump probe lsp.restarts a_restarts2.txt
dump probe lsp.error a_error2.txt
dump probe lsp.connectMs a_connect2.txt
quit
'@ $null

$state1 = Get-Probe 'a_state1.txt'
$text1 = Get-Probe 'a_text1.txt'
$restarts1 = Get-Probe 'a_restarts1.txt'
$connect1 = Get-Probe 'a_connect1.txt'
$state2 = Get-Probe 'a_state2.txt'
$restarts2 = Get-Probe 'a_restarts2.txt'
$connect2 = Get-Probe 'a_connect2.txt'
if ($state1 -eq $null -or $state2 -eq $null) {
    Write-Output "PROBE_FAIL: 没拿到 lsp 探针（out\a_state*.txt）"
    exit 1
}
if ($state1 -ne '2') {
    Write-Output "PROBE_FAIL: 语言服务没连上（lsp.state=$state1，期望 2=已连接；text=$text1）"
    exit 1
}
Write-Output ("PROBE_OK 语言服务已连接，状态栏显示：" + $text1 + "（冷启动 " + $connect1 + " ms）")

$hitsPath = Join-Path $out 'a_hits.json'
if (!(Test-Path -LiteralPath $hitsPath)) {
    Write-Output "PROBE_FAIL: 没拿到命中区 dump（out\a_hits.json）"
    exit 1
}
$regions = (Get-Content -LiteralPath $hitsPath -Raw | ConvertFrom-Json).regions
$lspRegion = $regions | Where-Object { $_.label -eq 'ide-lsp-status' }
if ($lspRegion -eq $null) {
    Write-Output "PROBE_FAIL: 状态栏没有注册 ide-lsp-status 命中区"
    exit 1
}
Write-Output ("PROBE_OK 状态项命中区在位 @(" + $lspRegion.x + "," + $lspRegion.y +
    " " + $lspRegion.w + "x" + $lspRegion.h + ")")

if ($restarts1 -ne '0') {
    Write-Output "PROBE_FAIL: 还没点就重连过（lsp.restarts=$restarts1）"
    exit 1
}
if ($restarts2 -ne '1') {
    Write-Output "PROBE_FAIL: 点状态项没触发重连（lsp.restarts=$restarts2，期望 1）"
    exit 1
}
if ($state2 -ne '2') {
    Write-Output "PROBE_FAIL: 重连后没回到已连接（lsp.state=$state2）"
    exit 1
}
Write-Output ("PROBE_OK 点状态项 = 重连语言服务，重连后仍是已连接（重连冷启动 " + $connect2 + " ms）")

# ---- 会话 B：起不来的路径 ----
# ZAN_IDE_LSP_EXE 指向不存在的文件 → 严格覆盖生效 → 状态"不可用"
# 且带原因。
$bogus = Join-Path $work 'no_such_zan_lsp.exe'
Invoke-Session 'bad' @'
wait 8000
dump probe lsp.state b_state.txt
dump probe lsp.text b_text.txt
dump probe lsp.error b_error.txt
quit
'@ $bogus

$stateB = Get-Probe 'b_state.txt'
$textB = Get-Probe 'b_text.txt'
$errorB = Get-Probe 'b_error.txt'
if ($stateB -ne '3') {
    Write-Output "PROBE_FAIL: zan-lsp 找不到时状态不是不可用（lsp.state=$stateB，期望 3）"
    exit 1
}
if ([string]::IsNullOrWhiteSpace($errorB)) {
    Write-Output "PROBE_FAIL: 不可用但没给原因（lsp.error 为空）"
    exit 1
}
if ($errorB -notlike '*zan-lsp*') {
    Write-Output "PROBE_FAIL: 原因里没有 zan-lsp 字样（lsp.error=$errorB）"
    exit 1
}
Write-Output ("PROBE_OK 起不来时报" + $textB + "，原因：" + $errorB)

Write-Output "PROBE_PASS"
exit 0

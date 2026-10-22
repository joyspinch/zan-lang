# 调度器自测（scripts/sched.ps1）
#
# 验证的是"多任务并行不踩踏"的那几件事，逐条对着机制验：
#   1. 锁：拿到就占着，别人拿不到（不排队时明确拒绝）
#   2. 排队：-Wait 的人会等，等到了自己拿到
#   3. 崩溃自愈：持锁进程死了，锁自动失效（不等超时）
#   4. 准入：同题重复派发被拒（不白烧 token）
#   5. 准入：并发超上限被拒，带 -Wait 则排队
#   6. 收尾：finish 释放位子并清掉它登记的副本
#   7. 回收：过期租约被回收；没人引用的仓库外 worktree 副本被自动移除
#
# 用法：pwsh -File tests\sched_selftest.ps1
# 退出码 0 = 全过；非 0 = 有用例没过（逐条打印 PASS/FAIL）。
#
# 注意两个 PowerShell 细节（都踩过）：
#   - 命名参数只能用**哈希表** splat（@hashtable）；数组 splat（@array）是纯
#     位置的，传 '-Name' 会被当成位置参数报"找不到接受该实参的位置参数"。
#   - 后台辅助进程要把 stdout/stderr 重定向到文件，否则它会攥着管道不放，
#     调用方看着像"卡住了"。

param(
    [switch]$KeepArtifacts
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sched = Join-Path $root 'scripts\sched.ps1'
$scratch = Join-Path $root '_scratch\schedtest'
$failures = 0
$checks = 0

function Check([string]$what, [bool]$ok, [string]$detail = '') {
    $script:checks++
    if ($ok) {
        Write-Host ("  PASS  {0}" -f $what) -ForegroundColor Green
    } else {
        $script:failures++
        Write-Host ("  FAIL  {0}{1}" -f $what, $(if ($detail) { " —— $detail" } else { '' })) -ForegroundColor Red
    }
}

# 起一个常驻进程当持锁者（它的 PID 活着，租约才算占着）。
# 用 -EncodedCommand：Start-Process 的 -ArgumentList 是**按空格拼起来**的，
# "Start-Sleep -Seconds 120" 会被拆成 pwsh 自己的参数，进程秒退——锁于是
# 立刻被判成"持锁者已死"。Base64 没有空格，绕开这个问题。
function Start-Holder([int]$seconds, [string]$tag) {
    $outFile = Join-Path $scratch "$tag.out"
    $errFile = Join-Path $scratch "$tag.err"
    $enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes("Start-Sleep -Seconds $seconds"))
    return Start-Process pwsh -NoNewWindow -PassThru `
        -RedirectStandardOutput $outFile -RedirectStandardError $errFile `
        -ArgumentList @('-NoProfile', '-EncodedCommand', $enc)
}

function Invoke-Sched([hashtable]$schedArgs) {
    # *>&1：Write-Host 走的是**信息流**（3），只写 2>&1 抓不到它的输出。
    $out = & $sched @schedArgs *>&1
    return @{ code = $LASTEXITCODE; out = ($out | Out-String).Trim() }
}

if (Test-Path $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$lockName = 'selftest'

Write-Host '== 1. 锁：拿到就占着，别人拿不到 =='
$holder = Start-Holder 120 'holder1'
Start-Sleep -Milliseconds 400
$r = Invoke-Sched @{ Command = 'lock'; Name = $lockName; OwnerPid = "$($holder.Id)"; Note = 'selftest-holder' }
Check '持锁者能拿到锁' ($r.code -eq 0) $r.out

$r = Invoke-Sched @{ Command = 'lock-status'; Name = $lockName }
Check '状态显示被占用' ($r.out -match '被占用') $r.out

$r = Invoke-Sched @{ Command = 'lock'; Name = $lockName; OwnerPid = "$PID" }
Check '别人拿不到（非排队时拒绝，exit=3）' ($r.code -eq 3) "code=$($r.code) $($r.out)"

Write-Host '== 2. 排队：-Wait 的会等 =='
$waiterOwner = Start-Holder 120 'holder2'
$waiter = Start-Process pwsh -NoNewWindow -PassThru `
    -RedirectStandardOutput (Join-Path $scratch 'waiter.out') `
    -RedirectStandardError (Join-Path $scratch 'waiter.err') `
    -ArgumentList @('-NoProfile', '-File', $sched, 'lock', '-Name', $lockName,
                    '-OwnerPid', "$($waiterOwner.Id)", '-Wait', '-TimeoutSec', '60')
Start-Sleep -Seconds 3
Check '排队者在等（还没拿到）' (-not $waiter.HasExited) '等待者提前退出了'

$r = Invoke-Sched @{ Command = 'unlock'; Name = $lockName; OwnerPid = "$($holder.Id)" }
Check '原持锁者能释放' ($r.code -eq 0) $r.out
$waiter.WaitForExit(30000) | Out-Null
Check '排队者等到后拿到锁' ($waiter.ExitCode -eq 0) "exit=$($waiter.ExitCode)"
$r = Invoke-Sched @{ Command = 'unlock'; Name = $lockName; OwnerPid = "$($waiterOwner.Id)" }
Check '清理排队者的锁' ($r.code -eq 0) $r.out

Write-Host '== 3. 崩溃自愈：持锁进程死了，锁自动失效 =='
$holder2 = Start-Holder 120 'holder3'
Start-Sleep -Milliseconds 400
$r = Invoke-Sched @{ Command = 'lock'; Name = $lockName; OwnerPid = "$($holder2.Id)"; Note = 'will-die' }
Check '拿到锁' ($r.code -eq 0) $r.out
Stop-Process -Id $holder2.Id -Force
Start-Sleep -Milliseconds 1000
$r = Invoke-Sched @{ Command = 'lock'; Name = $lockName; OwnerPid = "$PID" }
Check '持锁者死后锁自动失效（不用等超时）' ($r.code -eq 0) $r.out
$r = Invoke-Sched @{ Command = 'unlock'; Name = $lockName; OwnerPid = "$PID" }
Check '可释放' ($r.code -eq 0) $r.out

Write-Host '== 4. 准入：同题重复派发被拒 =='
$title = "selftest-$([guid]::NewGuid().ToString('N').Substring(0,6))"
$r = Invoke-Sched @{ Command = 'register'; Title = $title; SessionId = 'sess-a' }
Check '第一次派发成功' ($r.code -eq 0) $r.out
$leaseId = ''
if ($r.out -match 'lease=([0-9a-f]+)') { $leaseId = $Matches[1] }
Check '拿到租约 id' ($leaseId -ne '') $r.out

$r = Invoke-Sched @{ Command = 'register'; Title = $title; SessionId = 'sess-b' }
Check '同题第二次派发被拒（exit=2）' ($r.code -eq 2) "code=$($r.code) $($r.out)"
Check '拒绝信息点明同题已在跑' ($r.out -match '同题已在跑') $r.out

Write-Host '== 5. 准入：并发上限 =='
$r = Invoke-Sched @{ Command = 'register'; Title = "$title-cap"; SessionId = 'sess-c'; MaxConcurrent = 1 }
Check '上限 1 时第二个任务被拒（exit=5）' ($r.code -eq 5) "code=$($r.code) $($r.out)"

Write-Host '== 6. 收尾：finish 清掉登记的副本 =='
$copyDir = Join-Path $scratch 'copy-under-test'
New-Item -ItemType Directory -Force -Path $copyDir | Out-Null
Set-Content -LiteralPath (Join-Path $copyDir 'junk.bin') -Value 'x' -Encoding ASCII
$r = Invoke-Sched @{ Command = 'finish'; Id = $leaseId; Scratch = $copyDir }
Check 'finish 成功' ($r.code -eq 0) $r.out
Check '登记的副本被删掉' (-not (Test-Path $copyDir)) $copyDir

$r = Invoke-Sched @{ Command = 'register'; Title = $title; SessionId = 'sess-d' }
Check 'finish 后同题可以再派' ($r.code -eq 0) "code=$($r.code) $($r.out)"
$leaseId2 = ''
if ($r.out -match 'lease=([0-9a-f]+)') { $leaseId2 = $Matches[1] }
Invoke-Sched @{ Command = 'finish'; Id = $leaseId2 } | Out-Null

Write-Host '== 7. 回收：过期租约 + 没人引用的仓库外 worktree 副本 =='
$stale = Join-Path $scratch 'stale-copy'
New-Item -ItemType Directory -Force -Path $stale | Out-Null
$r = Invoke-Sched @{ Command = 'register'; Title = 'selftest-stale'; SessionId = 'sess-e'; Scratch = $stale }
$staleLease = ''
if ($r.out -match 'lease=([0-9a-f]+)') { $staleLease = $Matches[1] }
$stalePath = Join-Path $env:LOCALAPPDATA "Zan\sched\tasks\$staleLease.json"
if ($staleLease -and (Test-Path $stalePath)) {
    # 把心跳改成很久以前，模拟"会话崩了没 finish"。
    # 用 epoch 秒，不用 ISO 字符串：字符串往返会被时区偏移（东八区差 8 小时，
    # 看着刚写的租约瞬间"过期"）。
    $j = Get-Content $stalePath -Raw | ConvertFrom-Json
    $j.heartbeatUnix = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - (3 * 3600)
    Set-Content -LiteralPath $stalePath -Value ($j | ConvertTo-Json -Depth 6) -Encoding UTF8
    $r = Invoke-Sched @{ Command = 'reap'; Apply = $true }
    Check '过期租约被回收' (-not (Test-Path $stalePath)) $r.out
    Check '过期租约登记的副本被删掉' (-not (Test-Path $stale)) $stale
} else {
    Check '过期租约被回收' $false "找不到租约文件 $stalePath"
}

# 仓库外 worktree 副本（zan-head 那类）：干净 + HEAD 已在 main 上 → 自动移除
$wtPath = Join-Path $scratch 'wt-copy'
$mk = & git -C $root worktree add --detach $wtPath HEAD 2>&1
if ($LASTEXITCODE -eq 0) {
    $r = Invoke-Sched @{ Command = 'reap'; Apply = $true }
    Check '仓库外 worktree 副本被自动回收' (-not (Test-Path $wtPath)) "reap 输出：$($r.out)"
    & git -C $root worktree prune 2>&1 | Out-Null
} else {
    Check '仓库外 worktree 副本被自动回收' $false "worktree add 失败：$mk"
}

# 收尾：把还活着的辅助进程收掉，别给测试环境留垃圾
foreach ($p in @($holder, $waiterOwner)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host ''
if ($failures -eq 0) {
    Write-Host ("SCHED_OK checks={0}" -f $checks) -ForegroundColor Green
    if (-not $KeepArtifacts) { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue }
    exit 0
}
Write-Host ("SCHED_FAIL checks={0} failures={1}" -f $checks, $failures) -ForegroundColor Red
exit 1

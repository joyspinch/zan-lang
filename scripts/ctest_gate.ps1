# ctest 的自动排队钩子（CTEST_CUSTOM_PRE_TEST / POST_TEST）
#
# 为什么需要它：
#   共享构建资源的闸只在 scripts\build.ps1 / scripts\test.ps1 上。裸跑
#   `ctest -R foo`（很多会话都这么干）绕过了闸——一个会话在跑测试，另一个
#   会话重编编译器，测试就成片假红，而两边都以为自己没错。
#
#   这个钩子由 CMake 的 CTestCustom.cmake 自动挂上：**任何** ctest 调用
#   （不管从哪来）都要先拿到同一把共享锁，拿不到就在原地排队，而不是
#   另开一棵构建树去躲。CTEST_CUSTOM_PRE_TEST 拿不到锁就以非零退出，
#   ctest 整轮不跑——"排队"是默认动作，绕不过去。
#
# 嵌套情况：scripts\test.ps1 自己持着锁再拉起 ctest。这时 ctest 的父进程
# 就是持锁者，钩子认出"这是同一次操作的嵌套"，不重复加锁、也不在收尾时
# 把别人的锁解掉（否则父进程还在跑，锁却被提前释放了）。
#
# 用法（一般不由人调用，CMake 自动挂）：
#   pwsh -File scripts\ctest_gate.ps1 -Action enter
#   pwsh -File scripts\ctest_gate.ps1 -Action leave

param(
    [ValidateSet('enter', 'leave')]
    [string]$Action = 'enter',
    [int]$TimeoutSec = 1800
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sched = Join-Path $PSScriptRoot 'sched.ps1'

function Get-ParentPid([int]$procId) {
    try {
        $p = Get-CimInstance Win32_Process -Filter "ProcessId = $procId" -ErrorAction Stop
        return [int]$p.ParentProcessId
    } catch { return 0 }
}

# ownerPid 是不是 descendantPid 的祖先？
function Test-IsAncestor([int]$ownerPid, [int]$descendantPid) {
    $cur = $descendantPid
    for ($i = 0; $i -lt 8 -and $cur -gt 0; $i++) {
        if ($cur -eq $ownerPid) { return $true }
        $cur = Get-ParentPid $cur
    }
    return $false
}

# 钩子进程是 ctest 拉起来的，所以它的父进程就是 ctest。
$ctestPid = Get-ParentPid $PID
if ($ctestPid -le 0) {
    Write-Host '[gate] 认不出 ctest 进程（拿不到父进程），本次不参与排队'
    exit 0
}

$lockName = 'build'
$lease = & $sched lock-status -Name $lockName -Json | ConvertFrom-Json

if ($Action -eq 'enter') {
    if ($lease -and [int]$lease.ownerPid -gt 0) {
        if (Test-IsAncestor ([int]$lease.ownerPid) $ctestPid) {
            # 嵌套：持锁者是我的祖先（scripts\test.ps1 拉起的 ctest）
            Write-Host "[gate] 检测到嵌套调用（持锁者 pid=$($lease.ownerPid) 是 ctest 的祖先），不重复加锁"
            exit 0
        }
    }
    # 拿不到就排队等（-Wait + 超时），绝不静默绕过
    & $sched lock -Name $lockName -OwnerPid $ctestPid -Wait -TimeoutSec $TimeoutSec -Note "ctest pid=$ctestPid"
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Host '[gate] ctest 未取得共享锁，整轮不跑（这是刻意的：别在别人重编的时候测）' -ForegroundColor Red
        exit $code
    }
    exit 0
}

# leave：只有当锁确实是"我这次 ctest"持有时才解；祖先持有的不动。
if ($lease -and [int]$lease.ownerPid -eq $ctestPid) {
    & $sched unlock -Name $lockName -OwnerPid $ctestPid | Out-Null
    exit 0
}
exit 0

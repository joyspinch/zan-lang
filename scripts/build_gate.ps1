# 共享构建资源的串行闸（跨进程互斥）
#
# 为什么需要它：
#   多个会话共享同一个 build/ 时，最容易发生的不是"代码写错"，而是**互相踩
#   坏构建产物**——一个会话重编 zanc.exe，另一个会话的 ctest 正用着它，测试
#   便成片失败（假红）。AGENTS.md 规则 8 写了"测试期间不得有其他构建"，但那
#   是靠人自觉的文字约束，没有机制，执行时必然漏。
#
#   于是出现了另一种应对：每个会话自己建一棵构建树（build_mine、
#   build-zancprobe、zan-pkgs…）。那是**用磁盘换隔离**，代价是几百 MB 到
#   2 GB 的重复构建，而且公共包一改副本就漂移——副本并没有让共享状态消失，
#   只是把它藏起来了，漂移照旧。
#
#   正确的做法是**串行等待**：拿不到共享资源就排队等，而不是另开一份。
#   这把闸就是那个等待点。
#
# 机制（现在由 scripts\sched.ps1 统一实现，闸只是它的命令行外壳）：
#   锁 = 一份**租约**（%LOCALAPPDATA%\Zan\sched\locks\），记着持锁者的
#   PID 与进程启动时刻。判定"锁着吗"看**持锁进程是否还活着**：
#     - 持锁进程崩了/被杀了 → 租约自动失效，下一个来的人直接接手；
#     - PID 被复用 → 启动时刻对不上，判定为已死；
#     - 别的进程也能替它解锁（这正是 ctest 钩子需要的：钩子进程自己会
#       退出，锁却要活到整轮测试结束）。
#   旧的实现是 Windows 命名 Mutex（内核对象，进程死了内核自动释放）。
#   换成租约不是为了放弃内核的自动释放，而是为了**支持"由别的进程释放"**
#   ——否则 ctest 的 PRE/POST 钩子没法把锁活到整轮测试结束。
#   租约的"检查+写入"仍在命名 Mutex 里做，保证两个进程不会同时抢到。
#
#   锁名带仓库路径的哈希，因此不同仓库互不干扰，同一仓库的所有会话共享一把。
#
# 超时：
#   默认 30 分钟。超时后**报错退出**，不静默绕过——静默绕过等于没有闸。
#   排队时每 5 秒打印一次持锁者，让等待者知道是在等谁。
#
# 用法（PowerShell）：
#   # 包一段需要独占共享构建资源的操作
#   .\scripts\build_gate.ps1 -Name "my-build" -Script { cmake --build build }
#
#   # 只看当前谁占着闸
#   .\scripts\build_gate.ps1 -Status

param(
    [string]$Name = 'build',
    [scriptblock]$Script,
    [string]$Command,
    [int]$TimeoutSec = 1800,
    [switch]$Status
)

$ErrorActionPreference = 'Stop'
$sched = Join-Path $PSScriptRoot 'sched.ps1'

if ($Status) {
    & $sched lock-status -Name $Name
    exit $LASTEXITCODE
}

if (-not $Script -and -not $Command) {
    Write-Error "[gate] 需要 -Script { ... } 或 -Command '<命令行>' 或 -Status"
    exit 2
}

# -Command 走 Invoke-Expression：给非交互调用（pwsh -File scripts/build_gate.ps1
# -Command "..."）用。参数传递受 PowerShell 引号规则限制，复杂逻辑请用 -Script。
if (-not $Script -and $Command) {
    $Script = [scriptblock]::Create($Command)
}

# 租约的持锁者是**本进程**（$PID）：闸活着锁就活着，闸死了锁自动失效。
& $sched lock -Name $Name -OwnerPid $PID -Wait -TimeoutSec $TimeoutSec -Note "build_gate pid=$PID"
$gateCode = $LASTEXITCODE
if ($gateCode -ne 0) {
    if ($gateCode -eq 1) {
        Write-Host "[gate] 等待 '$Name' 超时（${TimeoutSec}s）。" -ForegroundColor Red
        Write-Host "[gate] 处理：确认那个会话是否还活着；活的就等它做完，别再另开一棵构建树。" -ForegroundColor Yellow
    }
    exit $gateCode
}

$code = 0
try {
    & $Script
    $code = if ($LASTEXITCODE -ne $null) { $LASTEXITCODE } else { 0 }
} catch {
    Write-Host "[gate] '$Name' 执行出错：$_" -ForegroundColor Red
    $code = 1
} finally {
    & $sched unlock -Name $Name -OwnerPid $PID | Out-Null
}
exit $code

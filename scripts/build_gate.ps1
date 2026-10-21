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
# 用法（PowerShell）：
#   # 包一段需要独占共享构建资源的操作
#   .\scripts\build_gate.ps1 -Name "my-build" -Script { cmake --build build }
#
#   # 只看当前谁占着闸
#   .\scripts\build_gate.ps1 -Status
#
# 跨进程语义：
#   用 Windows 命名 Mutex（内核对象）。持锁进程崩溃/被杀时内核自动释放，
#   不会有"死进程留下永久锁"的坑——这是选 Mutex 而不是锁文件的关键理由。
#   锁名带仓库路径的哈希，因此不同仓库互不干扰，同一仓库的所有会话共享一把。
#
# 超时：
#   默认 30 分钟。超时后**报错退出**，不静默绕过——静默绕过等于没有闸。
#   排队时每 5 秒打印一次持锁者，让等待者知道是在等谁。

param(
    [string]$Name = 'build',
    [scriptblock]$Script,
    [string]$Command,
    [int]$TimeoutSec = 1800,
    [switch]$Status
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# 锁名：仓库绝对路径的 SHA256 前 16 位 + 用途名。
# 同一仓库 = 同一把锁（不同会话共享）；不同仓库 = 不同锁（互不阻塞）。
$repoKey = [System.IO.Path]::GetFullPath($root)
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $hashBytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($repoKey))
    $repoHash = ([System.BitConverter]::ToString($hashBytes) -replace '-', '').Substring(0, 16)
} finally {
    $sha.Dispose()
}
$mutexName = "Local\ZanBuildGate_${repoHash}_$Name"

# 持锁者信息写在这里，供等待者显示（Mutex 本身查不出持有者）。
$holderFile = Join-Path $env:TEMP "zan_build_gate_${repoHash}_${Name}.holder"

function Show-Holder {
    if (Test-Path $holderFile) {
        try {
            $h = Get-Content $holderFile -Raw -ErrorAction Stop
            if ($h.Trim()) { return $h.Trim() }
        } catch { }
    }
    return '<未知>'
}

if ($Status) {
    $created = $false
    $m = New-Object System.Threading.Mutex($false, $mutexName, [ref]$created)
    try {
        if ($m.WaitOne(0)) {
            Write-Host "[gate] '$Name' 当前空闲（$mutexName）"
            $m.ReleaseMutex()
        } else {
            Write-Host "[gate] '$Name' 正被占用：$(Show-Holder)"
        }
    } finally {
        $m.Dispose()
    }
    exit 0
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

$mutex = New-Object System.Threading.Mutex($false, $mutexName)
$acquired = $false
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastReport = [DateTime]::MinValue

    while (-not $acquired) {
        # WaitOne(0) 试探；拿不到就退避重试，不忙等。
        $acquired = $mutex.WaitOne(0)
        if ($acquired) { break }

        if ((Get-Date) -gt $deadline) {
            Write-Host "[gate] 等待 '$Name' 超时（${TimeoutSec}s）。当前持锁者：$(Show-Holder)" -ForegroundColor Red
            Write-Host "[gate] 这说明另一个会话长时间占着共享构建资源。" -ForegroundColor Yellow
            Write-Host "[gate] 处理：确认那个会话是否还活着；活的就等它做完，别再另开一棵构建树。" -ForegroundColor Yellow
            exit 1
        }

        if (((Get-Date) - $lastReport).TotalSeconds -ge 5) {
            $lastReport = Get-Date
            Write-Host "[gate] 排队等待 '$Name'（持锁者：$(Show-Holder)）..." -ForegroundColor DarkGray
        }
        Start-Sleep -Milliseconds 500
    }

    # 记录持锁者，供后来的等待者显示。
    $who = "pid=$PID host=$env:COMPUTERNAME at=$((Get-Date).ToString('HH:mm:ss'))"
    try {
        Set-Content -Path $holderFile -Value $who -Encoding UTF8
    } catch { }

    Write-Host "[gate] 已获得 '$Name' 锁" -ForegroundColor Green
    try {
        & $Script
        $code = if ($LASTEXITCODE -ne $null) { $LASTEXITCODE } else { 0 }
    } catch {
        Write-Host "[gate] '$Name' 执行出错：$_" -ForegroundColor Red
        $code = 1
    }
    exit $code
} finally {
    if ($acquired) {
        try { $mutex.ReleaseMutex() } catch { }
        try { Remove-Item $holderFile -ErrorAction SilentlyContinue } catch { }
    }
    $mutex.Dispose()
}

# 并行协调调度器（任务准入 + 共享资源锁 + 副本自动回收）
#
# 为什么需要它：
#   多个会话共享同一个工作树时，真正的麻烦不是"代码写错"，而是**互相踩**：
#     1) 两个会话重编同一个编译器，测试成片假红；
#     2) 同一个任务被派发两遍，多出来的那个是零消息空壳，纯白烧 token；
#     3) 会话为了躲冲突自己复制一份源码/构建树，复制出来没人清，磁盘和
#        公共包一起漂移。
#   AGENTS.md 的规则 8/8a 写了"要等、不要另开一份"，但那是文字约束——靠人
#   自觉必然漏。**规则要有机制兜底**，否则每轮并发都要重新踩一遍。
#
#   所以这把调度器管三件事，全部自动：
#     准入   register：同题重复派发直接拒；并发超上限就排队等（不复制）。
#     锁     lock/unlock：共享构建资源一把锁，租约 + 进程存活判定——
#             持锁进程崩了锁自动失效，别的进程也能替它解锁。
#     回收   reap：过期任务自动释放并清掉它登记的副本；仓库外没人引用的
#             worktree 副本自动回收（删前先证明"内容已在主仓"）。
#
# 用法：
#   # 申请一个任务位（同题在跑就拒，超上限就排队）
#   pwsh -File scripts\sched.ps1 register -Title "rail UI 改造" -SessionId <sid>
#
#   # 长任务续租
#   pwsh -File scripts\sched.ps1 heartbeat -Id <leaseId>
#
#   # 结束任务：释放位子 + 清掉它登记的副本
#   pwsh -File scripts\sched.ps1 finish -Id <leaseId>
#
#   # 看现在谁在跑、谁占着锁、有哪些副本待回收
#   pwsh -File scripts\sched.ps1 status
#
#   # 回收（默认 dry-run；-Apply 才真删）
#   pwsh -File scripts\sched.ps1 reap
#   pwsh -File scripts\sched.ps1 reap -Apply
#
# 状态目录：%LOCALAPPDATA%\Zan\sched\（放仓库外：仓库会被 clean，状态不该
# 跟着没）。锁名带仓库路径哈希，不同仓库互不干扰。

param(
    [Parameter(Position = 0)][string]$Command = 'status',
    [string]$Name = 'build',
    [string]$Title = '',
    [string]$SessionId = '',
    [string]$Id = '',
    [int]$OwnerPid = 0,
    [int]$TimeoutSec = 1800,
    [int]$MaxConcurrent = 0,
    [int]$MaxAgeHours = 24,
    [int]$LeaseTtlMin = 30,
    [string]$Scratch = '',
    [string]$Worktree = '',
    [string]$Note = '',
    [switch]$Wait,
    [switch]$Apply,
    [switch]$Force,
    [switch]$KeepScratch,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# ---- 状态目录与仓库标识 ---------------------------------------------------
$stateRoot = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'Zan\sched' }
             else { Join-Path $env:TEMP 'zan-sched' }
$lockDir = Join-Path $stateRoot 'locks'
$taskDir = Join-Path $stateRoot 'tasks'
foreach ($d in @($stateRoot, $lockDir, $taskDir)) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
}

$repoKey = [System.IO.Path]::GetFullPath($root)
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $hashBytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($repoKey))
    $repoHash = ([System.BitConverter]::ToString($hashBytes) -replace '-', '').Substring(0, 16)
} finally { $sha.Dispose() }

# 准入互斥：只保护"检查 + 写租约"这一小段临界区（毫秒级），
# 不像旧闸那样把整段构建都攥在 Mutex 里——那样别的进程连"看一眼谁占着"
# 都做不到。真正的互斥靠租约（持锁者的进程活着 = 锁着）。
$admitMutex = New-Object System.Threading.Mutex($false, "Local\ZanSched_$repoHash")

function Write-Json([string]$path, $obj) {
    $tmp = "$path.tmp$PID"
    Set-Content -LiteralPath $tmp -Value ($obj | ConvertTo-Json -Depth 6) -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $path -Force
}

function Read-Json([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    try { return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json) } catch { return $null }
}

# 进程还活着吗？连"启动时刻"一起比——否则 PID 被复用会把死锁看成长锁。
#
# 启动时刻存 **epoch 毫秒（数字）**，不存 ISO 字符串：ConvertFrom-Json 会把
# ISO 字符串转成 DateTime，再转回字符串就不是原来的字面量了（时区还会被
# 折一次），比对必然失配——表现为"持锁者明明活着，锁却被判成已失效"。
# 数字过 JSON 不变形。
function Test-ProcessAlive([int]$procId, [int64]$startUnix) {
    if ($procId -le 0) { return $false }
    $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if (-not $p) { return $false }
    if ($startUnix -gt 0) {
        $actual = Get-ProcStartUnix $procId
        if ($actual -le 0) { return $false }
        if ([Math]::Abs($actual - $startUnix) -gt 1000) { return $false }   # PID 被复用了
    }
    return $true
}

function Get-ProcStartUnix([int]$procId) {
    try {
        $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
        if ($p) {
            return [int64]([DateTimeOffset]$p.StartTime).ToUnixTimeMilliseconds()
        }
    } catch { }
    return 0
}

function Get-LockPath([string]$n) { return (Join-Path $lockDir "$repoHash.$n.json") }
function Get-TaskPath([string]$i) { return (Join-Path $taskDir "$i.json") }

function Get-LockLease([string]$n) {
    $lease = Read-Json (Get-LockPath $n)
    if (-not $lease) { return $null }
    if (Test-ProcessAlive ([int]$lease.ownerPid) ([int64]$lease.ownerStartUnix)) { return $lease }
    return $null   # 持锁进程没了 → 租约自动失效
}

# 拿不到准入互斥就短暂退避重试（临界区极短，正常不会发生）。
function Invoke-WithAdmission([scriptblock]$block) {
    for ($i = 0; $i -lt 100; $i++) {
        if ($admitMutex.WaitOne(200)) {
            try { return & $block } finally { try { $admitMutex.ReleaseMutex() } catch { } }
        }
    }
    throw '[sched] 拿不到准入互斥（另一个调度器正在写状态）'
}

function Get-LiveTasks {
    $cut = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - ($LeaseTtlMin * 60)
    $out = @()
    foreach ($f in Get-ChildItem $taskDir -Filter '*.json' -ErrorAction SilentlyContinue) {
        $t = Read-Json $f.FullName
        if (-not $t) { continue }
        if ([int64]$t.heartbeatUnix -lt $cut) { continue }
        $out += $t
    }
    return $out
}

function Get-AllTasks {
    $out = @()
    foreach ($f in Get-ChildItem $taskDir -Filter '*.json' -ErrorAction SilentlyContinue) {
        $t = Read-Json $f.FullName
        if ($t) { $out += $t }
    }
    return $out
}

function Remove-TaskLease([string]$i) {
    $p = Get-TaskPath $i
    if (Test-Path $p) { Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue }
}

# 副本回收：只删"登记在任务租约里"的路径。
# 两种路径分开处理：
#   - **git worktree 副本**：走 `git worktree remove --force`（git 知道怎么清
#     元数据，也保证不会删到别人的东西）；
#   - **普通目录**：必须落在允许的根下（_scratch / 临时目录 / 调度器状态目录），
#     否则只报告不动手——注册路径是别人写进来的，不能拿着它当删除授权。
function Remove-RegisteredPath([string]$path, [switch]$Dry) {
    $full = [System.IO.Path]::GetFullPath($path)
    if (-not (Test-Path -LiteralPath $full)) { return $null }

    $wt = & git -C $root worktree list --porcelain 2>$null
    $isWorktree = $false
    foreach ($line in $wt) {
        if ($line -like 'worktree *') {
            if ([System.IO.Path]::GetFullPath($line.Substring(9)).Equals($full, [StringComparison]::OrdinalIgnoreCase)) {
                $isWorktree = $true; break
            }
        }
    }
    if ($isWorktree) {
        if ($Dry) { return "将删除 worktree 副本：$full" }
        & git -C $root worktree remove --force $full 2>&1 | Out-Null
        if (Test-Path $full) { Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue }
        return "已删除 worktree 副本：$full"
    }

    $allowed = @(
        [System.IO.Path]::GetFullPath((Join-Path $root '_scratch')),
        [System.IO.Path]::GetFullPath($env:TEMP),
        [System.IO.Path]::GetFullPath($stateRoot)
    )
    $ok = $false
    foreach ($a in $allowed) {
        if ($full.StartsWith($a, [StringComparison]::OrdinalIgnoreCase)) { $ok = $true; break }
    }
    if (-not $ok) { return "跳过（不在允许的根下，要人工确认）：$full" }
    if ($Dry) { return "将删除：$full" }
    try {
        Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction Stop
        return "已删除：$full"
    } catch {
        return "删除失败（可能被占用）：$full —— $_"
    }
}

# ---------------------------------------------------------------- 资源锁 ----
function Invoke-Lock {
    $owner = if ($OwnerPid -gt 0) { $OwnerPid } else { $PID }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastReport = [DateTime]::MinValue
    while ($true) {
        $result = Invoke-WithAdmission {
            $lease = Get-LockLease $Name
            if ($lease -and [int]$lease.ownerPid -ne $owner) {
                return @{ ok = $false; holder = $lease }
            }
            $obj = [ordered]@{
                name          = $Name
                repoHash      = $repoHash
                ownerPid      = $owner
                ownerStartUnix = (Get-ProcStartUnix $owner)
                ownerName     = (Get-Process -Id $owner -ErrorAction SilentlyContinue).ProcessName
                note          = $Note
                acquiredUtc   = (Get-Date).ToUniversalTime().ToString('o')
                acquiredUnix  = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
            }
            Write-Json (Get-LockPath $Name) $obj
            return @{ ok = $true; holder = $obj }
        }
        if ($result.ok) {
            Write-Host "[sched] 已获得 '$Name' 锁（owner pid=$owner）" -ForegroundColor Green
            exit 0
        }
        if (-not $Wait) {
            Write-Host "[sched] '$Name' 正被占用：pid=$($result.holder.ownerPid) $($result.holder.note)"
            exit 3
        }
        if ((Get-Date) -gt $deadline) {
            Write-Host "[sched] 等待 '$Name' 超时（${TimeoutSec}s）。持锁者：pid=$($result.holder.ownerPid) $($result.holder.note)" -ForegroundColor Red
            Write-Host "[sched] 处理：确认那个会话是否还活着；活的就等它做完，别另开一棵构建树。" -ForegroundColor Yellow
            exit 1
        }
        if (((Get-Date) - $lastReport).TotalSeconds -ge 5) {
            $lastReport = Get-Date
            Write-Host "[sched] 排队等待 '$Name'（持锁者：pid=$($result.holder.ownerPid)）..." -ForegroundColor DarkGray
        }
        Start-Sleep -Milliseconds 400
    }
}

function Invoke-Unlock {
    $res = Invoke-WithAdmission {
        $lease = Read-Json (Get-LockPath $Name)
        if (-not $lease) { return '当前没有锁' }
        if (-not $Force -and $OwnerPid -gt 0 -and [int]$lease.ownerPid -ne $OwnerPid) {
            return "锁属于 pid=$($lease.ownerPid)，不是 pid=$OwnerPid（要强解加 -Force）"
        }
        Remove-Item -LiteralPath (Get-LockPath $Name) -Force -ErrorAction SilentlyContinue
        return '已释放'
    }
    Write-Host "[sched] $Name：$res"
    if ($res -like '已释放*' -or $res -eq '当前没有锁') { exit 0 }
    exit 4
}

function Invoke-LockStatus {
    $lease = Get-LockLease $Name
    if ($Json) {
        if ($lease) { $lease | ConvertTo-Json -Depth 4 } else { 'null' }
        exit 0
    }
    if ($lease) {
        Write-Host "[sched] '$Name' 被占用：pid=$($lease.ownerPid) 进程=$($lease.ownerName) 说明=$($lease.note) 自=$($lease.acquiredUtc)"
    } else {
        $raw = Read-Json (Get-LockPath $Name)
        if ($raw) { Write-Host "[sched] '$Name' 空闲（上一任 pid=$($raw.ownerPid) 已不在，租约自动失效）" }
        else { Write-Host "[sched] '$Name' 空闲" }
    }
    exit 0
}

# ------------------------------------------------------------ 任务准入 ----
function Invoke-Register {
    if (-not $Title) { Write-Error '[sched] register 需要 -Title'; exit 2 }
    $key = ($Title -replace '\s+', ' ').Trim().ToLowerInvariant()
    $max = if ($MaxConcurrent -gt 0) { $MaxConcurrent } else { 4 }
    $scratchPaths = @()
    if ($Scratch) { $scratchPaths += ($Scratch -split ',' | Where-Object { $_ }) }
    $wtPaths = @()
    if ($Worktree) { $wtPaths += ($Worktree -split ',' | Where-Object { $_ }) }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastReport = [DateTime]::MinValue
    while ($true) {
        $live = Get-LiveTasks
        $dup = $live | Where-Object { $_.titleKey -eq $key } | Select-Object -First 1
        if ($dup) {
            Write-Host "[sched] 拒绝派发：同题已在跑 —— '$($dup.title)'（lease=$($dup.id) session=$($dup.sessionId) 起于 $($dup.startedUtc)）" -ForegroundColor Red
            Write-Host "[sched] 先把那条做完，或确认它确实是空壳（零消息）后再派。派发前先跑：sched.ps1 status" -ForegroundColor Yellow
            exit 2
        }
        if ($live.Count -ge $max) {
            if (-not $Wait) {
                Write-Host "[sched] 拒绝派发：并发已达上限 $max（活跃 $($live.Count)）。排队请加 -Wait。" -ForegroundColor Red
                exit 5
            }
            if ((Get-Date) -gt $deadline) {
                Write-Host "[sched] 等待任务位超时（${TimeoutSec}s）。活跃 $($live.Count) 个：$((($live | ForEach-Object { $_.title }) -join '、'))" -ForegroundColor Red
                exit 1
            }
            if (((Get-Date) - $lastReport).TotalSeconds -ge 5) {
                $lastReport = Get-Date
                Write-Host "[sched] 排队等任务位（上限 $max，活跃 $($live.Count)：$((($live | ForEach-Object { $_.title }) -join '、'))）..." -ForegroundColor DarkGray
            }
            Start-Sleep -Milliseconds 500
            continue
        }
        break
    }

    $leaseId = [guid]::NewGuid().ToString('N').Substring(0, 12)
    $obj = [ordered]@{
        id           = $leaseId
        title        = $Title
        titleKey     = $key
        sessionId    = $SessionId
        ownerPid     = $PID
        startedUtc   = (Get-Date).ToUniversalTime().ToString('o')
        startedUnix  = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        heartbeatUtc = (Get-Date).ToUniversalTime().ToString('o')
        heartbeatUnix = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        scratch      = $scratchPaths
        worktrees    = $wtPaths
    }
    Write-Json (Get-TaskPath $leaseId) $obj
    Write-Host "[sched] 已登记任务 '$Title'（lease=$leaseId）" -ForegroundColor Green
    Write-Host "[sched] 结束时请跑：pwsh -File scripts\sched.ps1 finish -Id $leaseId"
    exit 0
}

function Invoke-Heartbeat {
    $t = Read-Json (Get-TaskPath $Id)
    if (-not $t) { Write-Error "[sched] 没有这个任务租约：$Id"; exit 2 }
    $t.heartbeatUtc = (Get-Date).ToUniversalTime().ToString('o')
    $t.heartbeatUnix = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    Write-Json (Get-TaskPath $Id) $t
    Write-Host "[sched] '$($t.title)' 已续租"
    exit 0
}

function Invoke-Finish {
    $t = Read-Json (Get-TaskPath $Id)
    if (-not $t) { Write-Host "[sched] 没有这个任务租约：$Id（可能已过期回收）"; exit 0 }
    $extra = @()
    if ($Scratch) { $extra += ($Scratch -split ',' | Where-Object { $_ }) }
    if ($Worktree) { $extra += ($Worktree -split ',' | Where-Object { $_ }) }
    if (-not $KeepScratch) {
        foreach ($p in @($t.scratch) + @($t.worktrees) + $extra) {
            if (-not $p) { continue }
            $msg = Remove-RegisteredPath $p
            if ($msg) { Write-Host "[sched] $msg" }
        }
    }
    Remove-TaskLease $Id
    Write-Host "[sched] 已结束任务 '$($t.title)'（lease=$Id）" -ForegroundColor Green
    exit 0
}

function Invoke-Status {
    $live = Get-LiveTasks
    $leases = Get-AllTasks | Where-Object { $live.id -notcontains $_.id }
    $lock = Get-LockLease $Name
    if ($Json) {
        @{ live = $live; expired = $leases; lock = $lock } | ConvertTo-Json -Depth 5
        exit 0
    }
    Write-Host "== 活跃任务（上限默认 4）=="
    if (-not $live) { Write-Host '  （无）' }
    foreach ($t in $live) {
        Write-Host ("  {0,-12} {1,-28} session={2} 起于={3}" -f $t.id, $t.title, $t.sessionId, $t.startedUtc)
    }
    Write-Host "== 共享构建锁 '$Name' =="
    if ($lock) { Write-Host ("  被占用 pid={0} 说明={1}" -f $lock.ownerPid, $lock.note) }
    else { Write-Host '  空闲' }
    Write-Host "== 过期未回收的任务租约 =="
    if (-not $leases) { Write-Host '  （无）' }
    foreach ($t in $leases) { Write-Host ("  {0,-12} {1}" -f $t.id, $t.title) }
    exit 0
}

# -------------------------------------------------------------- 回收 ----
function Invoke-Reap {
    $dry = -not $Apply
    Write-Host "[sched] 回收（$(if ($dry) { 'dry-run，加 -Apply 才真删' } else { '执行' })）"
    $cut = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - ($LeaseTtlMin * 60)

    # 1) 过期任务：释放租约 + 清掉它登记的副本
    foreach ($t in Get-AllTasks) {
        if ([int64]$t.heartbeatUnix -ge $cut) { continue }
        Write-Host "  过期任务 '$($t.title)'（lease=$($t.id)，最后心跳 $($t.heartbeatUtc)）"
        foreach ($p in @($t.scratch) + @($t.worktrees)) {
            if (-not $p) { continue }
            $msg = Remove-RegisteredPath $p -Dry:$dry
            if ($msg) { Write-Host "    $msg" }
        }
        if (-not $dry) { Remove-TaskLease $t.id }
    }

    # 2) 仓库外的 worktree 副本：没人引用 + 干净 + HEAD 已在 main 上 → 回收。
    #    "HEAD 是 main 的祖先"是"内容没丢"的证明（规则 9：删前先证明）。
    $wt = & git -C $root worktree list --porcelain 2>$null
    $paths = @()
    foreach ($line in $wt) { if ($line -like 'worktree *') { $paths += $line.Substring(9) } }
    $mainRoot = [System.IO.Path]::GetFullPath($root)
    $referenced = @()
    foreach ($t in Get-LiveTasks) { foreach ($w in @($t.worktrees)) { if ($w) { $referenced += [System.IO.Path]::GetFullPath($w) } } }

    foreach ($p in $paths) {
        $full = [System.IO.Path]::GetFullPath($p)
        if ($full.Equals($mainRoot, [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($referenced -contains $full) { Write-Host "  保留副本 $full（活跃任务登记在用）"; continue }
        $dirty = (& git -C $full status --porcelain 2>$null | Measure-Object).Count
        if ($dirty -gt 0) { Write-Host "  保留副本 $full（有 $dirty 处未提交改动，不敢删）"; continue }
        $head = (& git -C $full rev-parse HEAD 2>$null)
        $isAncestor = $false
        if ($head) {
            & git -C $root merge-base --is-ancestor $head main 2>$null
            $isAncestor = ($LASTEXITCODE -eq 0)
        }
        if (-not $isAncestor) { Write-Host "  保留副本 $full（HEAD 不在 main 上，有独有提交）"; continue }
        if ($dry) { Write-Host "  将回收副本 $full（干净且内容已在 main）"; continue }
        & git -C $root worktree remove --force $full 2>&1 | Out-Null
        if (Test-Path $full) { Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue }
        Write-Host "  已回收副本 $full" -ForegroundColor Green
    }
    exit 0
}

# ---------------------------------------------------------------- 分发 ----
switch ($Command.ToLowerInvariant()) {
    'lock' { Invoke-Lock }
    'unlock' { Invoke-Unlock }
    'lock-status' { Invoke-LockStatus }
    'register' { Invoke-Register }
    'heartbeat' { Invoke-Heartbeat }
    'finish' { Invoke-Finish }
    'status' { Invoke-Status }
    'reap' { Invoke-Reap }
    default {
        Write-Error "[sched] 未知命令：$Command（lock/unlock/lock-status/register/heartbeat/finish/status/reap）"
        exit 2
    }
}

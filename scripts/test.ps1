# Runs ctest. Tier runs (no -Match) are RELEASE GATES ONLY; routine verification
# is targeted and budget-checked:
#
#   scripts\test.ps1 -Match "case_xlsx_write"        # targeted, all tiers, ≤80 hits
#   scripts\test.ps1 -Match "^(case_a|case_b)$"      # anchored: never hits the det twin
#   scripts\test.ps1 smoke -ReleaseGate              # RELEASE GATE ONLY (~406 cases)
#   scripts\test.ps1 standard -ReleaseGate           # RELEASE GATE ONLY (~2558 cases)
#
# IMPORTANT: Per AGENTS.md Rule 8, tier runs without -Match are RELEASE GATES
# ONLY, NEVER routine verification. Machine-enforced below (机制落地，不靠自觉):
#   * budget guard: ANY run whose ctest hit count exceeds -MaxMatch (default 80)
#     is refused — smoke included. 单跑一条是秒级；一组上百条在缓存失效时就是十几分钟。
#   * rerun guard: the same -Match group twice within -RecheckMin (default 10 min)
#     is refused — rerun only the single test whose output changed.
#   * targeted runs (-Match) automatically exclude the full-tier det twins
#     (^case_det_) — that lens costs two compiles per case and is release-gate only.
#
# Nothing else may compile while this runs: the cases share build\zanc.exe and
# the stdlib stamp, so a concurrent build makes unrelated cases fail.
param(
    [ValidateSet('smoke', 'standard', 'full')]
    [string]$Tier = 'smoke',
    [string]$Match = '',
    [int]$Jobs = 0,
    [int]$MaxMatch = 80,
    [int]$RecheckMin = 10,
    [switch]$ReleaseGate,
    [switch]$Force,
    [string]$Build = (Join-Path $PSScriptRoot '../build')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

# 契约：多平台交叉编译与驱动打包管线
# 契约：测试套件与发布门控调度规范
# 契约：工程辅助自动化与脚本执行规范
$TierExplicit = $PSBoundParameters.ContainsKey('Tier')
$TierName = if ($TierExplicit) { $Tier } elseif ($Match -ne '') { 'targeted' } else { $Tier }
$gateSkip = $Force -or $ReleaseGate -or $env:CI -or $env:ZAN_RELEASE_GATE
$buildDir = [IO.Path]::GetFullPath($Build)
$ctestBase = @('--test-dir', $buildDir, '-C', 'Release')

# ---- 防呆门 1：整档（无 -Match）是发布门，日常验证拒绝 ----------------------
# （standard/full 裸跑在这里被拒；smoke 裸跑 ≈406 条，由门 2 的预算机检拦住）
if (($Tier -eq 'standard' -or $Tier -eq 'full') -and $Match -eq '' -and -not $gateSkip) {
    Write-Host "[test] REJECTED: Running '$Tier' tier without -Match compiles 1000+ full programs and saturates all CPU cores." -ForegroundColor Red
    Write-Host "[test] Per AGENTS.md Rule 8, full-tier runs are RELEASE GATES ONLY, NEVER routine development verification." -ForegroundColor Yellow
    Write-Host "[test] - For routine/iterative verification, run the single affected test or probe:" -ForegroundColor Cyan
    Write-Host "    ctest -R <name> --output-on-failure"
    Write-Host "    scripts\test.ps1 -Match <name>"
    Write-Host "[test] - If this is an intentional release gate run, add -ReleaseGate:" -ForegroundColor Cyan
    Write-Host "    scripts\test.ps1 $Tier -ReleaseGate"
    exit 1
}

if (-not $gateSkip) {
    # ---- 防呆门 2：命中面预算（ctest -N 先数，秒级）-------------------------
    # 任何档位、任何调用方：命中数超 -MaxMatch 一律拒绝。这不是刁难——
    # 预算内的一组在缓存命中时是几十秒，超预算的一组在 zanc/stdlib 刚变过时
    # 就是十几分钟起步。"想确认一下就跑一大组"正是要拦的行为。
    $probeArgs = $ctestBase + @('-N')
    if ($TierExplicit -or $Match -eq '') { $probeArgs += @('-L', $Tier) }   # 裸跑 = smoke 档，别全档
    if ($Match -ne '') { $probeArgs += @('-R', $Match) }
    $probeOut = (& ctest @probeArgs 2>&1 | Out-String)
    $hitNames = @([regex]::Matches($probeOut, 'Test\s+#\d+:\s*(\S+)') | ForEach-Object { $_.Groups[1].Value })
    $hits = $hitNames.Count
    if ($hits -gt $MaxMatch) {
        Write-Host "[test] REJECTED: 命中 $hits 条测试，超预算 $MaxMatch（命中面预算机检，AGENTS.md Rule 8）。" -ForegroundColor Red
        Write-Host "[test] 先收窄再跑，不要硬跑大组：" -ForegroundColor Yellow
        Write-Host "[test] - 用 tools\agent\verify.ps1 -Paths <改动文件> 按 import 依赖推导最小命中面" -ForegroundColor Cyan
        Write-Host "[test] - 锚定单条：ctest -R `"^<name>$`"（注意 det 孪生 case_det_* 只属 full，别误中）" -ForegroundColor Cyan
        Write-Host "[test] - 确要跑大组/整档：加 -Force（发布门用 -ReleaseGate，并先看 build_gate.ps1 -Status）" -ForegroundColor Cyan
        Write-Host ("[test] 命中样例： " + (($hitNames | Select-Object -First 12) -join ', '))
        exit 1
    }

    # ---- 防呆门 3：同组重复跑 -----------------------------------------------
    # 一轮任务收口时验证一次。"每改一处就重跑整组"不是勤奋是内耗——重跑只重跑
    # 输出会变的那条。状态记在 build\test-out\，跨会话共享。
    if ($Match -ne '') {
        $stateDir = Join-Path $root 'build\test-out'
        $stateFile = Join-Path $stateDir '.last-targeted.json'
        $nowUnix = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        $prev = $null
        if (Test-Path $stateFile) {
            try { $prev = Get-Content $stateFile -Raw -ErrorAction Stop | ConvertFrom-Json } catch { }
        }
        if ($prev -and "$($prev.match)" -eq $Match -and (($nowUnix - [int64]$prev.unixTime) -lt ($RecheckMin * 60))) {
            Write-Host "[test] REJECTED: 同一组 -Match '$Match' 在 $RecheckMin 分钟内已跑过（重复组机检）。" -ForegroundColor Red
            Write-Host "[test] 改动之后只重跑输出会变的那条：ctest -R `"^<name>$`"；确要重跑整组加 -Force。" -ForegroundColor Yellow
            exit 1
        }
        if (-not (Test-Path $stateDir)) { New-Item -ItemType Directory -Force -Path $stateDir | Out-Null }
        @{ match = $Match; unixTime = $nowUnix } | ConvertTo-Json | Set-Content -Path $stateFile -Encoding ASCII
    }
}

# -Match 给了而档位没显式给 → 不加 -L，让 -R 跨档匹配。旧写法 -L smoke 会把
# conformance/case（standard 档标签）全部滤掉，永远 "No tests were found"，
# 会话拿不到绿灯就误判升级去跑更大的档。无 -Match 时保留档位过滤（裸跑 = smoke）。
$ctestArgs = $ctestBase + @('-j', $Jobs, '--output-on-failure')
if ($TierExplicit -or $Match -eq '') { $ctestArgs += @('-L', $Tier) }
if (-not $gateSkip -and $Match -ne '') {
    # det 孪生（determinism lens，每案双编译、百秒级）只属发布门：日常定向跑
    # 一律排除，别让一条 -Match 悄悄把成本翻倍。
    $ctestArgs += @('-R', $Match, '-E', '^case_det_')
}
elseif ($Match -ne '') { $ctestArgs += @('-R', $Match) }

Write-Host "[test] tier=$TierName jobs=$Jobs$(if ($Match) { " match=$Match" })$(if (-not $gateSkip) { " budget=$MaxMatch" })"

# ---- 产物收口：测试跑完不留垃圾，汇总日志合并成一份 ----------------------
# 用例产物已经全部收进 build\cases\（CMakeLists 里统一改的路径），跑完只按
# "最近用过就留、老的就扔"清扫——它是缓存，不是垃圾，热的工作集要保住。
# 同时把 ctest 那堆一次性输出合并成一份日志，别再每个用例散一个文件。
$cleanScript = Join-Path $PSScriptRoot 'clean_build.ps1'

# ctest 的临时日志残留（它自己不会清），跑之前先扫掉
Get-ChildItem -LiteralPath (Join-Path $buildDir 'Testing\Temporary') -File -Filter 'LastTest.log.tmp*' -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }

# 共享构建资源的串行闸：ctest 用的是 build\zanc.exe 与 stdlib stamp，
# 别的会话此刻重编就会让本轮用例成片假红。拿不到就排队等，别另开一棵
# 构建树去躲（那只是把共享状态藏起来，公共包照样漂移）。
# 锁名与 scripts/build.ps1 相同（'build'）——编译与测试必须互斥：
# 编译持锁时测试排队，测试持锁时编译排队。
# 详见 scripts/build_gate.ps1 顶部的说明。
#
# 退出码经临时文件回传：`& $gate` 在子作用域里跑，$script: 变量穿不过去
# （各自的 script scope），而闭包里的 $LASTEXITCODE 同样出不来。
$gate = Join-Path $PSScriptRoot 'build_gate.ps1'
$schedPath = Join-Path $PSScriptRoot 'sched.ps1'
$codeFile = Join-Path $env:TEMP "zan_ctest_code_$PID.txt"
$logFile  = Join-Path $env:TEMP "zan_ctest_log_$PID.txt"
Remove-Item -LiteralPath $codeFile -Force -ErrorAction SilentlyContinue
& $gate -Name 'build' -Script {
    # 顺手回收上一轮留下的副本（过期任务租约 + 没人引用的仓库外 worktree）。
    # 在闸里跑，与编译互斥。
    if (Test-Path $schedPath) { & $schedPath reap -Apply | Out-Null }
    & ctest @ctestArgs 2>&1 | Tee-Object -FilePath $logFile
    # Child scripts (reap) can shadow LASTEXITCODE locally; native exits are global.
    $ct = $global:LASTEXITCODE
    # ctest 遇到"没匹配到任何用例"时返回 0，但那不是"验证通过"，是
    # "什么都没跑"。把它判成失败——否则一次拼错的 -Match 会伪装成绿灯。
    if ($ct -eq 0 -and (Select-String -Path $logFile -Pattern 'No tests were found' -Quiet)) {
        $ct = 1
    }
    Set-Content -Path $codeFile -Value $ct -Encoding ASCII
}.GetNewClosure()
$gateExit = $LASTEXITCODE

$code = 1
if (Test-Path $codeFile) {
    $raw = (Get-Content $codeFile -Raw).Trim()
    if ($raw -match '^\d+$') { $code = [int]$raw }
    Remove-Item -LiteralPath $codeFile -Force -ErrorAction SilentlyContinue
}

if ($gateExit -ne 0) { $code = $gateExit }

# ---- 产物收口（收尾）：本次跑出来的东西不留着下次再堆 --------------------
# 1) 失败用例名（留它们的 .ll 供排查，其余中间产物清掉）
$failedNames = @()
if (Test-Path $logFile) {
    Select-String -Path $logFile -Pattern 'FAIL\s+([A-Za-z0-9_]+):' -ErrorAction SilentlyContinue |
        ForEach-Object { $failedNames += $_.Matches[0].Groups[1].Value }

    # 2) 汇总日志合并归档到一处，只留最近 5 份（原来是每个用例一个文件散在
    #    build\ 里，现在是 build\test-out\<tier>-<时间>.log 一份）
    $outDir = Join-Path $buildDir 'test-out'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $archive = Join-Path $outDir "$TierName-$stamp.log"
    Copy-Item -LiteralPath $logFile -Destination $archive -Force
    Get-ChildItem -LiteralPath $outDir -File -Filter '*.log' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -Skip 5 |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }
    Write-Host "[test] 本轮输出已合并归档：$archive（只留最近 5 份）"
}

# 3) 自举对拍的中间产物：每个用例一对 .ll/.exe。成功就不留，失败只留失败用例的
#    .ll（IR 是排查"两边为什么不一样"的唯一线索）
$removedMid = 0
Get-ChildItem -LiteralPath $buildDir -Directory -Filter 'selfhost_diff*' -ErrorAction SilentlyContinue | ForEach-Object {
    Get-ChildItem -LiteralPath $_.FullName -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.exe', '.ll') -and $failedNames -notcontains $_.BaseName } |
        ForEach-Object {
            try { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction Stop; $removedMid++ } catch { }
        }
}

# 4) 用例缓存与历史遗留：按"最近用过就留、老的就扔"清扫（热的留，冷的扔），
#    不是把缓存一把删光——run_case.cmake 会按"源文件/编译器/stdlib 没变就复用
#    旧 exe"来加速，全删等于每轮都重编一千多个程序。
if (Test-Path $cleanScript) {
    & $cleanScript -Apply -Build $buildDir
}

if ($removedMid -gt 0) {
    Write-Host "[test] 产物收口：清掉 $removedMid 个对拍中间产物（失败用例的 .ll 保留）"
}

Remove-Item -LiteralPath $logFile -Force -ErrorAction SilentlyContinue
if ($code -eq 0) { Write-Host "TEST_OK tier=$TierName" }
else { Write-Host "TEST_FAIL tier=$TierName exit=$code" }
exit $code

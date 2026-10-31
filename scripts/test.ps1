# Runs one tier of the test suite (see the "Test tiers" block in CMakeLists).
#
#   scripts\test.ps1                 # smoke: compiler gates only (~300 cases)
#   scripts\test.ps1 standard -ReleaseGate  # RELEASE GATE ONLY: 1000+ full programs
# 契约：测试套件与发布门控调度规范
#   scripts\test.ps1 smoke -Match gui       # targeted: only tests matching a regex
#
# IMPORTANT: Per AGENTS.md Rule 8, full tier runs (standard / full without -Match)
# are RELEASE GATES ONLY. Do NOT run them as routine or pre-commit checks.
# Routine verification must be targeted: build a probe or run:
#   ctest -R <test_name> --output-on-failure
#
# Nothing else may compile while this runs: the cases share build\zanc.exe and
# the stdlib stamp, so a concurrent build makes unrelated cases fail.
param(
    [ValidateSet('smoke', 'standard', 'full')]
    [string]$Tier = 'smoke',
    [string]$Match = '',
    [int]$Jobs = 0,
    [switch]$ReleaseGate,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

# 契约：多平台交叉编译与驱动打包管线
# 契约：测试套件与发布门控调度规范
# 契约：工程辅助自动化与脚本执行规范
if (($Tier -eq 'standard' -or $Tier -eq 'full') -and $Match -eq '' -and -not $ReleaseGate -and -not $Force -and -not $env:CI -and -not $env:ZAN_RELEASE_GATE) {
    Write-Host "[test] REJECTED: Running '$Tier' tier without -Match compiles 1000+ full programs and saturates all CPU cores." -ForegroundColor Red
    Write-Host "[test] Per AGENTS.md Rule 8, full-tier runs are RELEASE GATES ONLY, NEVER routine development verification." -ForegroundColor Yellow
    Write-Host "[test] - For routine/iterative verification, run the single affected test or probe:" -ForegroundColor Cyan
    Write-Host "    ctest -R <name> --output-on-failure"
    Write-Host "    scripts\test.ps1 $Tier -Match <name>"
    Write-Host "[test] - If this is an intentional release gate run, add -ReleaseGate:" -ForegroundColor Cyan
    Write-Host "    scripts\test.ps1 $Tier -ReleaseGate"
    exit 1
}

$ctestArgs = @('--test-dir', (Join-Path $root 'build'), '-C', 'Release',
               '-j', $Jobs, '--output-on-failure', '-L', $Tier)
if ($Match -ne '') { $ctestArgs += @('-R', $Match) }

Write-Host "[test] tier=$Tier jobs=$Jobs$(if ($Match) { " match=$Match" })"

# ---- 产物收口：测试跑完不留垃圾，汇总日志合并成一份 ----------------------
# 用例产物已经全部收进 build\cases\（CMakeLists 里统一改的路径），跑完只按
# "最近用过就留、老的就扔"清扫——它是缓存，不是垃圾，热的工作集要保住。
# 同时把 ctest 那堆一次性输出合并成一份日志，别再每个用例散一个文件。
$buildDir = Join-Path $root 'build'
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
& $gate -Name 'build' -Script {
    # 顺手回收上一轮留下的副本（过期任务租约 + 没人引用的仓库外 worktree）。
    # 在闸里跑，与编译互斥。
    if (Test-Path $schedPath) { & $schedPath reap -Apply | Out-Null }
    & ctest @ctestArgs 2>&1 | Tee-Object -FilePath $logFile
    $ct = $LASTEXITCODE
    # ctest 遇到"没匹配到任何用例"时返回 0，但那不是"验证通过"，是
    # "什么都没跑"。把它判成失败——否则一次拼错的 -Match 会伪装成绿灯。
    if ($ct -eq 0 -and (Select-String -Path $logFile -Pattern 'No tests were found' -Quiet)) {
        $ct = 1
    }
    Set-Content -Path $codeFile -Value $ct -Encoding ASCII
}.GetNewClosure()

$code = 1
if (Test-Path $codeFile) {
    $raw = (Get-Content $codeFile -Raw).Trim()
    if ($raw -match '^\d+$') { $code = [int]$raw }
    Remove-Item -LiteralPath $codeFile -Force -ErrorAction SilentlyContinue
}

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
    $archive = Join-Path $outDir "$Tier-$stamp.log"
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
    & $cleanScript -Apply
}

if ($removedMid -gt 0) {
    Write-Host "[test] 产物收口：清掉 $removedMid 个对拍中间产物（失败用例的 .ll 保留）"
}

Remove-Item -LiteralPath $logFile -Force -ErrorAction SilentlyContinue
if ($code -eq 0) { Write-Host "TEST_OK tier=$Tier" }
else { Write-Host "TEST_FAIL tier=$Tier exit=$code" }
exit $code

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
$codeFile = Join-Path $env:TEMP "zan_ctest_code_$PID.txt"
$logFile  = Join-Path $env:TEMP "zan_ctest_log_$PID.txt"
& $gate -Name 'build' -Script {
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
Remove-Item -LiteralPath $logFile -Force -ErrorAction SilentlyContinue
if ($code -eq 0) { Write-Host "TEST_OK tier=$Tier" }
else { Write-Host "TEST_FAIL tier=$Tier exit=$code" }
exit $code

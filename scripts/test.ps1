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

$args = @('--test-dir', (Join-Path $root 'build'), '-C', 'Release',
          '-j', $Jobs, '--output-on-failure', '-L', $Tier)
if ($Match -ne '') { $args += @('-R', $Match) }

Write-Host "[test] tier=$Tier jobs=$Jobs$(if ($Match) { " match=$Match" })"
& ctest @args
$code = $LASTEXITCODE
if ($code -eq 0) { Write-Host "TEST_OK tier=$Tier" }
else { Write-Host "TEST_FAIL tier=$Tier exit=$code" }
exit $code

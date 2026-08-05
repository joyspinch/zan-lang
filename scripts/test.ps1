# Runs one tier of the test suite (see the "Test tiers" block in CMakeLists).
#
#   scripts\test.ps1                 # smoke: compiler gates, seconds
#   scripts\test.ps1 standard        # + every conformance program, pre-commit
#   scripts\test.ps1 full            # + determinism / leakcheck / self-host
#   scripts\test.ps1 smoke -Match gui   # only tests matching a regex
#
# Nothing else may compile while this runs: the cases share build\zanc.exe and
# the stdlib stamp, so a concurrent build makes unrelated cases fail.
param(
    [ValidateSet('smoke', 'standard', 'full')]
    [string]$Tier = 'smoke',
    [string]$Match = '',
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

$args = @('--test-dir', (Join-Path $root 'build'), '-C', 'Release',
          '-j', $Jobs, '--output-on-failure', '-L', $Tier)
if ($Match -ne '') { $args += @('-R', $Match) }

Write-Host "[test] tier=$Tier jobs=$Jobs$(if ($Match) { " match=$Match" })"
& ctest @args
$code = $LASTEXITCODE
if ($code -eq 0) { Write-Host "TEST_OK tier=$Tier" }
else { Write-Host "TEST_FAIL tier=$Tier exit=$code" }
exit $code

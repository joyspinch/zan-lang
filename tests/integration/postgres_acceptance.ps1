# The caller owns scripts/build_gate.ps1 for a full run. PreflightOnly uses no compiler.
[CmdletBinding()]
param(
    [string]$Python = 'python',
    [string]$Distro = 'Ubuntu',
    [string]$WslUser = 'qq',
    [string]$PgBin = '/usr/lib/postgresql/16/bin',
    [string]$Compiler,
    [switch]$SkipBuild,
    [switch]$PreflightOnly,
    [int]$StressWorkers = 0,
    [int]$StressLoops = 100,
    [int]$StressTimeout = 180,
    [switch]$Restart,
    [int]$RestartTimeout = 90,
    [int]$CompileTimeout = 300,
    [int]$FixtureTimeout = 120,
    [int]$WslStartTimeout = 120
)
$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'postgres_acceptance.py'
$runnerArgs = @('-I', $runner, '--distro', $Distro, '--wsl-user', $WslUser,
    '--pg-bin', $PgBin, '--compile-timeout', "$CompileTimeout",
    '--fixture-timeout', "$FixtureTimeout", '--wsl-start-timeout', "$WslStartTimeout",
    '--stress-workers', "$StressWorkers", '--stress-loops', "$StressLoops",
    '--stress-timeout', "$StressTimeout", '--restart-timeout', "$RestartTimeout")
if ($Compiler) { $runnerArgs += @('--compiler', $Compiler) }
if ($SkipBuild) { $runnerArgs += '--skip-build' }
if ($PreflightOnly) { $runnerArgs += '--preflight-only' }
if ($Restart) { $runnerArgs += '--restart' }
& $Python @runnerArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
exit 0

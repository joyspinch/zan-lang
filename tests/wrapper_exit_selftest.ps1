# Standalone synthetic regression: do not register inside ctest, whose outer
# build lock would deadlock the wrappers taking that same lock in child pwsh.
# Run: pwsh -NoProfile -File tests/wrapper_exit_selftest.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pwsh = (Get-Process -Id $PID).Path
$fixture = Join-Path $root ("build/test-out/wrapper-exit-" + [guid]::NewGuid().ToString('N'))
$cmake = (Get-Command cmake -CommandType Application).Source
$ctest = (Get-Command ctest -CommandType Application).Source
$gate = Join-Path $root 'scripts/build_gate.ps1'
$testWrapper = Join-Path $root 'scripts/test.ps1'
$buildWrapper = Join-Path $root 'scripts/build.ps1'
function Assert-Exit([int]$Actual, [int]$Expected, [string]$Context) {
    if ($Actual -ne $Expected) { throw "$Context exit=$Actual expected=$Expected" }
}
try {
    New-Item -ItemType Directory -Path $fixture -Force | Out-Null
    # A native failure through the gate must survive scheduler unlock (exit 0).
    & $pwsh -NoProfile -File $gate -Command '& cmake -E false'
    Assert-Exit $LASTEXITCODE 1 'gate native failure'
    & $pwsh -NoProfile -File $gate -Command 'throw "synthetic gate exception"'
    Assert-Exit $LASTEXITCODE 1 'gate exception'
    & $pwsh -NoProfile -File $gate -Command '& cmake -E true'
    Assert-Exit $LASTEXITCODE 0 'gate success'

    $cmakePath = $cmake.Replace('\', '/')
    $manifest = 'add_test(synthetic_sentinel "' + $cmakePath + '" -E false)' + "`n"
    Set-Content -LiteralPath (Join-Path $fixture 'CTestTestfile.cmake') -Value $manifest
    # Establish the real CTest exit status, then require the wrapper to preserve
    # that exact number through reap, gate, log archiving and successful cleanup.
    & $pwsh -NoProfile -File $gate -Command "& '$ctest' --test-dir '$fixture' -R '^synthetic_sentinel$' --output-on-failure"
    $expected = $LASTEXITCODE
    if ($expected -eq 0) { throw 'Synthetic failing CTest returned success' }
    & $pwsh -NoProfile -File $testWrapper -Build $fixture -Match '^synthetic_sentinel$' -Force -Jobs 1
    Assert-Exit $LASTEXITCODE $expected 'test wrapper sentinel failure'
    Set-Content -LiteralPath (Join-Path $fixture 'CTestTestfile.cmake') -Value $manifest.Replace('-E false', '-E true')
    & $pwsh -NoProfile -File $testWrapper -Build $fixture -Match '^synthetic_sentinel$' -Force -Jobs 1
    Assert-Exit $LASTEXITCODE 0 'test wrapper success'

    # No compiler/source build: project NONE has only two command sentinels.
    $source = Join-Path $fixture 'source'
    $binary = Join-Path $fixture 'binary'
    New-Item -ItemType Directory -Path $source | Out-Null
    @'
cmake_minimum_required(VERSION 3.20)
project(wrapper_exit NONE)
add_custom_target(sentinel_fail COMMAND "${CMAKE_COMMAND}" -E false)
add_custom_target(sentinel_pass COMMAND "${CMAKE_COMMAND}" -E true)
'@ | Set-Content -LiteralPath (Join-Path $source 'CMakeLists.txt')
    & $pwsh -NoProfile -File $gate -Command "& '$cmake' -S '$source' -B '$binary' -G Ninja"
    Assert-Exit $LASTEXITCODE 0 'synthetic configure'
    & $pwsh -NoProfile -File $buildWrapper -Build $binary -Target sentinel_fail
    Assert-Exit $LASTEXITCODE 1 'build wrapper sentinel failure'
    & $pwsh -NoProfile -File $buildWrapper -Build $binary -Target sentinel_pass
    Assert-Exit $LASTEXITCODE 0 'build wrapper success'
    Write-Host 'wrapper exit regression passed'
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

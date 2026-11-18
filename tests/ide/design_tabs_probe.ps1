# Run the real IDE against the checked-in fixture and inspect retained controls.
param(
    [string]$Executable = 'build/cases/ide-retained-input.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = [IO.Path]::GetFullPath((Join-Path $root $Executable))
if (!(Test-Path -LiteralPath $exe)) { throw "IDE executable missing: $exe" }
$work = Join-Path $root '_scratch/design_tabs_probe'
$cfg = Join-Path $work 'cfg'
$out = Join-Path $root 'build/test-out/design-tabs-probe'
$proj = Join-Path $PSScriptRoot 'design_tabs_fixture'
foreach ($directory in @($cfg, $out)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
foreach ($name in @('tree_design.json', 'tree_code.json', 'tree_returned.json')) {
    $artifact = Join-Path $out $name
    if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Force }
}
[IO.File]::WriteAllText((Join-Path $cfg 'lastproject.txt'), ($proj -replace '\\', '/'))
$script = Join-Path $work 'script.txt'
[IO.File]::WriteAllText($script, @"
wait 7000
dump tree tree_design.json
clickid @ide-code-tab
wait 2000
dump tree tree_code.json
clickid @ide-design-tab
wait 2000
dump tree tree_returned.json
quit
"@)
$previous = @{}
foreach ($key in @('ZAN_UI_SCRIPT', 'ZAN_UI_OUT', 'ZAN_IDE_CONFIG_DIR')) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key)
}
try {
    $env:ZAN_UI_SCRIPT = $script
    $env:ZAN_UI_OUT = $out
    $env:ZAN_IDE_CONFIG_DIR = $cfg
    $process = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru -NoNewWindow
    if (!$process.WaitForExit(90000)) { $process.Kill(); throw 'IDE probe timed out after 90 seconds' }
    if ($process.ExitCode -ne 0) { throw "IDE exited with $($process.ExitCode)" }
} finally {
    foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key, $previous[$key]) }
}
function Get-Tree([string]$name) {
    $path = Join-Path $out $name
    if (!(Test-Path -LiteralPath $path)) { throw "Retained tree dump missing: $path" }
    return [IO.File]::ReadAllText($path) | ConvertFrom-Json
}
function Find-Nodes($node, [string]$name) {
    if ($null -eq $node) { return }
    if ($node.name -eq $name) { $node }
    foreach ($child in $node.kids) { Find-Nodes $child $name }
}
$design = Get-Tree 'tree_design.json'
$code = Get-Tree 'tree_code.json'
$returned = Get-Tree 'tree_returned.json'
foreach ($tree in @($design, $code, $returned)) {
    foreach ($name in @('ide-design-tab', 'ide-code-tab')) {
        $target = @($tree.inputTargets | Where-Object { $_.name -eq $name -and $_.pointerHit -eq 1 })
        if ($target.Count -ne 1) { throw "Expected one reachable retained tab: $name" }
    }
}
$before = @(Find-Nodes $design.root 'ide-design-code-editor' | Where-Object { $_.visible -eq 1 })
$after = @(Find-Nodes $code.root 'ide-design-code-editor' | Where-Object { $_.visible -eq 1 })
$again = @(Find-Nodes $returned.root 'ide-design-code-editor' | Where-Object { $_.visible -eq 1 })
if ($before.Count -ne 0 -or $after.Count -ne 1 -or $again.Count -ne 0) {
    throw 'Retained design/code tab events did not switch the actual code editor both ways'
}
Write-Output 'DESIGN_TABS_PROBE_PASS'

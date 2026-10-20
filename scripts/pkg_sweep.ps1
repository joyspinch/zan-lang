# Whole-tree package health sweep: compile every .zan of each package in one
# unit, so files that no entry program references still get diagnosed. The
# stdlib/package on-demand pull is a dead-code shield -- a "born broken" file
# (the Zan.Game multi-arity Action batch) compiles green until the first
# template actually references it. One run of this script closes that gap for
# the whole package surface; rerun after any package split or bulk edit.
#
# Usage:  powershell -NoProfile -File scripts\pkg_sweep.ps1 [-Zanc build\zanc.exe]
# Output: per-package line in _scratch\pkg_sweep_report.txt (green/RED + first
#         errors); "SWEEP_DONE" marks completion.
#
# Windows PowerShell 5.1 caveats baked in: native-command stderr must not hit
# a Stop preference (it becomes a terminating NativeCommandError), and "`e"
# is not an escape there -- strip ANSI with [char]27 or the colored ": error"
# never matches and broken packages report green.

param([string]$Zanc = "build\zanc.exe")

$ErrorActionPreference = "Continue"
Set-Location (Join-Path $PSScriptRoot "..")
$zancPath = (Resolve-Path $Zanc).Path
$entry = "_scratch\sweep_entry.zan"
$report = "_scratch\pkg_sweep_report.txt"
$esc = [char]27
New-Item -ItemType Directory -Force -Path "_scratch" | Out-Null
Set-Content -Path $entry -Value "class SweepEntry {`n    static void Main() { }`n}"
Set-Content -Path $report -Value ""

foreach ($pkg in Get-ChildItem -Directory packages) {
    $files = Get-ChildItem -Recurse -Filter *.zan $pkg.FullName | Sort-Object FullName
    if (-not $files) { Add-Content $report "$($pkg.Name): NO_SOURCES"; continue }
    # Hand the package directory to the compiler: it expands *.zan itself, so
    # the command line stays constant-length no matter how many files the
    # package has. Listing the files ourselves overflowed the ~32K Windows
    # command-line limit for the big SDK packages and the process then never
    # started -- silently, because the failure text carries no compiler
    # diagnostic, so the package used to be reported GREEN while it had never
    # been compiled at all. A failed spawn is now RED, not green.
    $log = (& $zancPath $pkg.FullName $entry --auto-stdlib `
        -o "_scratch\sweep_pkg.exe" 2>&1 | Out-String) -replace "$esc\[[0-9;]*m", ""
    if ($log -match "failed to run" -or $log -match "文件名或扩展名太长") {
        Add-Content $report "$($pkg.Name): RED (spawn failed, $($files.Count) files)"
        ($log -split "`n" | Where-Object { $_ -match "failed to run|文件名或扩展名太长" } |
            Select-Object -First 3) | Add-Content $report
    } elseif ($log -match ": error") {
        Add-Content $report "$($pkg.Name): RED ($($files.Count) files)"
        ($log -split "`n" | Where-Object { $_ -match ": error" } |
            Select-Object -First 5) | Add-Content $report
    } else {
        Add-Content $report "$($pkg.Name): green ($($files.Count) files)"
    }
}
Add-Content $report "SWEEP_DONE"
Get-Content $report

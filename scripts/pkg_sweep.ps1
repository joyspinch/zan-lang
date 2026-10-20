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
    # Windows caps the command line at ~32K. The big SDK packages blow past it
    # and the process then never starts -- the script used to read that
    # "file name too long" text as "no : error" and report the package as
    # GREEN, i.e. a package that was never compiled at all looked healthy.
    # Feed the file list through an @argfile, and treat a failed spawn as RED.
    $argFile = "_scratch\sweep_args.txt"
    $argText = ($files.FullName | ForEach-Object { '"' + $_ + '"' }) -join "`n"
    [IO.File]::WriteAllText($argFile, $argText, (New-Object System.Text.UTF8Encoding($false)))
    $log = (& $zancPath $entry "@$argFile" --auto-stdlib `
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

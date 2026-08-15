# bump_version.ps1 -- Increments the patch component of the root VERSION file
# (the single source of truth every binary reports) and prints the new version.
#
# Releases are cut by scripts\publish_ide.ps1, which calls this first, so each
# published build carries a version nobody has to remember to raise. A release
# that must set major/minor edits VERSION by hand and publishes with -NoBump.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\bump_version.ps1
#         Add  -Part minor|major  to raise that component instead (the lower
#         components reset to 0, as semantic versioning expects).

param([ValidateSet('patch', 'minor', 'major')][string]$Part = 'patch')

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$file = Join-Path $root 'VERSION'

$current = (Get-Content $file -Raw).Trim()
if ($current -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
    Write-Output "BUMP_FAILED: VERSION is '$current', expected MAJOR.MINOR.PATCH"
    exit 1
}
$major = [int]$Matches[1]
$minor = [int]$Matches[2]
$patch = [int]$Matches[3]

if ($Part -eq 'major') { $major = $major + 1; $minor = 0; $patch = 0 }
elseif ($Part -eq 'minor') { $minor = $minor + 1; $patch = 0 }
else { $patch = $patch + 1 }

$next = "$major.$minor.$patch"
# No trailing newline juggling: the file is read with .Trim() everywhere (CMake
# strips it, build_ide.ps1 trims it), and one terminating newline keeps it a
# well-formed text file.
[System.IO.File]::WriteAllText($file, "$next`n")
Write-Output "VERSION_BUMPED $current -> $next"
# 显式成功退出：调用方（publish_ide.ps1）按 $LASTEXITCODE 判断这一步，
# 而纯 PowerShell 脚本不设置它——不写这一行，上一条原生命令留下的
# 退出码就会被当成"版本号没升上去"，发布在第一步直接失败。
exit 0

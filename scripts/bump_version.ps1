# bump_version.ps1 -- Increments VERSION and the gen1 identity before the
# release build. scripts\publish_ide.ps1 packages the selected version with -NoBump.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\bump_version.ps1
#         Add  -Part minor|major  to raise that component instead (the lower
#         components reset to 0, as semantic versioning expects).

param([ValidateSet('patch', 'minor', 'major')][string]$Part = 'patch')

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$file = Join-Path $root 'VERSION'
$driver = Join-Path $root 'src\selfhost\driver\main.zan'
$paths = @($file, $driver)
$streams = @{}
$original = @{}
$text = @{}
$attempted = @()

try {
    # Keep both files exclusively open through validation, writing and rollback.
    foreach ($path in $paths) {
        $stream = [System.IO.File]::Open($path, 'Open', 'ReadWrite', 'None')
        $streams[$path] = $stream
        $snapshot = [System.IO.MemoryStream]::new()
        $stream.CopyTo($snapshot)
        $original[$path] = $snapshot.ToArray()
        $snapshot.Position = 0
        $reader = [System.IO.StreamReader]::new($snapshot)
        try { $text[$path] = $reader.ReadToEnd() }
        finally { $reader.Dispose() }
    }

    $current = $text[$file].Trim()
    if ($current -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
        throw "VERSION is '$current', expected MAJOR.MINOR.PATCH"
    }
    $major = [int]$Matches[1]
    $minor = [int]$Matches[2]
    $patch = [int]$Matches[3]

    # The differential gate (B-ID189) requires gen1 and gen0 to agree.
    $driverText = $text[$driver]
    $pattern = '(?m)^[ \t]*static\s+string\s+SelfhostVersion\s*\(\s*\)\s*\{\s*return\s+"([^"\r\n]*)"\s*;\s*\}'
    $literals = [regex]::Matches($driverText, $pattern)
    if ($literals.Count -ne 1 -or $literals[0].Groups[1].Value -cne $current) {
        throw "Expected exactly one SelfhostVersion literal matching VERSION ($current) in src\selfhost\driver\main.zan"
    }
    $literal = $literals[0].Groups[1]

    if ($Part -eq 'major') { $major = $major + 1; $minor = 0; $patch = 0 }
    elseif ($Part -eq 'minor') { $minor = $minor + 1; $patch = 0 }
    else { $patch = $patch + 1 }

    $next = "$major.$minor.$patch"
    $updated = @{
        $file   = "$next`n"
        $driver = $driverText.Remove($literal.Index, $literal.Length).Insert($literal.Index, $next)
    }
    foreach ($path in $paths) {
        $stream = $streams[$path]
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($updated[$path])
        $attempted += $path
        $stream.Position = 0
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.SetLength($bytes.Length)
        $stream.Flush()
    }
} catch {
    Write-Output "BUMP_FAILED: $($_.Exception.Message)"
    foreach ($path in $attempted) {
        try {
            $stream = $streams[$path]
            $bytes = $original[$path]
            $stream.Position = 0
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.SetLength($bytes.Length)
            $stream.Flush()
        } catch {
            Write-Output "BUMP_FAILED: Could not restore '$path': $($_.Exception.Message)"
        }
    }
    exit 1
} finally {
    foreach ($stream in $streams.Values) { $stream.Dispose() }
}

Write-Output "VERSION_BUMPED $current -> $next"
# Set $LASTEXITCODE for callers that invoke this script from PowerShell.
exit 0

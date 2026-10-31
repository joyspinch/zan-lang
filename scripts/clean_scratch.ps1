# Sweep _scratch/: entries untouched for -KeepDays days are deleted.
#
# Why this exists: _scratch only ever grew. Sessions drop bisect trees, A/B
# compiler snapshots, SDK extractions and screenshot runs and never loop back
# to delete them, so it hit 48G by 2026-09. This script is the floor under the
# AGENTS.md rule 2 "cleanup is part of finishing the task" — run it when
# _scratch grows past a few GB:
#
#   powershell -File scripts\clean_scratch.ps1              # dry-run, lists everything
#   powershell -File scripts\clean_scratch.ps1 -Apply       # actually delete
#   powershell -File scripts\clean_scratch.ps1 -KeepDays 3 -Apply
#
# Rules:
# - Age is the NEWEST file mtime inside an entry, not the directory's own
#   mtime: deep writes do not update ancestor dir mtimes on NTFS, so using
#   the dir mtime would let an active session's tree look stale and get
#   deleted mid-run.
# - Entries carrying a README.md / README.txt / KEEP are keepers (SDKs,
#   toolchains, anything expensive to re-acquire) and are always skipped —
#   the convention is that anything you keep must say what it is and how to
#   re-acquire it (AGENTS.md rule 2).
# - Dry-run is the default; -Apply is what deletes. Deleting needs no further
#   confirmation because the dry-run output IS the review.

param(
    [int]$KeepDays = 7,
    [switch]$Apply,
    [string]$Scratch = (Join-Path $PSScriptRoot "..\_scratch")
)

$ErrorActionPreference = "Continue"

$scratchFull = [System.IO.Path]::GetFullPath($Scratch)
if (-not (Test-Path -LiteralPath $scratchFull)) {
    Write-Host "_scratch not found at $scratchFull - nothing to do."
    exit 0
}
if ((Split-Path $scratchFull -Leaf) -ne "_scratch") {
    Write-Error "Refusing to run: target is not a _scratch directory ($scratchFull)."
    exit 1
}

$cutoff = (Get-Date).AddDays(-1 * $KeepDays)
$entries = Get-ChildItem -LiteralPath $scratchFull -Force
if ($entries.Count -eq 0) {
    Write-Host "_scratch is empty."
    exit 0
}

function Get-EntryInfo([System.IO.FileSystemInfo]$e) {
    # Newest mtime across the entry (the entry itself for files).
    $newest = $e.LastWriteTime
    if ($e.PSIsContainer) {
        $latest = Get-ChildItem -LiteralPath $e.FullName -Recurse -Force -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($latest -and $latest.LastWriteTime -gt $newest) { $newest = $latest.LastWriteTime }
    }
    $size = 0L
    if ($e.PSIsContainer) {
        $sum = Get-ChildItem -LiteralPath $e.FullName -Recurse -Force -File -ErrorAction SilentlyContinue |
            Measure-Object Length -Sum
        if ($sum.Sum) { $size = [long]$sum.Sum }
    } else {
        $size = [long]$e.Length
    }
    return [pscustomobject]@{
        Path   = $e.FullName
        Name   = $e.Name
        Newest = $newest
        SizeMB = [math]::Round($size / 1MB, 1)
    }
}

$keepers = @()
$stale   = @()
$fresh   = @()

foreach ($e in $entries) {
    # NOTE: not `$bool -and @() | Where-Object` — -and binds tighter than the
    # pipeline, that shape filters a boolean and never sees the names.
    $hasKeeperMark = $false
    if ($e.PSIsContainer) {
        foreach ($mark in @("README.md", "README.txt", "KEEP")) {
            if (Test-Path -LiteralPath (Join-Path $e.FullName $mark)) { $hasKeeperMark = $true; break }
        }
    }
    $info = Get-EntryInfo $e
    if ($hasKeeperMark) {
        $keepers += $info
    } elseif ($info.Newest -lt $cutoff) {
        $stale += $info
    } else {
        $fresh += $info
    }
}

function Show-List($title, $list) {
    Write-Host ""
    Write-Host "== $title =="
    if ($list.Count -eq 0) { Write-Host "  (none)"; return }
    $sorted = @($list | Sort-Object SizeMB -Descending)
    $shown = if ($sorted.Count -gt 30) { $sorted[0..29] } else { $sorted }
    foreach ($i in $shown) {
        Write-Host ("  {0,10} MB  {1}  {2}" -f $i.SizeMB, $i.Newest.ToString("yyyy-MM-dd HH:mm"), $i.Name)
    }
    if ($sorted.Count -gt 30) {
        $restMB = ($sorted[30..($sorted.Count - 1)] | Measure-Object SizeMB -Sum).Sum
        Write-Host ("  ... and {0} more entries, {1} MB" -f ($sorted.Count - 30), [math]::Round($restMB, 1))
    }
}

Show-List "KEEPERS (README/KEEP marked, skipped)" $keepers
Show-List "TOO NEW (< $KeepDays days, kept)" $fresh
Show-List ("STALE (> $KeepDays days" + $(if ($Apply) { ", DELETING" } else { ", would delete" }) + ")") $stale

$totalMB = ($stale | Measure-Object SizeMB -Sum).Sum
if (-not $totalMB) { $totalMB = 0 }
Write-Host ""
if ($stale.Count -eq 0) {
    Write-Host ("Nothing stale (cutoff {0}). _scratch is clean by this policy." -f $cutoff.ToString("yyyy-MM-dd"))
    exit 0
}

if (-not $Apply) {
    Write-Host ("Dry run: {0} entries, {1} MB reclaimable. Re-run with -Apply to delete." -f $stale.Count, [math]::Round($totalMB, 1))
    exit 0
}

$failed = 0
foreach ($i in ($stale | Sort-Object @{Expression = { $_.Path.Length }; Descending = $true })) {
    # Longest paths first: deleting outer trees fails on Windows if a nested
    # entry was already removed and the dir handle lingers.
    $longPath = if ($i.Path.StartsWith("\\?\")) { $i.Path } else { "\\?\" + $i.Path }
    try {
        Remove-Item -LiteralPath $longPath -Recurse -Force -ErrorAction Stop
        Write-Host ("  deleted {0,10} MB  {1}" -f $i.SizeMB, $i.Name)
    } catch {
        # Some PowerShell builds dislike the \\?\ prefix on relative-ish
        # literals; retry bare before giving up.
        try {
            Remove-Item -LiteralPath $i.Path -Recurse -Force -ErrorAction Stop
            Write-Host ("  deleted {0,10} MB  {1}" -f $i.SizeMB, $i.Name)
        } catch {
            $failed++
            Write-Warning ("  FAILED {0}: {1}" -f $i.Name, $_.Exception.Message)
        }
    }
}

Write-Host ""
Write-Host ("Deleted {0}/{1} stale entries, reclaimed ~{2} MB." -f ($stale.Count - $failed), $stale.Count, [math]::Round($totalMB, 1))
if ($failed -gt 0) { exit 2 }
exit 0

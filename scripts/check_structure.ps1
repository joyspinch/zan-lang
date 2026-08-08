$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- ZanIDE structure guard ------------------------------------------------
# Enforces docs/projects/zanide/STRUCTURE.md so the project keeps its shape as
# it grows: one .zform per visual unit with a same-named code-behind, a fixed
# directory layering, no UI dependency inside services/models, and a file-size
# ceiling with a shrink-only baseline for the files still being split up.

$src        = Join-Path $root "src\ide_zan\src"
$baselineFn = Join-Path $root "docs\projects\zanide\oversize.baseline.txt"
$maxLines   = 800
$allowedDirs = @("shell", "panels", "editor", "dialogs", "pages", "services",
                 "models", "components", "views")
$errors = @()

# 1) every design document has a code-behind next to it
foreach ($f in Get-ChildItem $src -Recurse -Include *.zform) {
    $behind = [System.IO.Path]::ChangeExtension($f.FullName, ".zan")
    if (!(Test-Path -LiteralPath $behind)) {
        $errors += "missing code-behind: " + $f.FullName.Substring($root.Length + 1)
    }
}

# 2) top-level folders under src/ come from the documented layering
foreach ($d in Get-ChildItem $src -Directory) {
    if ($allowedDirs -notcontains $d.Name) {
        $errors += "undocumented folder: src\ide_zan\src\" + $d.Name +
                   " (see docs\projects\zanide\STRUCTURE.md)"
    }
}

# 3) services/ and models/ stay UI-free
foreach ($layer in @("services", "models")) {
    $dir = Join-Path $src $layer
    if (!(Test-Path -LiteralPath $dir)) { continue }
    foreach ($f in Get-ChildItem $dir -Recurse -Include *.zan) {
        $text = [System.IO.File]::ReadAllText($f.FullName)
        if ($text -match '(?m)^\s*using\s+Gui') {
            $errors += "UI dependency in " + $layer + "/: " + $f.Name +
                       " must not use Gui"
        }
    }
}

# 4) file size ceiling, with a shrink-only baseline of the not-yet-split files
$baseline = @{}
if (Test-Path -LiteralPath $baselineFn) {
    foreach ($line in [System.IO.File]::ReadAllLines($baselineFn)) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith("#")) { continue }
        $baseline[$t.ToLower()] = $true
    }
}
$stillOver = @()
foreach ($f in Get-ChildItem (Join-Path $root "src\ide_zan") -Recurse -Include *.zan) {
    $rel = $f.FullName.Substring($root.Length + 1).Replace("\", "/")
    $n = ([System.IO.File]::ReadAllLines($f.FullName)).Count
    if ($n -le $maxLines) { continue }
    if ($baseline.ContainsKey($rel.ToLower())) { $stillOver += $rel; continue }
    $errors += "over " + $maxLines + " lines (" + $n + "): " + $rel +
               " - split it by responsibility"
}
$gone = @()
foreach ($k in $baseline.Keys) {
    if ($stillOver -notcontains $k) { $gone += $k }
}

foreach ($e in $errors) { Write-Output ("STRUCTURE_ERROR " + $e) }
Write-Output ("[oversize] " + $stillOver.Count + " baselined file(s) still to split")
if ($gone.Count -gt 0) {
    Write-Output ("[oversize] " + $gone.Count +
        " baseline entr(y|ies) now under the limit - drop them from " +
        "docs\projects\zanide\oversize.baseline.txt")
}
if ($errors.Count -gt 0) {
    Write-Output ("STRUCTURE_FAILED " + $errors.Count)
    exit 1
}
Write-Output "STRUCTURE_OK"

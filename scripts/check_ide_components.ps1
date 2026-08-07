$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$files = Get-ChildItem (Join-Path $root "src\ide_zan") -Recurse -Filter *.zan
$patterns = @(
    "\bCanvas\s+[A-Za-z_]",
    "\.canvas\b",
    "\bDraw(Text|Rect|Line|Image|Icon)\s*\(",
    "\boverride\s+void\s+OnPaint\s*\("
)
$hits = @()
foreach ($file in $files) {
    $matches = Select-String -Path $file.FullName -Pattern $patterns
    foreach ($match in @($matches)) {
        $line = $match.Line.Trim()
        if ($line.StartsWith("//") -or $line.StartsWith("///")) { continue }
        if ($line.StartsWith('"') -or $line.EndsWith('"')) { continue }
        $hits += $match
    }
}
Write-Output ("DIRECT_CANVAS_HITS=" + $hits.Count)
$hits | ForEach-Object {
    Write-Output ((Resolve-Path $_.Path -Relative) + ":" + $_.LineNumber + ":" + $_.Line.Trim())
}
# This is an architecture inventory during migration. CI can turn it into a
# zero-hit gate after the remaining main-window/designer batches land.
exit 0

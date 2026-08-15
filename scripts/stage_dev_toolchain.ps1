# stage_dev_toolchain.ps1 -- Populate build\toolchain for a DEV IDE run.
#
# ZanIDE resolves the compiler and its friends relative to its own folder
# (ZanIDE.BaseDir()/toolchain -- see ZanIDE.Zanc / ToolchainDir), so an IDE
# started from build\ needs build\toolchain to look like the shipped
# dist\win-x64\toolchain. zanc.exe, zan-lsp.exe, zan-dap.exe and zanfmt.exe are
# resolved there with no usable fallback, and zanc then finds its linker /
# sysroots / runtime objects as its own siblings in that same folder.
#
# The list mirrors scripts\publish_ide.ps1 (same layout, same reasoning); this
# script only differs in copying into the build tree instead of a dist tree, and
# in being incremental so a rebuild restages in a second.
#
# Copying build\* wholesale instead would drop the destination into itself and
# recurse (build\toolchain\toolchain\toolchain\... -- 2.2 GB of it once), so
# every entry here is named explicitly and anything under $Dest is skipped.
#
#   powershell -File scripts\stage_dev_toolchain.ps1 [-Dest build\toolchain]
param(
    [string]$Build = "",
    [string]$Dest = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Build) { $Build = Join-Path $root "build" }
if (-not $Dest)  { $Dest = Join-Path $Build "toolchain" }
if (-not (Test-Path -LiteralPath $Build)) {
    Write-Output "STAGE_FAILED: no build directory: $Build"
    exit 1
}
$destFull = [IO.Path]::GetFullPath($Dest)

# A junction here (build\toolchain -> build, a shortcut some dev trees used so
# ZanIDE.ToolchainDir resolved to the build root) makes every mirror below walk
# through the link and mirror -- or purge -- the link target instead. Drop the
# link itself (Directory.Delete never touches what it points at) and stage into
# a real folder.
$destItem = Get-Item -LiteralPath $destFull -ErrorAction SilentlyContinue
if ($destItem -and ($destItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    Write-Output "STAGE_NOTE: $destFull was a link; replacing it with a real directory"
    [IO.Directory]::Delete($destFull)
}
New-Item -ItemType Directory -Force -Path $destFull | Out-Null

# Guards against staging the destination into itself.
function Test-UnderDest([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    return $full -eq $destFull -or
           $full.StartsWith($destFull + [IO.Path]::DirectorySeparatorChar,
                            [StringComparison]::OrdinalIgnoreCase)
}

$copied = 0
$missing = @()

function Stage-File([string]$src, [string]$name) {
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { return $false }
    if (Test-UnderDest $src) { return $false }
    $dst = Join-Path $destFull $name
    if ((Test-Path -LiteralPath $dst) -and
        (Get-Item -LiteralPath $dst).LastWriteTime -ge (Get-Item -LiteralPath $src).LastWriteTime) {
        return $true
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
    $script:copied = $script:copied + 1
    return $true
}

# robocopy mirrors incrementally (and copes with the deep MinGW/sysroot trees);
# exit codes 0-7 are success, 8+ are real failures. /XJ keeps the mirror from
# descending into (or purging through) junctions such as a build\stdlib link
# that points back at the source tree.
function Stage-Dir([string]$src, [string]$name) {
    if (-not (Test-Path -LiteralPath $src -PathType Container)) { return $false }
    if (Test-UnderDest $src) { return $false }
    $dst = Join-Path $destFull $name
    & robocopy $src $dst /MIR /XJ /NJH /NJS /NFL /NDL /NP | Out-Null
    if ($LASTEXITCODE -ge 8) {
        Write-Output "STAGE_FAILED: robocopy $src -> $dst ($LASTEXITCODE)"
        exit 1
    }
    $script:copied = $script:copied + 1
    return $true
}

# compiler + companion CLIs (the IDE resolves all four under toolchain\)
foreach ($exe in @("zanc.exe", "zan-lsp.exe", "zan-dap.exe",
                   "zanfmt.exe", "zandoc.exe")) {
    if (-not (Stage-File (Join-Path $Build $exe) $exe)) { $missing += $exe }
}

# bundled linker: ld.exe alone is useless without the MinGW-w64 runtime beside it
if (Stage-File (Join-Path $Build "ld.exe") "ld.exe") {
    if (-not (Stage-Dir (Join-Path $Build "mingw") "mingw")) { $missing += "mingw\" }
} else {
    $missing += "ld.exe"
}

# cross sysroots (--target linux-* / win-arm64 / macos ... ), each optional
foreach ($sub in @("linux-musl", "linux-arm64", "linux-riscv64", "win-x64",
                   "win-arm64", "wasm32", "riscv64", "macos")) {
    Stage-Dir (Join-Path $Build $sub) $sub | Out-Null
}

# runtime objects the IDE links explicitly (ZanIDE.RtSyncArg and friends)
foreach ($rt in (Get-ChildItem -LiteralPath $Build -File -Filter "zanrt_*" -ErrorAction SilentlyContinue |
                 Where-Object { $_.Extension -in ".o", ".obj" })) {
    Stage-File $rt.FullName $rt.Name | Out-Null
}

# native GUI runtime: without it the IDE cannot link type=gui projects
if (-not (Stage-File (Join-Path $Build "zan_gui.lib") "zan_gui.lib")) {
    $missing += "zan_gui.lib"
}

# A single-file publish embeds the resources in the exe (zanc --embed) and
# links the drivers statically, so no launcher stub is staged any more. Remove
# one left by an older dev tree, or the IDE would keep finding it.
Remove-Item -LiteralPath (Join-Path $destFull "pkg_stub.exe") -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $destFull "pack_single.ps1") -Force -ErrorAction SilentlyContinue

# new projects get their icon from here when no assets\zan.ico is shipped
Stage-File (Join-Path $root "assets\zan.ico") "zan.ico" | Out-Null

if ($missing.Count -gt 0) {
    Write-Output ("STAGE_WARN: missing in " + $Build + ": " + ($missing -join ", "))
}
Write-Output ("STAGE_TOOLCHAIN_OK -> " + $destFull + " (" + $copied + " item(s) updated)")

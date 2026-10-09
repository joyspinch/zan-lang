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
    if (-not (Stage-File (Join-Path $Build "ld.lld.exe") "ld.lld.exe")) { $missing += "ld.lld.exe" }
    if (-not (Stage-Dir (Join-Path $Build "mingw") "mingw")) { $missing += "mingw\" }
} else {
    $missing += "ld.exe"
}

# --emit-lib uses a real indexed archive; stage the host LLVM tool next to zanc.
function Find-HostLlvmAr([string]$buildDir) {
    $candidates = @((Join-Path $buildDir "llvm-ar.exe"))
    $cache = Join-Path $buildDir "CMakeCache.txt"
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        foreach ($key in @("LLVM_TOOLS_BINARY_DIR", "LLVM_DIR", "CMAKE_C_COMPILER")) {
            $entry = Select-String -LiteralPath $cache -Pattern "^${key}:[^=]*=(.+)$" |
                     Select-Object -First 1
            if (-not $entry) { continue }
            $value = $entry.Matches[0].Groups[1].Value.Trim()
            switch ($key) {
                "LLVM_TOOLS_BINARY_DIR" { $candidates += Join-Path $value "llvm-ar.exe" }
                "LLVM_DIR" { $candidates += Join-Path $value "../../../bin/llvm-ar.exe" }
                "CMAKE_C_COMPILER" { $candidates += Join-Path (Split-Path -Parent $value) "llvm-ar.exe" }
            }
        }
    }
    foreach ($prefix in @($env:LLVM_ROOT, $env:LLVM_PATH)) {
        if ($prefix) { $candidates += Join-Path $prefix "bin/llvm-ar.exe" }
    }
    $onPath = Get-Command llvm-ar.exe -CommandType Application -ErrorAction SilentlyContinue |
              Select-Object -First 1
    if ($onPath) { $candidates += $onPath.Source }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    return $null
}
$llvmAr = Find-HostLlvmAr $Build
if (-not $llvmAr -or -not (Stage-File $llvmAr "llvm-ar.exe")) { $missing += "llvm-ar.exe" }

# 契约：工程辅助自动化与脚本执行规范
foreach ($sub in @("linux-musl", "linux-arm64", "linux-riscv64", "win-x64",
                   "win-arm64", "wasm32", "riscv64", "macos", "ios",
                   "ohos-x64", "ohos-arm64")) {
    $srcSub = Join-Path $Build $sub
    if (-not (Test-Path $srcSub)) {
        $srcSub = Join-Path (Join-Path $root "toolchain") $sub
    }
    Stage-Dir $srcSub $sub | Out-Null
}

# runtime objects the IDE links explicitly (ZanIDE.RtSyncArg and friends)
foreach ($rt in (Get-ChildItem -LiteralPath $Build -File -ErrorAction SilentlyContinue |
                 Where-Object { ($_.Name -like 'zanrt_*' -or $_.Name -like 'zan_inflate*' -or $_.Name -like 'zan_embed_api*') -and $_.Extension -in '.o', '.obj' })) {
    Stage-File $rt.FullName $rt.Name | Out-Null
}

# Canonical driver ownership stays with packages; also stage the independently
# built native products for callers that use the toolchain as --driver-dir.
$nativeOwners = @{
    zan_gui = 'packages/Zan.Gui/src/Gui/drivers'
    zan_image = 'packages/Zan.Image/src/System/Drawing/Imaging/drivers'
    zan_audio = 'packages/Zan.Desktop/src/System/Media/drivers'
    zan_game = 'packages/Zan.Game/src/Game/Graphics/drivers'
}
foreach ($driver in @("zan_gui", "zan_image", "zan_audio", "zan_game")) {
    foreach ($name in @("$driver.dll", "$driver.lib", "lib$driver.dll.a")) {
        $nativeFile = Join-Path $Build $name
        if (Test-Path $nativeFile) { Stage-File $nativeFile $name | Out-Null }
    }
    $ownerDir = Join-Path (Join-Path $root $nativeOwners[$driver]) 'win-x64'
    $bundleName = "$driver.bundle"
    $bundle = Join-Path $Build $bundleName
    if (-not (Test-Path -LiteralPath $bundle -PathType Leaf)) { $bundle = Join-Path $ownerDir $bundleName }
    if (Test-Path -LiteralPath $bundle -PathType Leaf) {
        Stage-File $bundle $bundleName | Out-Null
        # Carry conditional runtime payloads (e.g. WebView2Loader if WebView).
        foreach ($entry in (Get-Content -LiteralPath $bundle)) {
            if ($entry -match '^\s*([^#@\s]+)\s+if\s+\S+\s*$') {
                $payloadName = $Matches[1]
                $payload = Join-Path $Build $payloadName
                if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) { $payload = Join-Path $ownerDir $payloadName }
                if (Test-Path -LiteralPath $payload -PathType Leaf) {
                    Stage-File $payload $payloadName | Out-Null
                }
            }
        }
    }
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

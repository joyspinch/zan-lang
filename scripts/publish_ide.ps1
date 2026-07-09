# publish_ide.ps1 -- Assemble a self-contained, redistributable Zan IDE into
# the canonical release directory  d:\project\zan-lang\dist .
#
# The published tree is intentionally minimal: only the files an end user needs
# to run the IDE and compile/run Zan programs. Nothing else should be dropped
# into dist -- treat it as the single, clean release output.
#
#   dist\
#     ide_demo.exe        the IDE
#     zanc.exe            the Zan compiler (found relative to the IDE at runtime)
#     stdlib\             standard library sources (zanc auto-includes from here)
#     examples\           a few sample programs opened by the IDE's Examples pane
#     README.txt          prerequisites + how to run
#
# Prerequisite on the target machine: an LLVM toolchain (clang, llvm-lib) on
# PATH, because zanc emits LLVM IR and shells out to clang to produce the exe.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\publish_ide.ps1
#         Add  -SkipBuild  to package the existing build\ artifacts as-is.

param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
Set-Location 'd:\project\zan-lang'

$root = (Get-Location).Path
$dist = Join-Path $root 'dist'

if (-not $SkipBuild) {
    Write-Output "[1/4] Building the IDE (scripts\build_ide.ps1) ..."
    & (Join-Path $root 'scripts\build_ide.ps1')
    if ($LASTEXITCODE -ne 0) { Write-Output "PUBLISH_FAILED: build error"; exit 1 }
}

# ---- required inputs ----
$ideExe  = Join-Path $root 'build\ide_demo.exe'
$zancExe = Join-Path $root 'build\zanc.exe'
$stdlib  = Join-Path $root 'stdlib'
foreach ($p in @($ideExe, $zancExe, $stdlib)) {
    if (-not (Test-Path $p)) { Write-Output "PUBLISH_FAILED: missing $p"; exit 1 }
}

# ---- clean + recreate dist (release output only) ----
Write-Output "[2/4] Preparing clean dist directory: $dist"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist | Out-Null

# ---- copy the toolchain ----
Write-Output "[3/4] Copying IDE + compiler + stdlib ..."
Copy-Item $ideExe  (Join-Path $dist 'ide_demo.exe')
Copy-Item $zancExe (Join-Path $dist 'zanc.exe')
Copy-Item $stdlib  (Join-Path $dist 'stdlib') -Recurse

# Bundle the self-contained linker toolchain (ld + mingw runtime) that zanc
# looks for next to itself. Without it, a published zanc falls back to a
# system clang/gcc and fails to link (e.g. "cannot find -lgcc_eh").
$toolchain = Join-Path $root 'build\toolchain'
if (Test-Path (Join-Path $toolchain 'ld.exe')) {
    Copy-Item $toolchain (Join-Path $dist 'toolchain') -Recurse
} else {
    Write-Output "PUBLISH_WARN: build\toolchain missing; dist zanc will need a system LLVM/clang on PATH"
}

# ---- copy a small set of example programs, if present ----
$examples = Join-Path $root 'examples'
if (Test-Path $examples) {
    Copy-Item $examples (Join-Path $dist 'examples') -Recurse
}

# ---- release readme ----
Write-Output "[4/4] Writing README.txt ..."
$readme = @"
Zan IDE - self-contained release
================================

Contents
  ide_demo.exe   The Zan IDE.
  zanc.exe       The Zan compiler. The IDE locates it relative to its own
                 directory, so keep it next to ide_demo.exe.
  stdlib\        Standard library sources. zanc auto-includes the .zan files
                 it needs from here; keep this folder next to zanc.exe.
  toolchain\     Bundled linker (ld) + MinGW-w64 runtime. zanc uses this to
                 produce executables with no external toolchain; keep it next
                 to zanc.exe.
  examples\      Sample programs shown in the IDE's Examples pane (optional).

Requirement
  None for normal use: zanc links via the bundled toolchain\ folder, so no
  external LLVM/clang install is needed. (If toolchain\ is removed, zanc falls
  back to a system clang on PATH.)

Run
  Double-click ide_demo.exe (or run it from a terminal). Everything the IDE
  needs to build and run Zan programs ships in this folder.

Note
  This directory is produced by scripts\publish_ide.ps1. Do not hand-edit or
  drop unrelated files here -- re-run the publish script to refresh it.
"@
Set-Content -Path (Join-Path $dist 'README.txt') -Value $readme -Encoding UTF8

$n = (Get-ChildItem $dist -Recurse -File | Measure-Object).Count
Write-Output "PUBLISH_OK -> $dist ($n files)"

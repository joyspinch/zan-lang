# publish_ide.ps1 -- Assemble a self-contained, redistributable Zan IDE into
# the canonical release directory  d:\project\zan-lang\dist .
#
# The published tree is intentionally minimal: only the files an end user needs
# to run the IDE and compile/run Zan programs. Nothing else should be dropped
# into dist -- treat it as the single, clean release output.
#
#   dist\
#     ZanIDE.exe           the IDE
#     toolchain\          the Zan compiler + its self-contained linker bundle:
#                           zanc.exe, ld.exe, mingw\, linux-musl\ ...
#                         (the IDE finds zanc here; zanc finds its linker next
#                          to itself in this same folder)
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
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$dist = Join-Path $root 'dist'

if (-not $SkipBuild) {
    Write-Output "[1/4] Building the IDE (scripts\build_ide.ps1) ..."
    & (Join-Path $root 'scripts\build_ide.ps1')
    if ($LASTEXITCODE -ne 0) { Write-Output "PUBLISH_FAILED: build error"; exit 1 }
}

# ---- required inputs ----
$b       = Join-Path $root 'build'
$ideExe  = Join-Path $b 'ZanIDE.exe'
$zancExe = Join-Path $b 'zanc.exe'
$stdlib  = Join-Path $root 'stdlib'
foreach ($p in @($ideExe, $zancExe, $stdlib)) {
    if (-not (Test-Path $p)) { Write-Output "PUBLISH_FAILED: missing $p"; exit 1 }
}

# ---- clean + recreate dist (release output only) ----
Write-Output "[2/4] Preparing clean dist directory: $dist"
if (Test-Path $dist) {
    try { Remove-Item $dist -Recurse -Force -ErrorAction Stop }
    catch {
        # The dist folder itself is held by another process (e.g. an Explorer
        # window open on it, or a shell whose current directory is inside it),
        # so it can't be deleted. Clearing its contents and reusing the folder
        # works around the lock on the directory node.
        Get-ChildItem -Path $dist -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (-not (Test-Path $dist)) { New-Item -ItemType Directory -Path $dist | Out-Null }

# ---- copy the toolchain ----
Write-Output "[3/4] Copying IDE + compiler + stdlib ..."
$distTc = Join-Path $dist 'toolchain'
New-Item -ItemType Directory -Path $distTc | Out-Null
Copy-Item $ideExe (Join-Path $dist 'ZanIDE.exe')
Copy-Item $stdlib (Join-Path $dist 'stdlib') -Recurse

# Everything the compiler needs travels together in dist\toolchain, laid out
# exactly as next to zanc in the build tree: the IDE resolves zanc here, and
# zanc finds its linker / sysroot / runtime objects as its own siblings (no
# nested toolchain\toolchain). Companion CLIs (zan-lsp, zan-dap) live here too.
#   compiler + CLIs : zanc.exe, zan-lsp.exe, zan-dap.exe,
#                     zanpkg.exe, zanfmt.exe, zandoc.exe
#   linker bundle   : ld.exe, mingw\            (Windows self-contained linking)
#   cross sysroot   : linux-musl\               (--target linux-* static ELF)
#   runtime objects : zanrt_io*, zanrt_sync*    (socket-async / sync runtimes)
Copy-Item $zancExe (Join-Path $distTc 'zanc.exe')
foreach ($cli in @('zan-lsp.exe', 'zan-dap.exe',
                   'zanpkg.exe', 'zanfmt.exe', 'zandoc.exe')) {
    $p = Join-Path $b $cli
    if (Test-Path $p) { Copy-Item $p (Join-Path $distTc $cli) }
    else { Write-Output "PUBLISH_WARN: missing build\$cli (skipped)" }
}
$ld = Join-Path $b 'ld.exe'
if (Test-Path $ld) {
    Copy-Item $ld (Join-Path $distTc 'ld.exe')
    Copy-Item (Join-Path $b 'mingw') (Join-Path $distTc 'mingw') -Recurse
} else {
    Write-Output "PUBLISH_WARN: build\ld.exe missing; dist zanc will need a system LLVM/clang on PATH"
}
$musl = Join-Path $b 'linux-musl'
if (Test-Path $musl) { Copy-Item $musl (Join-Path $distTc 'linux-musl') -Recurse }
foreach ($rt in (Get-ChildItem $b -File -ErrorAction SilentlyContinue |
                 Where-Object { $_.Name -like 'zanrt_*' -and $_.Extension -in '.o','.obj' })) {
    Copy-Item $rt.FullName (Join-Path $distTc $rt.Name)
}

# ---- native GUI runtime (linked when the IDE builds/runs GUI projects) ----
# Without this, user GUI/window projects fail to link (undefined zan_gui_* /
# sprintf). The IDE passes -L<toolchain> -lzan_gui when a project is type=gui.
$guiLib = Join-Path $b 'zan_gui.lib'
if (Test-Path $guiLib) { Copy-Item $guiLib (Join-Path $distTc 'zan_gui.lib') }
else { Write-Output "PUBLISH_WARN: build\zan_gui.lib missing; GUI projects will not link" }

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
  ZanIDE.exe     The Zan IDE.
  toolchain\     The Zan compiler and everything it links with, all as siblings:
                   zanc.exe                the compiler
                   zan-lsp.exe             language server (for external editors)
                   zan-dap.exe             debug adapter (for external editors)
                   zanpkg/zanfmt/zandoc    package / format / doc CLIs
                   ld.exe, mingw\          bundled linker + MinGW-w64 runtime
                   linux-musl\             sysroot for --target linux-* builds
                   zanrt_io*, zanrt_sync*  runtime objects
                 The IDE locates zanc here, and zanc finds its linker / sysroot
                 next to itself in this same folder, so producing an .exe needs
                 no external toolchain. Keep this folder intact.
  stdlib\        Standard library sources. zanc auto-includes the .zan files
                 it needs from here; keep this folder next to ZanIDE.exe.
  examples\      Sample programs shown in the IDE's Examples pane (optional).

Requirement
  None for normal use: zanc links via the bundled toolchain\ folder, so no
  external LLVM/clang install is needed. (If the linker files under toolchain\
  are removed, zanc falls back to a system clang on PATH.)

Run
  Double-click ZanIDE.exe (or run it from a terminal). Everything the IDE
  needs to build and run Zan programs ships in this folder.

Note
  This directory is produced by scripts\publish_ide.ps1. Do not hand-edit or
  drop unrelated files here -- re-run the publish script to refresh it.
"@
Set-Content -Path (Join-Path $dist 'README.txt') -Value $readme -Encoding UTF8

$n = (Get-ChildItem $dist -Recurse -File | Measure-Object).Count
Write-Output "PUBLISH_OK -> $dist ($n files)"

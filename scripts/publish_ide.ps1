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
#     templates\          built-in New Project templates (data-driven, editable)
#     README.txt          prerequisites + how to run
#
# No prerequisite on the target machine: zanc links via the bundled linker in
# dist\toolchain (ld.exe + mingw\ runtime), and zan-dap debugs with the bundled
# gdb. A system clang on PATH is only a fallback if the bundled linker files
# are removed.
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
# The IDE window is an SDL3 window (built with ZAN_GUI_SDL), so SDL3.dll must
# ship beside ZanIDE.exe or the published IDE fails to start.
$sdlDll = Join-Path $b 'SDL3.dll'
if (Test-Path $sdlDll) { Copy-Item $sdlDll (Join-Path $dist 'SDL3.dll') }
else { Write-Output "PUBLISH_FAILED: build\SDL3.dll missing (IDE needs it to run)"; exit 1 }
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
foreach ($sub in @('linux-musl', 'linux-arm64', 'win-x64', 'win-arm64', 'wasm32', 'riscv64', 'macos')) {
    $sys = Join-Path $b $sub
    if (Test-Path $sys) { Copy-Item $sys (Join-Path $distTc $sub) -Recurse }
}
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

# ---- bundle the native debugger (gdb) so debugging is out-of-the-box ----
# zan-dap resolves gdb next to itself first (toolchain\debugger\bin\gdb.exe),
# so a published IDE debugs with no system install. Source it from ZAN_GDB_DIR
# or a TDM-GCC install; copy gdb.exe plus the DLLs it needs to run.
$gdbSrcDir = $env:ZAN_GDB_DIR
if (-not $gdbSrcDir -or -not (Test-Path (Join-Path $gdbSrcDir 'gdb.exe'))) {
    $gdbSrcDir = 'C:\TDM-GCC-64\bin'
}
$gdbExe = Join-Path $gdbSrcDir 'gdb.exe'
if (Test-Path $gdbExe) {
    $dbgBin = Join-Path $distTc 'debugger\bin'
    New-Item -ItemType Directory -Path $dbgBin -Force | Out-Null
    Copy-Item $gdbExe (Join-Path $dbgBin 'gdb.exe')
    # gdb.exe links against the MinGW runtime DLLs shipped in the same bin dir;
    # copy them so the bundled debugger runs on a machine with no TDM-GCC.
    foreach ($dll in (Get-ChildItem $gdbSrcDir -Filter '*.dll' -File -ErrorAction SilentlyContinue)) {
        Copy-Item $dll.FullName (Join-Path $dbgBin $dll.Name) -Force
    }
    # Verify the relocated gdb actually runs. Some distributions (e.g. TDM-GCC)
    # ship a launcher stub that bakes in its install path and cannot be moved;
    # shipping that would give a broken debugger. If the copy fails to run,
    # drop the bundle so zan-dap falls back to a system/known gdb instead.
    $bundledGdb = Join-Path $dbgBin 'gdb.exe'
    $gdbOk = $false
    try { & $bundledGdb --version *> $null; $gdbOk = ($LASTEXITCODE -eq 0) } catch { $gdbOk = $false }
    if ($gdbOk) {
        Write-Output "Bundled gdb from $gdbSrcDir -> toolchain\debugger\bin"
    } else {
        Remove-Item (Join-Path $distTc 'debugger') -Recurse -Force -ErrorAction SilentlyContinue
        Write-Output "PUBLISH_WARN: gdb at $gdbSrcDir is not relocatable; not bundling (zan-dap will use a system/known gdb)"
    }
} else {
    Write-Output "PUBLISH_WARN: gdb.exe not found (set ZAN_GDB_DIR); debugging will need a system gdb/ZAN_GDB"
}

# ---- copy a small set of example programs, if present ----
$examples = Join-Path $root 'examples'
if (Test-Path $examples) {
    Copy-Item $examples (Join-Path $dist 'examples') -Recurse
}

# ---- copy the built-in project templates (data-driven; read at runtime) ----
# The IDE scans <ExeDir>\templates for template.manifest folders, so editing or
# adding a template needs no rebuild. Keep this folder next to ZanIDE.exe.
$templates = Join-Path $root 'templates'
if (Test-Path $templates) {
    Copy-Item $templates (Join-Path $dist 'templates') -Recurse
} else {
    Write-Output "PUBLISH_WARN: templates\ missing; New Project will use the built-in fallback set"
}

# ---- release readme ----
Write-Output "[4/4] Writing README.txt ..."
$readme = @"
Zan IDE - self-contained release
================================

Contents
  ZanIDE.exe     The Zan IDE.
  SDL3.dll       SDL3 runtime the IDE window uses; keep it next to ZanIDE.exe.
  toolchain\     The Zan compiler and everything it links with, all as siblings:
                   zanc.exe                the compiler
                   zan-lsp.exe             language server (for external editors)
                   zan-dap.exe             debug adapter (for external editors)
                   zanpkg/zanfmt/zandoc    package / format / doc CLIs
                   ld.exe, mingw\          bundled linker + MinGW-w64 runtime
                   linux-musl\             sysroot for --target linux-* builds
                   debugger\bin\gdb.exe    bundled native debugger (used by
                                           zan-dap; no system gdb needed)
                   zanrt_io*, zanrt_sync*  runtime objects
                 The IDE locates zanc here, and zanc finds its linker / sysroot
                 next to itself in this same folder, so producing an .exe needs
                 no external toolchain. Keep this folder intact.
  stdlib\        Standard library sources. zanc auto-includes the .zan files
                 it needs from here; keep this folder next to ZanIDE.exe.
  examples\      Sample programs shown in the IDE's Examples pane (optional).
  templates\     Built-in New Project templates (one folder each, with a
                 template.manifest). Edit or drop in your own folders to add
                 templates -- no rebuild needed.

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

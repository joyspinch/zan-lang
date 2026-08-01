# publish_ide.ps1 -- Assemble a self-contained, redistributable Zan IDE into
# the per-platform release directory  dist\win-x64  (other platforms get their
# own dist\<platform> when their builds exist, e.g. dist\linux-x64).
#
# The published tree is intentionally minimal: only the files an end user needs
# to run the IDE and compile/run Zan programs. Nothing else should be dropped
# into dist -- treat it as the single, clean release output.
#
# ZanIDE.exe in the release is a self-extracting wrapper (scripts\pkg_stub.c)
# carrying only the real IDE exe + SDL3.dll as its zip payload: no loose
# SDL3.dll ships, everything else stays as normal folders next to the exe. On
# first run the wrapper extracts to %LOCALAPPDATA%\ZanGames\ZanIDE\, exports
# ZAN_APP_DIR=<wrapper's folder> and starts the real IDE, which resolves
# toolchain\ / stdlib\ / examples\ / templates\ / tools\ via ZAN_APP_DIR.
#
#   dist\win-x64\
#     ZanIDE.exe           self-extracting wrapper (real IDE + SDL3.dll inside)
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

$dist = Join-Path $root 'dist\win-x64'

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
    # (dist\ itself may hold other platform folders; only win-x64 is rebuilt)
    try { Remove-Item $dist -Recurse -Force -ErrorAction Stop }
    catch {
        # The dist folder itself is held by another process (e.g. an Explorer
        # window open on it, or a shell whose current directory is inside it),
        # so it can't be deleted. Clearing its contents and reusing the folder
        # works around the lock on the directory node.
        Get-ChildItem -Path $dist -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (-not (Test-Path $dist)) { New-Item -ItemType Directory -Path $dist -Force | Out-Null }

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

# ---- the IDE's own page-layout stylesheet, beside the install root ----
# ide.css (flex/widths/gaps) is resolved from BaseDir()/ExeDir(); ship it in the
# install root so it loads without falling back to the source tree.
$ideCss = Join-Path $root 'src\ide_zan\ide.css'
if (Test-Path $ideCss) { Copy-Item $ideCss (Join-Path $dist 'ide.css') -Force }
else { Write-Output "PUBLISH_WARN: src\ide_zan\ide.css missing (IDE uses default layout)" }

# ---- skin packs are baked into ZanIDE.exe as embedded resources -----------
# (scripts\gen_embed.ps1, linked in by build_ide.ps1). The skin picker reads
# them from the exe in memory, so no external skins\ folder ships at the root.
# The filesystem is only a fallback for user overrides (an on-disk skins\ or
# stdlib\Gui\skins still wins if present).

# ---- copy the app icon so the IDE can stamp new projects with it ----
# zanc also carries a compiled-in copy (see CMakeLists.txt), so a produced .exe
# has an icon even when this file is missing.
$assetsIco = Join-Path $root 'assets\zan.ico'
if (Test-Path $assetsIco) {
    $distAssets = Join-Path $dist 'assets'
    New-Item -ItemType Directory -Force -Path $distAssets | Out-Null
    Copy-Item $assetsIco (Join-Path $distAssets 'zan.ico')
} else {
    Write-Output "PUBLISH_WARN: assets\zan.ico missing (new projects get no icon file)"
}

# Everything the compiler needs travels together in dist\toolchain, laid out
# exactly as next to zanc in the build tree: the IDE resolves zanc here, and
# zanc finds its linker / sysroot / runtime objects as its own siblings (no
# nested toolchain\toolchain). Companion CLIs (zan-lsp, zan-dap) live here too.
#   compiler + CLIs : zanc.exe, zan-lsp.exe, zan-dap.exe,
#                     zanfmt.exe, zandoc.exe
#   linker bundle   : ld.exe, mingw\            (Windows self-contained linking)
#   cross sysroot   : linux-musl\               (--target linux-* static ELF)
#   runtime objects : zanrt_io*, zanrt_sync*    (socket-async / sync runtimes)
Copy-Item $zancExe (Join-Path $distTc 'zanc.exe')
foreach ($cli in @('zan-lsp.exe', 'zan-dap.exe',
                   'zanfmt.exe', 'zandoc.exe')) {
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

# ---- copy the shipped IDE tools (Tools panel; run on double-click) ----
# The IDE scans <ExeDir>\tools for .zan tools, so this folder must travel
# next to ZanIDE.exe in the published tree.
$toolsDir = Join-Path $root 'tools'
if (Test-Path $toolsDir) {
    Copy-Item $toolsDir (Join-Path $dist 'tools') -Recurse
} else {
    Write-Output "PUBLISH_WARN: tools\ missing; the Tools panel will be empty"
}

# ---- release readme ----
Write-Output "[4/4] Writing README.txt ..."
$readme = @"
Zan IDE - self-contained release
================================

Contents
  ZanIDE.exe     The Zan IDE. It runs from this folder; SDL3.dll ships beside
                 it and skins/ide.css/stdlib/toolchain resolve from here.
  SDL3.dll       The windowing runtime the IDE needs; keep it next to the exe.
  ide.css        The IDE's page-layout stylesheet.
  (skins)        Skin packs are baked into ZanIDE.exe (embedded resources) and
                 read from memory -- no skins\ folder is required. Drop a
                 skins\ folder next to the exe only to override/add packs.
  toolchain\     The Zan compiler and everything it links with, all as siblings:
                   zanc.exe                the compiler
                   zan-lsp.exe             language server (for external editors)
                   zan-dap.exe             debug adapter (for external editors)
                   zanfmt/zandoc           format / doc CLIs
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
  tools\         Zan tools shown in the IDE's Tools panel. Double-click a tool
                 to run it; right-click to open its source in the editor.
                 Drop your own .zan tools here -- no rebuild needed.

Requirement
  None for normal use: zanc links via the bundled toolchain\ folder, so no
  external LLVM/clang install is needed. (If the linker files under toolchain\
  are removed, zanc falls back to a system clang on PATH.)

Run
  Double-click ZanIDE.exe (or run it from a terminal). Everything the IDE
  needs to build and run Zan programs ships in this folder.

Note
  This folder is produced by scripts\publish_ide.ps1. Do not hand-edit or
  drop unrelated files here -- re-run the publish script to refresh it.
"@
Set-Content -Path (Join-Path $dist 'README.txt') -Value $readme -Encoding UTF8

$n = (Get-ChildItem $dist -Recurse -File | Measure-Object).Count
Write-Output "STAGE_OK -> $dist ($n files)"

# ---- flat layout: the real IDE exe runs from the install dir ---------------
# No self-extract wrapper. ZanIDE.exe (the real IDE) and SDL3.dll ship loose in
# $dist and the exe runs right here, so skins/ide.css/stdlib/toolchain resolve
# from the exe's own directory -- no %LOCALAPPDATA% extraction, no ZAN_APP_DIR
# indirection, no "resources next to the extracted copy are missing" bugs.
Write-Output "[5/5] Finalizing flat layout (real exe + SDL3.dll beside it) ..."

# Still ship the single-file packer into the toolchain so the published IDE can
# wrap USER programs into single-file exes (its Publish flow calls
# toolchain\pack_single.ps1 with toolchain\pkg_stub.exe). This is independent of
# how the IDE itself ships; skip gracefully if no C compiler is available.
$gcc = 'C:\TDM-GCC-64\bin\gcc.exe'
if (-not (Test-Path $gcc)) {
    $cc = Get-Command gcc -ErrorAction SilentlyContinue
    if ($cc) { $gcc = $cc.Source } else { $gcc = $null }
}
if ($gcc) {
    $stub = Join-Path $b 'pkg_stub.exe'
    & $gcc -O2 -mwindows (Join-Path $root 'scripts\pkg_stub.c') -o $stub
    if ($LASTEXITCODE -eq 0 -and (Test-Path $stub)) {
        Copy-Item $stub (Join-Path $distTc 'pkg_stub.exe') -Force
        Copy-Item (Join-Path $root 'scripts\pack_single.ps1') (Join-Path $distTc 'pack_single.ps1') -Force
    } else {
        Write-Output "PUBLISH_WARN: launcher stub build failed; user single-file publish unavailable"
    }
} else {
    Write-Output "PUBLISH_WARN: gcc not found; user single-file publish unavailable (IDE itself ships flat, unaffected)"
}

if (-not (Test-Path (Join-Path $dist 'ZanIDE.exe'))) { Write-Output "PUBLISH_FAILED: dist\ZanIDE.exe missing"; exit 1 }
if (-not (Test-Path (Join-Path $dist 'SDL3.dll')))   { Write-Output "PUBLISH_FAILED: dist\SDL3.dll missing"; exit 1 }
$sz = (Get-Item (Join-Path $dist 'ZanIDE.exe')).Length
Write-Output "PUBLISH_OK -> $dist\ZanIDE.exe (flat, real exe, $sz bytes; SDL3.dll beside it)"

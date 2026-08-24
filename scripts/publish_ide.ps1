# publish_ide.ps1 -- Assemble a self-contained, redistributable Zan IDE into
# the per-platform release directory  dist\win-x64  (other platforms get their
# own dist\<platform> when their builds exist, e.g. dist\linux-x64).
#
# The published tree is intentionally minimal: only the files an end user needs
# to run the IDE and compile/run Zan programs. Nothing else should be dropped
# into dist -- treat it as the single, clean release output.
#
# ZanIDE.exe is the real IDE, shipped flat: SDL3 and the skin packs are
# statically linked / embedded inside the exe, so it has no dll beside it.
#
#   dist\win-x64\
#     ZanIDE.exe           the IDE (SDL3 statically linked, skins embedded)
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

param([switch]$SkipBuild, [switch]$NoBump)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$dist = Join-Path $root 'dist\win-x64'

# Every publish is a release, so it gets its own version: the patch component is
# raised before the build that stamps it into the binaries. -NoBump republishes
# the current version (e.g. re-packaging after a failed run), and a release that
# raises major/minor runs scripts\bump_version.ps1 -Part first.
if (-not $NoBump) {
    & (Join-Path $root 'scripts\bump_version.ps1')
    if ($LASTEXITCODE -ne 0) { Write-Output "PUBLISH_FAILED: version bump"; exit 1 }
}
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
Write-Output "Publishing version $version"

if (-not $SkipBuild) {
    Write-Output "[1/6] Building the IDE (scripts\build_ide.ps1) ..."
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
Write-Output "[2/6] Preparing clean dist directory: $dist"
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
Write-Output "[3/6] Copying IDE + compiler + stdlib ..."
$distTc = Join-Path $dist 'toolchain'
New-Item -ItemType Directory -Path $distTc | Out-Null
Copy-Item $ideExe (Join-Path $dist 'ZanIDE.exe')
# The dev build carries DWARF line tables (build_ide.ps1 passes -g) so
# build\zan_crash.log can name source lines. An end user has no source tree to
# resolve them against and they are ~1.6 MB of the image, so strip the
# published copy; build\ZanIDE.exe keeps its debug info.
$distIde = Join-Path $dist 'ZanIDE.exe'
$stripped = $false
foreach ($stripTool in @('llvm-strip', 'strip')) {
    if (Get-Command $stripTool -ErrorAction SilentlyContinue) {
        & $stripTool $distIde
        if ($LASTEXITCODE -eq 0) { $stripped = $true; break }
    }
}
if ($stripped) {
    Write-Output ("Stripped ZanIDE.exe -> {0:N1} MB" -f ((Get-Item $distIde).Length / 1MB))
} else {
    Write-Output 'PUBLISH_WARN: no llvm-strip/strip on PATH; ZanIDE.exe ships with debug info'
}

# SDL3 is statically linked into ZanIDE.exe (build_ide.ps1), so no SDL3.dll
# ships beside the IDE.
Copy-Item $stdlib (Join-Path $dist 'stdlib') -Recurse

# ---- the IDE's own page-layout stylesheet is baked into the exe ------------
# ide.css travels as an embedded resource (scripts\gen_embed.ps1 -Prefix ide,
# linked in by build_ide.ps1) and is read from memory, so nothing ships in the
# install root. A file next to the exe still wins, which is how a user overrides
# the layout without a rebuild -- we just do not create one.

# ---- native driver DLLs the IDE itself loads at run time -------------------
# ZanIDE.exe imports the driver DLLs of the stdlib namespaces it uses (TLS ->
# libssl/libcrypto, Sqlite -> libsqlite3). zanc stages them next to the exe it
# produces and reports each one, and build_ide.ps1 records that report in
# build\ZanIDE.deps.txt; ship exactly those files. Without them the published
# IDE cannot start on a machine that has no copy of them on PATH.
$depsList = Join-Path $b 'ZanIDE.deps.txt'
if (Test-Path $depsList) {
    foreach ($dep in (Get-Content $depsList | Where-Object { $_.Trim() -ne "" })) {
        $depSrc = Join-Path $b $dep.Trim()
        if (Test-Path $depSrc) { Copy-Item $depSrc (Join-Path $dist $dep.Trim()) -Force }
        else { Write-Output "PUBLISH_WARN: missing build\$dep (the IDE will not start without it)" }
    }
} else {
    Write-Output "PUBLISH_WARN: build\ZanIDE.deps.txt missing (run scripts\build_ide.ps1); driver DLLs the IDE needs may be absent from the release"
}

# The Help topics travel inside the exe as well (embed group 'docs'), so no
# topics.json ships here. A docs\topics.json next to the exe still wins (see
# DocsData.DiskPaths), which is how a user replaces the documentation without a
# new build -- we just do not create one.

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

# ---- knowledge base for the built-in assistant (offline API index + example
#      catalog) -------------------------------------------------------------
# The IDE's assistant reads <ExeDir>\knowledge\symbols.json (RepoMap index of
# the shipped stdlib) and <ExeDir>\knowledge\gallery.json (golden-example
# catalog) via its api_search / example tools, so it answers from a generated
# index instead of grepping the raw stdlib. Same data the server-side MCP
# serves. Non-fatal: the assistant falls back to stdlib_grep if this is absent.
$knowledgeDir = Join-Path $dist 'knowledge'
New-Item -ItemType Directory -Force -Path $knowledgeDir | Out-Null
$gallerySeed = Join-Path $root 'tools\mcp_server\gallery.json'
$zformDoc = Join-Path $root 'tools\mcp_server\zform.doc.json'
if (Test-Path $gallerySeed) {
    Copy-Item $gallerySeed (Join-Path $knowledgeDir 'gallery.seed.json') -Force
} else {
    Write-Output "PUBLISH_WARN: gallery seed missing; server-side refresh is unavailable"
}
if (Test-Path $zformDoc) {
    Copy-Item $zformDoc (Join-Path $knowledgeDir 'zform.doc.json') -Force
} else {
    Write-Output "PUBLISH_WARN: zform doc missing; server-side refresh is unavailable"
}
& (Join-Path $root 'scripts\gen_knowledge.ps1') `
    -Zanc $zancExe -Stdlib $stdlib -OutDir $knowledgeDir `
    -GallerySrc $gallerySeed -ZformDoc $zformDoc

# ---- AI onboarding pack (AGENTS.md + skills + stdio MCP + client configs) --
# Any external AI tool (Claude Code / Cursor / Copilot / Windsurf) can drive
# this SDK without reading its sources: it launches zan-mcp.exe --stdio and
# calls zan_start_here. The pack is what makes that a copy-one-file step, so it
# ships with the release. Non-fatal throughout: a missing piece degrades the
# assistance, it does not break the IDE.
Write-Output "[4/6] Building the AI onboarding pack ..."
$aiPack = Join-Path $root 'tools\ai_pack'
$mcpSrc = Join-Path $root 'tools\mcp_server\mcp_server.zan'
$distTools = Join-Path $dist 'tools'
New-Item -ItemType Directory -Force -Path $distTools | Out-Null
$mcpExe = Join-Path $distTools 'zan-mcp.exe'
if (Test-Path $mcpSrc) {
    # Under tools\ so the install root stays clean: the server resolves
    # knowledge\, toolchain\zanc.exe and stdlib\ from the SDK root it finds one
    # level up (McpServer.ResolveSdkDir), so the tree stays relocatable.
    # zanc writes its optimization report to stderr, and under
    # $ErrorActionPreference = "Stop" PowerShell turns a native command's stderr
    # into a terminating error whichever way the streams are redirected. Launch
    # it as a process instead so its output only ever reaches the log files, and
    # judge the result by the exit code.
    $mcpLog = Join-Path $root '_scratch\publish_zan_mcp.log'
    New-Item -ItemType Directory -Force -Path (Split-Path $mcpLog) | Out-Null
    $mcpProc = Start-Process -FilePath $zancExe -Wait -PassThru -NoNewWindow `
        -ArgumentList @($mcpSrc, '--auto-stdlib', '--publish', '-o', $mcpExe) `
        -RedirectStandardOutput $mcpLog -RedirectStandardError "$mcpLog.err"
    if ($mcpProc.ExitCode -ne 0) {
        Write-Output "PUBLISH_WARN: zanc exited $($mcpProc.ExitCode) building zan-mcp.exe; see $mcpLog.err"
    }
    if (Test-Path $mcpExe) {
        Write-Output "PUBLISH_MCP_OK -> $mcpExe"
    } else {
        Write-Output "PUBLISH_WARN: could not build tools\zan-mcp.exe; AI clients must run tools\mcp_server\mcp_server.zan themselves"
    }
} else {
    Write-Output "PUBLISH_WARN: tools\mcp_server\mcp_server.zan missing; no MCP server published"
}

if (Test-Path (Join-Path $aiPack 'AGENTS.md')) {
    # The pack ships as a TEMPLATE under ai\, never as live config in the
    # install root: AGENTS.md / .mcp.json / .cursor\ / .vscode\ only mean
    # anything inside the project you are working on, and nobody develops inside
    # the SDK folder. "tools\zan-mcp.exe --init-agent <project>" (or the IDE
    # assistant's one-click button) copies it into a project and expands
    # <ZAN_MCP> / <ZAN_SDK> / <ZAN_PROJECT> there -- which is also why the
    # placeholders are left intact here.
    $distAi = Join-Path $dist 'ai'
    New-Item -ItemType Directory -Force -Path $distAi | Out-Null
    Copy-Item (Join-Path $aiPack 'AGENTS.md') (Join-Path $distAi 'AGENTS.md') -Force
    foreach ($cfg in @('mcp.json', 'cursor.mcp.json', 'vscode.mcp.json')) {
        $srcCfg = Join-Path $aiPack $cfg
        if (Test-Path $srcCfg) { Copy-Item $srcCfg (Join-Path $distAi $cfg) -Force }
    }
    $aiReadme = Join-Path $aiPack 'README.md'
    if (Test-Path $aiReadme) { Copy-Item $aiReadme (Join-Path $distAi 'README.md') -Force }
    $distSkills = Join-Path $distAi 'skills'
    New-Item -ItemType Directory -Force -Path $distSkills | Out-Null
    Get-ChildItem (Join-Path $aiPack 'skills') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $distSkills -Recurse -Force }

    $onboarding = Join-Path $root 'docs\AI_ONBOARDING.md'
    if (Test-Path $onboarding) {
        New-Item -ItemType Directory -Force -Path (Join-Path $dist 'docs') | Out-Null
        Copy-Item $onboarding (Join-Path $dist 'docs\AI_ONBOARDING.md') -Force
    }
    foreach ($doc in @('TOOLING.md', 'ai-assist.md')) {
        $p = Join-Path $root "docs\$doc"
        if (Test-Path $p) { Copy-Item $p (Join-Path $dist "docs\$doc") -Force }
    }
    Write-Output "PUBLISH_AIPACK_OK -> ai\ (AGENTS.md, skills\, mcp configs), docs\AI_ONBOARDING.md"
} else {
    Write-Output "PUBLISH_WARN: tools\ai_pack missing; released SDK has no AGENTS.md/skills for AI clients"
}

# ---- release readme ----
Write-Output "[5/6] Writing README.txt ..."
$readme = @"
Zan IDE - self-contained release
================================

This folder is the SDK installation. It is not a workspace: create your Zan
projects anywhere else and run the IDE from here.

Contents
  ZanIDE.exe     The Zan IDE. It runs from this folder; SDL3 is statically
                 linked in, and stdlib/toolchain resolve from here.
  (skins, ide.css, help topics)
                 Baked into ZanIDE.exe as embedded resources and read from
                 memory -- no files required beside the exe. A file of the same
                 name next to the exe (ide.css, skins\, docs\topics.json) still
                 wins, which is how you override one without a rebuild.
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
  knowledge\     Offline knowledge base for the built-in assistant:
                   symbols.json  API index generated from stdlib (api_search)
                   gallery.json  golden-example catalog (example tool)
                 The assistant queries these instead of grepping the stdlib.
  examples\      Sample programs shown in the IDE's Examples pane (optional).
  templates\     Built-in New Project templates (one folder each, with a
                 template.manifest). Edit or drop in your own folders to add
                 templates -- no rebuild needed.
  tools\         Zan tools shown in the IDE's Tools panel (double-click to run,
                 right-click to open the source), plus zan-mcp.exe -- the MCP
                 server for AI clients. See the AI section below.
  ai\            Templates installed into your own projects by
                 "tools\zan-mcp.exe --init-agent" (see below).

AI coding tools (Claude Code / Cursor / Copilot / Windsurf)
  Enable them per PROJECT, not here. Two ways, same implementation:

    In the IDE   Assistant panel -> "为当前项目启用 AI 接入" (one click on the
                 project you have open).
    Without it   tools\zan-mcp.exe --init-agent D:\path\to\your-project
                 (add --force to overwrite files that already exist)

  Either one writes into THAT project, with this SDK's paths filled in:
    AGENTS.md            the rules an AI agent must follow in Zan code
    .agents\skills\      zan-development / zan-debugging / zan-mcp workflows
    .mcp.json            Claude Code and most clients
    .cursor\mcp.json     Cursor
    .vscode\mcp.json     VS Code / Copilot
  Then open that project in your AI editor and it starts the server itself.
  Existing files are kept, not overwritten, and reported.

  tools\zan-mcp.exe
                 The MCP server behind all of it, run as
                   tools\zan-mcp.exe --stdio <your project dir>
                 It answers what a model would otherwise guess at:
                 zan_start_here (project layout, commands, rules, tools in one
                 call), zan_api_search (exact stdlib signatures), zan_example
                 (shipped, build-verified programs), zan_compile /
                 zan_build_project (structured diagnostics), plus workspace
                 file access. Add --read-only or --no-exec to restrict it;
                 without --stdio it serves the same API over HTTP. It finds
                 knowledge\, toolchain\ and stdlib\ one level up on its own
                 (--sdk-root <dir> overrides).
  ai\            The templates the two commands above install (AGENTS.md,
                 skills\, mcp.json / cursor.mcp.json / vscode.mcp.json). They
                 still carry <ZAN_MCP> / <ZAN_SDK> / <ZAN_PROJECT> placeholders,
                 so use --init-agent rather than copying them by hand.
  docs\AI_ONBOARDING.md
                 The full connection guide (also covers zan-lsp / zan-dap for
                 editors that speak LSP/DAP directly).

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
# No self-extract wrapper. ZanIDE.exe (the real IDE) ships loose in $dist and
# runs right here, so ide.css/stdlib/toolchain resolve from the exe's own
# directory -- no %LOCALAPPDATA% extraction, no ZAN_APP_DIR indirection.
Write-Output "[6/6] Finalizing flat layout (real exe, no dlls) ..."

# No self-extract launcher ships any more, for the IDE or for user programs:
# a single-file publish bakes the resources into the exe with zanc --embed and
# links the native drivers statically, so nothing is ever unpacked into a cache
# directory and run from there. Drop a stub left by an older publish.
Remove-Item (Join-Path $distTc 'pkg_stub.exe') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $distTc 'pack_single.ps1') -Force -ErrorAction SilentlyContinue

if (-not (Test-Path (Join-Path $dist 'ZanIDE.exe'))) { Write-Output "PUBLISH_FAILED: dist\ZanIDE.exe missing"; exit 1 }
$sz = (Get-Item (Join-Path $dist 'ZanIDE.exe')).Length
Write-Output "PUBLISH_OK v$version -> $dist\ZanIDE.exe (flat, real exe, $sz bytes; SDL3 statically linked)"

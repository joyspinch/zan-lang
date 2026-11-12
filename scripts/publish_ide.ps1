# publish_ide.ps1 -- Assemble a self-contained, redistributable Zan IDE into
# the per-platform release directory  dist\win-x64  (other platforms get their
# own dist\<platform> when their builds exist, e.g. dist\linux-x64).
#
# The published tree is intentionally minimal: only the files an end user needs
# to run the IDE and compile/run Zan programs. Nothing else should be dropped
# into dist -- treat it as the single, clean release output.
#
# ZanIDE.exe is the real IDE, shipped flat: the skin packs are embedded
# inside the exe, so it has no dll beside it.
#
#   dist\win-x64\
#     ZanIDE.exe           the IDE (skins embedded)
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
#         -NoBump is required: select VERSION before the observed build.
#         Add  -OutDir <d> to publish directly to custom path (e.g. D:\Zan).

param([switch]$SkipBuild, [switch]$NoBump, [string]$OutDir,
      [switch]$PrepareOnly, [switch]$LocalStage, [switch]$PreflightOnly, [switch]$PlanOnly, [switch]$SkipDesignGeneration)

if ($PrepareOnly -and ($SkipBuild -or -not $NoBump)) {
    throw 'PUBLISH_FAILED: PrepareOnly requires NoBump and cannot use SkipBuild'
}
if (($PreflightOnly -or $PlanOnly) -and (-not $SkipBuild -or -not $NoBump)) {
    throw 'PUBLISH_FAILED: PreflightOnly requires SkipBuild and NoBump'
}

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$dist = if ($OutDir -and $OutDir.Trim() -ne "") { $OutDir.Trim() } else { Join-Path $root 'dist\win-x64' }

# Packaging cannot mutate VERSION after observed inputs were captured. Version
# selection is an explicit step before capture, shared by every release platform.
if (-not $NoBump) { throw 'PUBLISH_FAILED: NoBump is required; choose VERSION before the observed build' }
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
Write-Output "Publishing version $version"

. (Join-Path $PSScriptRoot 'sdk_stage.ps1')
# All build-producing operations are confined to preparation, under the caller's
# shared gate. SkipBuild only consumes these outputs; it never invokes zanc.
if (-not $SkipBuild) {
    & (Join-Path $root 'scripts\build_ide.ps1') -SkipDesignGeneration:$SkipDesignGeneration
    if ($LASTEXITCODE -ne 0) { throw 'PUBLISH_FAILED: IDE build error' }
    $buildZanc = Join-Path $root 'build\zanc.exe'
    $builtKnowledge = Join-Path $root 'build\knowledge'
    foreach ($name in @('symbols.json', 'gallery.json', 'packages-catalog.json', 'examples-catalog.json')) {
        Remove-Item -LiteralPath (Join-Path $builtKnowledge $name) -Force -ErrorAction SilentlyContinue
    }
    & (Join-Path $root 'scripts\gen_knowledge.ps1') -Zanc $buildZanc `
        -Stdlib (Join-Path $root 'stdlib') -OutDir $builtKnowledge `
        -GallerySrc (Join-Path $root 'tools\mcp_server\gallery.json')
    foreach ($name in @('symbols.json', 'gallery.json', 'packages-catalog.json', 'examples-catalog.json')) {
        Assert-SdkFile (Join-Path $builtKnowledge $name)
    }
    $mcpSources = @(Get-ChildItem -LiteralPath (Join-Path $root 'tools\mcp_server') -Filter '*.zan' -File | Select-Object -ExpandProperty FullName)
    $mcpSources += @('gallery\GalleryIndex.zan', 'gallery\GalleryResult.zan', 'semantic_index\SemanticIndex.zan', 'semantic_index\IndexedField.zan') |
        ForEach-Object { Join-Path (Join-Path $root 'tools\shared') $_ }
    foreach ($source in $mcpSources) { Assert-SdkFile $source }
    $mcpLog = Join-Path $root 'build\test-out\prepare-zan-mcp.log'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $mcpLog) | Out-Null
    $mcpArgs = @($mcpSources) + @('--auto-stdlib', '--publish', '-o', (Join-Path $root 'build\zan-mcp.exe'))
    $quotedArgs = @($mcpArgs | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    Remove-Item -LiteralPath (Join-Path $root 'build\zan-mcp.exe') -Force -ErrorAction SilentlyContinue
    $proc = Start-Process -FilePath $buildZanc -Wait -PassThru -NoNewWindow -ArgumentList $quotedArgs `
        -RedirectStandardOutput $mcpLog -RedirectStandardError "$mcpLog.err"
    if ($proc.ExitCode -ne 0) { throw "PUBLISH_FAILED: MCP build error ($($proc.ExitCode)); see $mcpLog.err" }
    Assert-SdkFile (Join-Path $root 'build\zan-mcp.exe')
    if ($PrepareOnly) { Write-Output 'PUBLISH_PREPARE_OK (IDE, MCP, knowledge; no dist mutation)'; return }
}

# ---- complete preflight, before deleting any old output ----
$b = Join-Path $root 'build'
$dist = Assert-SdkStagePath $dist $root @((Join-Path $root 'dist'), (Join-Path $root '_scratch'))
$distTc = Join-Path $dist 'toolchain'
$work = Join-Path $root 'build\test-out'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$toolInputs = Join-Path $work 'sdk-toolchain-inputs.json'
$planFile = Join-Path $work 'sdk-stage-plan.json'
$ticket = Join-Path $work 'sdk-stage-preflight.json'
& (Join-Path $PSScriptRoot 'stage_dev_toolchain.ps1') -Build $b -Dest $distTc `
    -IncludeDebugger -PreflightOnly -InputList $toolInputs
$copies = [Collections.Generic.List[object]]::new()
function Add-SdkCopy([string]$Source, [string]$Destination, [switch]$Tree) {
    $kind = if ($Tree) { 'Container' } else { 'Leaf' }
    if (-not (Test-Path -LiteralPath $Source -PathType $kind)) { throw "PUBLISH_FAILED: required input missing: $Source" }
    $copies.Add([pscustomobject]@{ source = [IO.Path]::GetFullPath($Source); destination = $Destination; tree = [bool]$Tree; source_only = ($Tree -and $Destination -notin @('stdlib', 'packages')) })
}
foreach ($tree in @('stdlib', 'packages', 'examples', 'templates', 'tools')) {
    Add-SdkCopy (Join-Path $root $tree) $tree -Tree
}
Assert-SdkFile (Join-Path $root 'examples\html_gallery\zan.proj')
Add-SdkCopy (Join-Path $b 'ZanIDE.exe') 'ZanIDE.exe'
Add-SdkCopy (Join-Path $b 'ZanIDE.deps.txt') 'ZanIDE.deps.txt'
foreach ($dep in (Get-Content -LiteralPath (Join-Path $b 'ZanIDE.deps.txt'))) {
    $dep = $dep.Trim()
    if (-not $dep) { continue }
    if ([IO.Path]::GetFileName($dep) -ne $dep -or $dep -notmatch '\.dll$') { throw "PUBLISH_FAILED: unsafe IDE dependency: $dep" }
    Add-SdkCopy (Join-Path $b $dep) $dep
}
Add-SdkCopy (Join-Path $b 'zan-mcp.exe') 'tools/zan-mcp.exe'
foreach ($name in @('symbols.json', 'gallery.json', 'packages-catalog.json', 'examples-catalog.json')) {
    Add-SdkCopy (Join-Path (Join-Path $b 'knowledge') $name) "knowledge/$name"
}
Add-SdkCopy (Join-Path $root 'tools\mcp_server\gallery.json') 'knowledge/gallery.seed.json'
Add-SdkCopy (Join-Path $root 'assets\zan.ico') 'assets/zan.ico'
$aiPack = Join-Path $root 'tools\ai_pack'
foreach ($name in @('AGENTS.md', 'mcp.json', 'cursor.mcp.json', 'vscode.mcp.json', 'README.md')) {
    Add-SdkCopy (Join-Path $aiPack $name) "ai/$name"
}
Add-SdkCopy (Join-Path $aiPack 'skills') 'ai/skills' -Tree
Add-SdkCopy (Join-Path $aiPack 'docs\AI_README.md') 'AI_README.md'
foreach ($name in @('AI_ONBOARDING.md', 'ai-assist.md', 'MCP_HOSTING.md', 'AI_DEV_INFRASTRUCTURE.md')) {
    Add-SdkCopy (Join-Path (Join-Path $aiPack 'docs') $name) "docs/$name"
}
Add-SdkCopy (Join-Path $root 'docs\TOOLING.md') 'docs/TOOLING.md'
$toolPlan = Get-Content -LiteralPath $toolInputs -Raw | ConvertFrom-Json
$plan = @{ repo = $root; toolchain = @($toolPlan | ForEach-Object { $_ }); copies = @($copies.ToArray()) }
ConvertTo-Json -InputObject $plan -Depth 8 | Set-Content -LiteralPath $planFile -Encoding UTF8
# Include exact gallery file references missing from the regular payload trees.
$manifestTool = Join-Path $PSScriptRoot 'sdk_stage_manifest.py'
& python -I -B $manifestTool gallery-plan --plan $planFile
if ($LASTEXITCODE -ne 0) { throw 'PUBLISH_FAILED: gallery payload closure validation failed' }
if ($PlanOnly) { Write-Output "PUBLISH_PLAN_OK -> $planFile (no identity approval, no output mutation)"; return }
$manifestArgs = @('--repo', $root, '--build', $b, '--plan', $planFile, '--ticket', $ticket)
if ($LocalStage) { $manifestArgs += '--local-stage' }
& python -I -B $manifestTool preflight @manifestArgs
if ($LASTEXITCODE -ne 0) { throw 'PUBLISH_FAILED: SDK identity/preflight validation failed; previous output preserved' }
if ($PreflightOnly) { Write-Output 'PUBLISH_PREFLIGHT_OK (previous output preserved)'; return }

# All source/identity failures above preserve an old output for inspection. An
# output that cannot be cleaned is an error, never a partly mixed release.
if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force -ErrorAction Stop }
New-Item -ItemType Directory -Path $dist -Force | Out-Null
& python -I -B $manifestTool copy-sources --plan $planFile --sdk $dist
if ($LASTEXITCODE -ne 0) { throw 'PUBLISH_FAILED: SDK source copy failed' }
& (Join-Path $PSScriptRoot 'stage_dev_toolchain.ps1') -Build $b -Dest $distTc -IncludeDebugger

# ---- release readme ---------------------------------------------------------
Write-Output "[5/6] Writing README.txt + AI entry index ..."
$readme = @"
Zan IDE - self-contained release
================================

This folder is the SDK installation. It is not a workspace: create your Zan
projects anywhere else and run the IDE from here.

Contents
  ZanIDE.exe     The Zan IDE. It runs from this folder; the GUI runtime is
                 statically
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
  packages\      Official extension packages ecosystem (Zan.Data, Zan.Gui.Charts, etc.)
                 installed into user projects by pkg_install or the AI assistant.
  knowledge\     Offline knowledge base for the built-in assistant:
                   symbols.json          API index generated from stdlib (api_search)
                   gallery.json          golden-example catalog (example tool)
                   packages-catalog.json Package catalog index (pkg_search/pkg_install)
                   examples-catalog.json Examples catalog index
                 The assistant queries these instead of grepping the stdlib.
  examples\      Sample programs shown in the IDE's Examples pane.
  templates\     Built-in New Project templates (one folder each, with a
                 template.manifest). Edit or drop in your own folders to add
                 templates -- no rebuild needed.
  tools\         Zan tools shown in the IDE's Tools panel (double-click to run,
                 right-click to open the source), plus zan-mcp.exe -- the MCP
                 server for AI clients. See the AI section below.
  ai\            Templates installed into your own projects by
                 "tools\zan-mcp.exe --init-agent" (see below).
  llms.txt       AI entry index of this folder (generated from what shipped):
                 the paths to the rules, tools, knowledge, skills and docs an
                 AI agent needs, readable without an MCP connection.
  AI_README.md   How AI uses this SDK (in Chinese): setup, the MCP tool
                 catalog, skills, the knowledge base. Start here when wiring
                 up an AI coding tool.
  README.CN.txt  Chinese version of this readme.

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

  Website mirror    The same ai\ content (rules + skills + mcp configs) is
                  mirrored on the project website at /ai-connect, generated
                  from this pack so they never drift. This SDK copy is the
                  authoritative one that --init-agent installs; the site copy
                  is for reading.

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

# ---- Chinese readme ----------------------------------------------------------
# Kept next to the English one ON PURPOSE: both describe the same folder, so a
# layout change edits both here in the same commit.
# NOTE: this script now contains Chinese string literals, so it must be saved
# as UTF-8 WITH BOM. PowerShell 5.1 reads a BOM-less script as ANSI, which
# would corrupt the text below straight into the published file.
$readmeCn = @"
Zan IDE 自包含发布包
====================

本目录是 SDK 安装目录，不是工作区：请把 Zan 项目建在别处，从这里运行 IDE。

目录内容
  ZanIDE.exe     Zan 集成开发环境。GUI 运行时已静态链接进 exe，皮肤/帮助主题为内嵌
                 资源从内存读取，旁边不需要任何 dll 或数据文件。在 exe 旁边放
                 同名文件（ide.css、skins\、docs\topics.json）仍会优先生效，
                 这就是不重新打包就替换它们的办法。
  toolchain\     Zan 编译器及其链接所需的一切，全部作为同目录的兄弟文件平铺：
                   zanc.exe                编译器
                   zan-lsp.exe             语言服务器（供外部编辑器）
                   zan-dap.exe             调试适配器（供外部编辑器）
                   zanfmt.exe / zandoc.exe 格式化 / 文档生成命令行
                   ld.exe, mingw\          内置链接器 + MinGW-w64 运行库
                   linux-musl\             --target linux-* 静态 ELF 的 sysroot
                   debugger\bin\gdb.exe    内置原生调试器（zan-dap 使用）
                   zanrt_io*, zanrt_sync*  运行时目标文件
                 IDE 在这里找 zanc，zanc 在自己旁边找链接器 / sysroot / 标准库，
                 生成 .exe 不依赖任何外部工具链。请保持本目录完整。
  stdlib\        标准库源码。zanc 自动按需引入；请与 ZanIDE.exe 放在一起。
  packages\      官方扩展包生态池（Zan.Data、Zan.Gui.Charts 等）。
                 由内置 AI 助手或 pkg_install 按需安装至具体项目。
  knowledge\     内置 AI 助手的离线知识库：
                   symbols.json          由随包 stdlib 生成的 API 索引（api_search 用）
                   gallery.json          金标示例目录（example 工具用）
                   packages-catalog.json 扩展包元数据索引（包查找与自动修复用）
                   examples-catalog.json 官方范例索引（功能参考用）
                 助手查这些索引，而不是去 grep 标准库源码。
  examples\      IDE 示例面板展示的示例程序。
  templates\     内置"新建项目"模板（每个模板一个目录，含 template.manifest）。
                 编辑或放入新目录即可增删模板——无需重新构建。
  tools\         IDE 工具面板展示的 Zan 工具（双击运行），以及 zan-mcp.exe——
                 AI 客户端的 MCP 服务器。见下方 AI 一节。
  ai\            由 "tools\zan-mcp.exe --init-agent" 安装进你自己项目的模板
                 （AGENTS.md、skills\、三个 mcp 配置），见下。
  llms.txt       本目录的 AI 索引（按实际发布内容生成）：AI 需要的规矩、工具、
                 知识库、技能、文档路径一览，没接 MCP 也能读。
  AI_README.md   怎么让 AI 用这个 SDK（中文）：接入方式、MCP 工具目录、技能、
                 知识库、常见问题。接 AI 工具前先读它。
  docs\          AI_ONBOARDING.md（AI 接入完整指南）、TOOLING.md、ai-assist.md、
                 MCP_HOSTING.md、AI_DEV_INFRASTRUCTURE.md。

AI 编程工具（Claude Code / Cursor / Copilot / Windsurf）
  按"项目"启用，而不是在这里启用。两种方式，同一实现：

    IDE 里    助手面板 ->「为当前项目启用 AI 接入」（对你打开的项目一键完成）
    命令行    tools\zan-mcp.exe --init-agent D:\path\to\your-project
              （加 --force 覆盖已存在的文件）

  两种方式都会把内容写进那个项目，并填好本 SDK 的真实路径：
    AGENTS.md            AI 在 Zan 代码里必须守的规矩
    .agents\skills\      zan-development / zan-debugging / zan-mcp 三个技能
    .mcp.json            Claude Code 及多数客户端
    .cursor\mcp.json     Cursor
    .vscode\mcp.json     VS Code / Copilot
  然后用你的 AI 编辑器打开那个项目，客户端会自己启动服务器。
  已存在的文件保留不覆盖，并逐个报告。

  tools\zan-mcp.exe
                 背后的 MCP 服务器，运行方式：
                   tools\zan-mcp.exe --stdio <你的项目目录>
                 它回答模型原本只能靠猜的问题：zan_start_here（项目布局、命令、
                 规矩、工具目录一次拿全）、zan_api_search（标准库精确签名）、
                 zan_example（可编译的官方示例）、zan_compile / zan_build_project
                 （结构化诊断），外加工作区文件读写。加 --read-only 或 --no-exec
                 收权；不带 --stdio 时以 HTTP 提供同一套 API。它会自己在上一级
                 找 knowledge\、toolchain\ 和 stdlib\（--sdk-root <目录> 可覆盖）。
  ai\            上面两条命令安装的模板。里面保留 <ZAN_MCP> / <ZAN_SDK> /
                 <ZAN_PROJECT> 占位符，所以请用 --init-agent，不要手工复制。
  docs\AI_ONBOARDING.md
                 完整接入指南（也覆盖 zan-lsp / zan-dap 直连 LSP/DAP 的编辑器）。

  官网镜像        ai\ 的同样内容（规矩 + 技能 + mcp 配置）镜像在项目官网
                 /ai-connect，由本包生成，两边不会漂移。本 SDK 里这份是权威
                 副本，--init-agent 安装的就是它；网站上那份供在线阅读。

环境要求
  正常使用为零依赖：zanc 通过内置 toolchain\ 里的链接器完成链接，不需要安装
  外部 LLVM/clang。（若删除 toolchain\ 下的链接器文件，zanc 回退到 PATH 上
  的系统 clang。）

运行
  双击 ZanIDE.exe（或在终端里运行）。构建和运行 Zan 程序所需的一切都在本目录。

注意
  本目录由 scripts\publish_ide.ps1 生成。不要手工编辑或往里放无关文件——
  重新运行发布脚本即可刷新。
"@
Set-Content -Path (Join-Path $dist 'README.CN.txt') -Value $readmeCn -Encoding UTF8

# Generated entry index is required; the manifest records its final bytes.
& (Join-Path $root 'scripts\gen_ai_index.ps1') -Dist $dist -Version $version
Assert-SdkFile (Join-Path $dist 'llms.txt')
& python -I -B $manifestTool seal @manifestArgs --sdk $dist
if ($LASTEXITCODE -ne 0) { throw 'PUBLISH_FAILED: final SDK input/output identity validation failed' }
& python -I -B $manifestTool verify --sdk $dist
if ($LASTEXITCODE -ne 0) { throw 'PUBLISH_FAILED: final SDK inventory verification failed' }
$n = (Get-ChildItem -LiteralPath $dist -Recurse -File | Measure-Object).Count
Write-Output "PUBLISH_OK v$version -> $dist ($n files; manifest sealed; template consumption pending)"

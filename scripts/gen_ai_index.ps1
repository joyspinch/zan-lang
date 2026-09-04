# gen_ai_index.ps1 -- Generate the AI entry index (llms.txt) for a published
# SDK tree. Fixed path at the SDK root, content regenerated from what the tree
# actually contains: the file-channel twin of zan-mcp's zan_start_here, so an
# agent with no MCP connection bootstraps from one file instead of crawling.
#
# Everything listed must already be staged in the dist tree; entries whose
# source is absent are skipped, so the index never promises a missing file.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\gen_ai_index.ps1
#           -Dist <published sdk dir> [-Version 0.2.4]

param(
    [Parameter(Mandatory = $true)][string]$Dist,
    [string]$Version = ""
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path $Dist)) { Write-Output "AI_INDEX_FAILED: no such dir: $Dist"; exit 1 }
if (-not $Version) {
    $versionFile = Join-Path $Dist 'VERSION'
    if (Test-Path $versionFile) { $Version = (Get-Content $versionFile -Raw).Trim() }
    else { $Version = 'unknown' }
}

function Test-Staged([string]$rel) { Test-Path (Join-Path $Dist $rel) }

# Pull the one-line `description:` out of a SKILL.md front-matter block.
function Get-SkillSummary([string]$skillMd) {
    foreach ($line in (Get-Content $skillMd -TotalCount 8 -Encoding UTF8)) {
        if ($line -match '^description:\s*(.+)$') { return $Matches[1].Trim() }
    }
    return ""
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# Zan SDK $Version")
$lines.Add("")
$lines.Add("> Self-contained Zan IDE + compiler SDK. Projects live ANYWHERE ELSE;")
$lines.Add("> point an AI client at a project, or read this file plus the paths")
$lines.Add("> below. Paths are relative to this folder (the SDK root).")
$lines.Add("")

# ---- start here -------------------------------------------------------------
$lines.Add("## Start")
$lines.Add("- [README.txt](README.txt): what this folder is, how to run the IDE")
if (Test-Staged 'AI_README.md') {
    $lines.Add("- [AI_README.md](AI_README.md): how AI uses this SDK (in Chinese) -- setup, MCP tool catalog, skills, knowledge base")
}
if (Test-Staged 'docs/AI_ONBOARDING.md') {
    $lines.Add("- [docs/AI_ONBOARDING.md](docs/AI_ONBOARDING.md): connect Claude Code / Cursor / Copilot / Windsurf (MCP config, zan-lsp, zan-dap)")
}
$lines.Add("")

# ---- agent rules ------------------------------------------------------------
$lines.Add("## Rules for AI agents")
if (Test-Staged 'ai/AGENTS.md') {
    $lines.Add("- [ai/AGENTS.md](ai/AGENTS.md): the rules --init-agent installs into a project (template; placeholders intact here)")
}
$lines.Add("- Call tools/zan-mcp.exe --stdio <project> and ask zan_start_here first: project layout, build/test commands, rules, tool catalog in one response")
$lines.Add("")

# ---- tools ------------------------------------------------------------------
$lines.Add("## Tools")
if (Test-Staged 'tools/zan-mcp.exe') {
    $lines.Add("- tools/zan-mcp.exe: MCP server (stdio or HTTP). zan_start_here / zan_api_search / zan_example / zan_compile / zan_build_project + workspace file access. Flags: --read-only, --no-exec, --sdk-root")
}
if (Test-Staged 'toolchain/zanc.exe') {
    $lines.Add("- toolchain/zanc.exe: the compiler. Compile a program: toolchain/zanc.exe <entry.zan> --auto-stdlib -o out.exe (add --publish for release)")
}
$cliDesc = @{
    'zan-lsp.exe'  = 'language server (LSP over stdio, for external editors)'
    'zan-dap.exe'  = 'debug adapter (DAP over stdio, for external editors)'
    'zanfmt.exe'   = 'source formatter'
    'zandoc.exe'   = 'documentation generator'
}
foreach ($cli in @('zan-lsp.exe', 'zan-dap.exe', 'zanfmt.exe', 'zandoc.exe')) {
    if (Test-Staged "toolchain/$cli") {
        $lines.Add("- toolchain/${cli}: " + $cliDesc[$cli])
    }
}
$lines.Add("")

# ---- generated knowledge ----------------------------------------------------
$know = @()
foreach ($k in @('symbols.json', 'gallery.json', 'gallery.seed.json', 'zform.doc.json')) {
    if (Test-Staged "knowledge/$k") { $know += "knowledge/$k" }
}
if ($know.Count -gt 0) {
    $lines.Add("## Knowledge (generated API index)")
    $lines.Add("- " + ($know -join ", ") + ": offline API index + example catalog behind zan_api_search / zan_example; generated from the shipped stdlib, not hand-written")
    $lines.Add("")
}

# ---- skills -----------------------------------------------------------------
$skillsDir = Join-Path $Dist 'ai/skills'
if (Test-Path $skillsDir) {
    $lines.Add("## Skills")
    Get-ChildItem $skillsDir -Directory | Sort-Object Name | ForEach-Object {
        $md = Join-Path $_.FullName 'SKILL.md'
        if (Test-Path $md) {
            $summary = Get-SkillSummary $md
            if ($summary) { $lines.Add("- [ai/skills/$($_.Name)/SKILL.md](ai/skills/$($_.Name)/SKILL.md): $summary") }
            else { $lines.Add("- [ai/skills/$($_.Name)/SKILL.md](ai/skills/$($_.Name)/SKILL.md)") }
        }
    }
    $lines.Add("")
}

# ---- docs -------------------------------------------------------------------
$docLines = @()
foreach ($d in @('AI_ONBOARDING.md', 'TOOLING.md', 'ai-assist.md', 'MCP_HOSTING.md', 'AI_DEV_INFRASTRUCTURE.md')) {
    if (Test-Staged "docs/$d") { $docLines += "docs/$d" }
}
if ($docLines.Count -gt 0) {
    $lines.Add("## Docs")
    foreach ($d in $docLines) { $lines.Add("- $d") }
    $lines.Add("")
}

# ---- project scaffolding ----------------------------------------------------
$tmpl = 0
if (Test-Staged 'templates') { $tmpl = (Get-ChildItem (Join-Path $Dist 'templates') -Directory | Measure-Object).Count }
if ($tmpl -gt 0) {
    $lines.Add("## Projects")
    $lines.Add("- templates/: $tmpl built-in New Project templates (template.manifest per folder)")
    if (Test-Staged 'examples') { $lines.Add("- examples/: build-verified sample programs (the zan_example tool serves these)") }
    $lines.Add("")
}

$out = Join-Path $Dist 'llms.txt'
[System.IO.File]::WriteAllLines($out, $lines)
Write-Output "AI_INDEX_OK -> $out ($(($lines | Measure-Object).Count) lines)"

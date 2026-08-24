# gen_knowledge.ps1 -- generate the built-in assistant's offline knowledge base:
#   <OutDir>\symbols.json   RepoMap API index built from the given stdlib
#   <OutDir>\gallery.json   golden-example catalog (merged from templates/examples)
#   <OutDir>\zform.json     .zform schema plus statically scanned controls
#
# The IDE's assistant (src\ide_zan\AiAgent.zan) reads these next to the IDE
# (<ExeDir>\knowledge, i.e. BaseDir()+"/knowledge") through its api_search /
# example tools, so it answers from a generated index instead of grepping the
# raw stdlib. This is the SAME data the server-side MCP serves, so the built-in
# assistant and external agents share one source of truth.
#
# Usage (called by scripts\build_ide.ps1 and scripts\publish_ide.ps1):
#   scripts\gen_knowledge.ps1 -Zanc <zanc.exe> -Stdlib <stdlib> -OutDir <knowledge>
param(
  [Parameter(Mandatory=$true)][string]$Zanc,
  [Parameter(Mandatory=$true)][string]$Stdlib,
  [Parameter(Mandatory=$true)][string]$OutDir,
  [string]$RepomapSrc,
  [string]$GallerySrc,
  [string]$ZformDoc,
  [switch]$WriteControls,
  [string]$WorkDir
)

# A compiler warning on stderr must not abort generation; only exit codes decide.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
if (-not $RepomapSrc) { $RepomapSrc = Join-Path $root 'tools\repomap\RepoMap.zan' }
if (-not $GallerySrc) { $GallerySrc = Join-Path $root 'tools\mcp_server\gallery.json' }
if (-not $ZformDoc) { $ZformDoc = Join-Path $root 'tools\mcp_server\zform.doc.json' }
$genKnowledgeSrc = Join-Path $root 'tools\genknowledge\GenKnowledge.zan'

# Everything past a Push-Location must be an absolute path.
$Zanc   = (Resolve-Path $Zanc).Path
$Stdlib = (Resolve-Path $Stdlib).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$toolDir = Split-Path -Parent $Zanc
if (-not $WorkDir) { $WorkDir = Join-Path $toolDir '_knowledge_work' }

# ---- API index (symbols.json) via RepoMap over the shipped stdlib ----------
if (Test-Path $RepomapSrc) {
  $repomapExe = Join-Path $toolDir 'repomap.exe'
  & $Zanc $RepomapSrc --auto-stdlib -o $repomapExe
  if ($LASTEXITCODE -eq 0 -and (Test-Path $repomapExe)) {
    # RepoMap writes <cwd>\.zanmap\symbols.json; run it against the shipped
    # stdlib from a scratch cwd, then lift the index into knowledge\.
    if (Test-Path $WorkDir) { Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue }
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
    Push-Location $WorkDir
    & $repomapExe $Stdlib | Out-Null
    Pop-Location
    $sym = Join-Path $WorkDir '.zanmap\symbols.json'
    if (Test-Path $sym) {
      Copy-Item $sym (Join-Path $OutDir 'symbols.json') -Force
      $n = (Get-Item (Join-Path $OutDir 'symbols.json')).Length
      Write-Output "KNOWLEDGE_INDEX_OK -> $OutDir\symbols.json ($n bytes)"
    } else {
      Write-Output "KNOWLEDGE_WARN: RepoMap produced no symbols.json (assistant api_search falls back to stdlib_grep)"
    }
    Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
  } else {
    Write-Output "KNOWLEDGE_WARN: repomap build failed (assistant api_search falls back to stdlib_grep)"
  }
} else {
  Write-Output "KNOWLEDGE_WARN: $RepomapSrc missing; no API index generated"
}

# ---- gallery and .zform schema through the cross-platform Zan tool --------
$controls = Join-Path $root 'tools\mcp_server\zform.controls.txt'
if ((Test-Path $genKnowledgeSrc) -and (Test-Path $GallerySrc) -and
    (Test-Path $ZformDoc) -and (Test-Path $Stdlib)) {
  $genKnowledgeExe = Join-Path $toolDir 'genknowledge.exe'
  try {
    & $Zanc $genKnowledgeSrc --stdlib-path $Stdlib -o $genKnowledgeExe
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $genKnowledgeExe)) {
      throw "GenKnowledge compile failed (exit code $LASTEXITCODE)"
    }
    $args = @('--root', $root, '--stdlib', $Stdlib, '--out', $OutDir,
      '--seed', $GallerySrc, '--doc', $ZformDoc, '--controls', $controls)
    if ($WriteControls) { $args += '--write-controls' }
    & $genKnowledgeExe @args
    if ($LASTEXITCODE -ne 0) {
      throw "GenKnowledge execution failed (exit code $LASTEXITCODE)"
    }
  } catch {
    Write-Output "KNOWLEDGE_WARN: GenKnowledge failed: $_"
  }
  if (-not (Test-Path (Join-Path $OutDir 'gallery.json'))) {
    Write-Output "KNOWLEDGE_WARN: gallery generation failed; assistant example tool will be empty"
  }
  if (-not (Test-Path (Join-Path $OutDir 'zform.json'))) {
    Write-Output "KNOWLEDGE_WARN: zform generation failed; form schema tool will be empty"
  }
} else {
  Write-Output "KNOWLEDGE_WARN: GenKnowledge inputs missing; gallery/form schema tools will be empty"
}

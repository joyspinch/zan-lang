<#
.SYNOPSIS
  Zan 游戏引擎素材与图集自动化处理工具
.EXAMPLE
  .\scripts\game_asset_tool.ps1 atlas 64 64 20
  .\scripts\game_asset_tool.ps1 mirror5 Warrior
  .\scripts\game_asset_tool.ps1 paperdoll Mage
#>
param(
    [Parameter(Position=0)]
    [string]$Command = "",
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$RemainingArgs
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Zanc = Join-Path $RepoRoot "build\zanc.exe"
$ToolSrc = Join-Path $RepoRoot "packages\Zan.Game\tools\game_asset_tool.zan"
$OutExe = Join-Path $RepoRoot "_scratch\game_asset_tool_cli.exe"

if (-not (Test-Path $Zanc)) {
    Write-Error "zanc.exe not found at $Zanc. Please build the compiler first."
}

if (-not (Test-Path (Split-Path $OutExe))) {
    New-Item -ItemType Directory -Path (Split-Path $OutExe) -Force | Out-Null
}

# 编译
$proc = Start-Process -FilePath $Zanc -ArgumentList @($ToolSrc, "--auto-stdlib", "-o", $OutExe) -NoNewWindow -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    throw "Failed to compile game_asset_tool.zan (exit code $($proc.ExitCode))"
}

if ([string]::IsNullOrEmpty($Command)) {
    & $OutExe
} else {
    & $OutExe $Command @RemainingArgs
}

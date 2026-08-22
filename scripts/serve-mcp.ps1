# Run zan-mcp as a shared HTTP server for AI clients (Windows / PowerShell).
#
#   $env:ZAN_MCP_TOKEN = '<secret>'
#   scripts\serve-mcp.ps1 -Workspace D:\project\some-project -Host 0.0.0.0
#
# The token is read from the environment (--token-env), never passed on the
# command line where the process list would show it. Never commit one.
# Details and deployment notes: docs\MCP_HOSTING.md
[CmdletBinding()]
param(
    # Workspace root the tools operate in. Every path a client sends is
    # resolved against it and rejected if it escapes.
    [string] $Workspace = (Get-Location).Path,
    [string] $BindHost  = $env:MCP_HOST,
    [int]    $Port      = 0,
    # Environment variable holding the bearer token clients must send.
    [string] $TokenEnv  = 'ZAN_MCP_TOKEN',
    # Skills directory served by skills_list / skill_read, on top of the
    # workspace's own .agents\skills.
    [string] $Skills    = '',
    [int]    $Workers   = 1,
    # Refuse every mutating tool / refuse run_command. Recommended for a
    # server reachable by anyone you would not hand a shell to.
    [switch] $ReadOnly,
    [switch] $NoExec,
    # Advertise the same tool catalog whatever this deployment can back, so
    # tools/list is identical for every project and the client's prompt prefix
    # stays cacheable. On by default for a hosted server; -FrozenTools:$false
    # to see only what is really available.
    [switch] $FrozenTools = $true,
    [string] $Exe = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ($Exe -eq '') {
    foreach ($c in @("$root\tools\zan-mcp.exe", "$root\build\zan-mcp.exe",
                     "$root\_scratch\zan-mcp.exe")) {
        if (Test-Path $c) { $Exe = $c; break }
    }
}
if ($Exe -eq '' -or -not (Test-Path $Exe)) {
    throw "zan-mcp.exe not found. Build it first: build\zanc.exe --stdlib-path stdlib tools\mcp_server\mcp_server.zan -o tools\zan-mcp.exe   (or pass -Exe)"
}
if (-not (Test-Path $Workspace)) { throw "workspace not found: $Workspace" }
if ($BindHost -eq '') { $BindHost = '127.0.0.1' }
if ($Port -le 0) { $Port = if ($env:MCP_PORT) { [int]$env:MCP_PORT } else { 18848 } }

$isLoopback = ($BindHost -eq '127.0.0.1' -or $BindHost -eq 'localhost' -or $BindHost -eq '::1')
if (-not $isLoopback -and -not (Get-Item "Env:$TokenEnv" -ErrorAction SilentlyContinue)) {
    throw "$BindHost is reachable from the network and `$env:$TokenEnv is empty. Set a token: `$env:$TokenEnv = '<secret>'"
}

$argv = @($Workspace, '--host', $BindHost, '--port', "$Port", '--token-env', $TokenEnv)
if ($FrozenTools) { $argv += '--frozen-tools' }
if ($ReadOnly)    { $argv += '--read-only' }
if ($NoExec)      { $argv += '--no-exec' }
if ($Workers -gt 1) { $argv += @('--workers', "$Workers") }
if ($Skills -ne '') { $argv += @('--skills', $Skills) }

Write-Host "zan-mcp  http://${BindHost}:$Port/mcp   workspace=$Workspace"
Write-Host "clients send: Authorization: Bearer `$env:$TokenEnv"
& $Exe @argv
exit $LASTEXITCODE

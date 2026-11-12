# Call under the existing build lock, after configure and before build; then
# Seal only after that same build succeeds. This helper never launches a build.
param(
    [Parameter(Mandatory=$true)][ValidateSet('Capture','Seal')][string]$Action,
    [Parameter(Mandatory=$true)][string]$Build,
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string[]]$NativeInput = @(),
    [string[]]$Artifact = @(),
    [switch]$SdkNativeOutputs,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$arguments = @('-I', '-B', (Join-Path $PSScriptRoot 'release_source_manifest.py'),
    $Action.ToLowerInvariant(), '--repo', [IO.Path]::GetFullPath($SourceRoot),
    '--build', [IO.Path]::GetFullPath($Build))
if ($SdkNativeOutputs) { $arguments += '--sdk-native-outputs' }
$lists = @()
try {
    # A complete SDK closure exceeds Windows' process command-line limit.
    # File lists change transport only; Python validates/hashes every entry.
    $listDir = Join-Path ([IO.Path]::GetFullPath($Build)) 'test-out'
    New-Item -ItemType Directory -Force -Path $listDir | Out-Null
    foreach ($item in @(@{ flag = '--native-input-list'; paths = $NativeInput }, @{ flag = '--artifact-list'; paths = $Artifact })) {
        if ($item.paths.Count -eq 0) { continue }
        $list = Join-Path $listDir ('identity-paths-' + [Guid]::NewGuid().ToString('N') + '.json')
        $lists += $list
        $paths = @($item.paths | ForEach-Object { [IO.Path]::GetFullPath($_) })
        ConvertTo-Json -InputObject $paths | Set-Content -LiteralPath $list -Encoding UTF8
        $arguments += @($item.flag, $list)
    }
    & $Python @arguments
    if ($LASTEXITCODE -ne 0) { throw "Release build identity $Action failed exit=$LASTEXITCODE" }
} finally {
    foreach ($list in $lists) { Remove-Item -LiteralPath $list -Force -ErrorAction SilentlyContinue }
}

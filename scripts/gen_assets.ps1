# Manifest-driven asset generator using the configured image model.
# Reads secrets.local.ps1 for OPENAI_API_KEY / OPENAI_BASE_URL and a JSON
# manifest: [{ "prompt": "...", "out": "examples/game/x/assets/y.png",
#              "size": "1024x1024", "background": "transparent" }, ...]
param(
    [Parameter(Mandatory = $true)][string]$Manifest,
    [string]$Model = "gpt-image-2",
    [switch]$Force
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
. (Join-Path $root "secrets.local.ps1")

$items = Get-Content -Raw -LiteralPath $Manifest | ConvertFrom-Json
foreach ($it in $items) {
    $outPath = Join-Path $root $it.out
    if ((Test-Path -LiteralPath $outPath) -and -not $Force) {
        Write-Output "SKIP (exists) $($it.out)"
        continue
    }
    $dir = Split-Path -Parent $outPath
    if (!(Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

    $size = "1024x1024"
    if ($it.size) { $size = $it.size }
    $body = @{ model = $Model; prompt = $it.prompt; size = $size; n = 1 }
    if ($it.background) { $body.background = $it.background }
    $json = $body | ConvertTo-Json -Depth 6

    $ok = $false
    for ($attempt = 1; $attempt -le 3 -and -not $ok; $attempt++) {
        try {
            $r = Invoke-RestMethod -Uri "$($env:OPENAI_BASE_URL)/images/generations" `
                -Method Post -Headers @{ Authorization = "Bearer $($env:OPENAI_API_KEY)" } `
                -ContentType "application/json" -Body $json -TimeoutSec 240
            $b64 = $r.data[0].b64_json
            if (-not $b64) { throw "no b64_json in response" }
            [IO.File]::WriteAllBytes($outPath, [Convert]::FromBase64String($b64))
            Write-Output "OK $($it.out) ($size, $([IO.File]::ReadAllBytes($outPath).Length) bytes)"
            $ok = $true
        } catch {
            Write-Output "RETRY $attempt $($it.out): $($_.Exception.Message)"
            Start-Sleep -Seconds 3
        }
    }
    if (-not $ok) { Write-Output "FAIL $($it.out)" }
}

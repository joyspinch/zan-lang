param(
  [string]$Prompt = "",
  # Path to a UTF-8 file holding the prompt. Preferred when launching detached:
  # a prompt passed inline is re-split on spaces by PowerShell's -File parser.
  [string]$PromptFile = "",
  [Parameter(Mandatory=$true)][string]$Out,
  [string]$Size = "1024x1024"
)
# Generates one skin-pack asset with the image model configured in
# secrets.local.ps1 and writes it as a PNG. Used to author the illustration
# layer of a skin pack (banner art, mascot) that the GUI then animates.
$ErrorActionPreference = "Stop"
. d:\project\zan-lang\secrets.local.ps1
if ($PromptFile -ne "") { $Prompt = [IO.File]::ReadAllText($PromptFile) }
if ($Prompt -eq "") { throw "no prompt: pass -Prompt or -PromptFile" }
$req = @{ model = "gpt-image-2"; prompt = $Prompt; size = $Size } | ConvertTo-Json -Compress
$reqFile = [IO.Path]::GetTempFileName()
[IO.File]::WriteAllText($reqFile, $req)
$respFile = [IO.Path]::GetTempFileName()
$code = & curl.exe -s -m 300 -o $respFile -w "%{http_code}" "$env:OPENAI_BASE_URL/images/generations" `
  -H "Authorization: Bearer $env:OPENAI_API_KEY" -H "Content-Type: application/json" `
  --data-binary "@$reqFile"
Remove-Item $reqFile -Force
if ($code -ne "200") {
  Write-Output "HTTP_$code"
  Get-Content $respFile -Raw | Select-Object -First 1
  exit 1
}
$json = Get-Content $respFile -Raw | ConvertFrom-Json
Remove-Item $respFile -Force
$b64 = $json.data[0].b64_json
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
[IO.File]::WriteAllBytes($Out, [Convert]::FromBase64String($b64))
Write-Output ("OK " + $Out + " " + (Get-Item $Out).Length)

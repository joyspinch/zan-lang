param(
  # SendKeys 组合串，分号分隔，每组单独起一个探针进程，日志不串。
  # 例："+{F12};^{ };^+f;%{ENTER}"
  [string]$Combos = "+{F12};^{ };^+f",
  [string]$Zanc = "$PSScriptRoot\..\build\zanc.exe",
  [string]$OutDir = "$PSScriptRoot\..\_scratch\keyprobe"
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."
if (-not (Test-Path $Zanc)) { throw "zanc not found: $Zanc" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$src = Join-Path $root "scripts\key_event_probe.zan"
$exe = Join-Path $OutDir "key_event_probe.exe"
Write-Host "[probe] compiling $src"
& $Zanc $src --auto-stdlib --package-project $root -o $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

$ws = New-Object -ComObject WScript.Shell
foreach ($combo in ($Combos -split ';')) {
  if ($combo.Trim() -eq "") { continue }
  $log = Join-Path $OutDir ("kp_" + ($combo -replace '[^A-Za-z0-9]', '_') + ".log")
  $p = Start-Process -FilePath $exe -RedirectStandardOutput $log -PassThru -WindowStyle Normal
  Start-Sleep -Seconds 2
  $null = $ws.AppActivate($p.Id)
  Start-Sleep -Milliseconds 700
  $ws.SendKeys($combo)
  Start-Sleep -Seconds 2
  Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
  Write-Output "=== $combo ==="
  if (Test-Path $log) { Get-Content $log | Where-Object { $_ -like "EV *" } }
}

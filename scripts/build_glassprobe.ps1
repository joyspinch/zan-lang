$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
    -Drivers @("zan_gui", "zan_image", "zan_audio")
& build\zanc.exe examples\glass_probe\glass_probe.zan --auto-stdlib `
    --link-mode static --subsystem windows --icon assets\zan.ico `
    -o build\glassprobe.exe
if ($LASTEXITCODE -ne 0) { throw "GLASSPROBE_LINK_FAILED code=$LASTEXITCODE" }
Write-Output "GLASSPROBE_BUILD_OK build\glassprobe.exe"

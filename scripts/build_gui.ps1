# Build a standalone windowed gui_demo using the normal zanc link driver.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
    -Drivers @("zan_gui", "zan_image", "zan_audio")
& build\zanc.exe examples\gui_demo.zan --auto-stdlib `
    --link-mode static --subsystem windows --icon assets\zan.ico `
    -o build\gui_demo.exe
if ($LASTEXITCODE -ne 0) { throw "GUI_DEMO_LINK_FAILED code=$LASTEXITCODE" }
Write-Output "GUI_BUILD_OK build\gui_demo.exe"

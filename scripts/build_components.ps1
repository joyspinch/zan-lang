# Build the components demo with zanc's normal driver selection/link path.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
    -Drivers @("zan_gui", "zan_image", "zan_audio")
& build\zanc.exe examples\components_demo.zan --auto-stdlib `
    --link-mode static --subsystem windows --icon assets\zan.ico `
    -o build\components_demo.exe
if ($LASTEXITCODE -ne 0) { throw "COMPONENTS_LINK_FAILED code=$LASTEXITCODE" }
Write-Output "COMPONENTS_BUILD_OK build\components_demo.exe"

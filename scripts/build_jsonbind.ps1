# Build the declarative JSON/two-way binding demo with independent drivers.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
    -Drivers @("zan_gui", "zan_image", "zan_audio")
& build\zanc.exe examples\json_ui_binding\json_ui_binding.zan --auto-stdlib `
    --link-mode static --subsystem windows --icon assets\zan.ico `
    -o build\jsonbind.exe
if ($LASTEXITCODE -ne 0) { throw "JSONBIND_LINK_FAILED code=$LASTEXITCODE" }
Write-Output "JSONBIND_BUILD_OK build\jsonbind.exe"

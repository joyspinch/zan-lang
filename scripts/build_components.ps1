$ErrorActionPreference = "Stop"
Set-Location 'd:\project\zan-lang'
$files = @()
$files += (Get-ChildItem stdlib\Gui\*.zan).FullName
$files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
$files += (Join-Path (Get-Location) "examples\components_demo.zan")
Push-Location build
$ir = & .\zanc.exe --emit-ir $files
$code = $LASTEXITCODE
if ($code -eq 0) {
    [System.IO.File]::WriteAllLines((Join-Path (Get-Location) "components_demo.ll"), $ir)
} else {
    Write-Output "IR_FAILED code=$code"
    Pop-Location
    exit 1
}
clang components_demo.ll zan_icon.res -o components_demo.exe -O2 -Xlinker /STACK:268435456 -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup -lzan_gui
$lc = $LASTEXITCODE
Pop-Location
if ($lc -ne 0) { Write-Output "LINK_FAILED code=$lc"; exit 1 }
Write-Output "COMPONENTS_BUILD_OK"

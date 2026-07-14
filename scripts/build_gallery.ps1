$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
clang -O2 -std=c11 -I src\runtime -c src\runtime\rt_sync.c -o build\zanrt_sync_gallery.obj
if ($LASTEXITCODE -ne 0) { Write-Output "RTSYNC_FAILED"; exit 1 }
$files = @()
$files += (Get-ChildItem stdlib\Gui\*.zan).FullName
$files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
$files += (Join-Path (Get-Location) "examples\gui_gallery\gui_gallery.zan")
Push-Location build
$ir = & .\zanc.exe --emit-ir $files
$code = $LASTEXITCODE
if ($code -eq 0) {
    [System.IO.File]::WriteAllLines((Join-Path (Get-Location) "gallery_test.ll"), $ir)
} else {
    Write-Output "IR_FAILED code=$code"
    Write-Output $ir
    Pop-Location
    exit 1
}
clang gallery_test.ll zanrt_sync_gallery.obj zan_icon.res -o gallery_test.exe -O2 -Xlinker /STACK:268435456 -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup -lzan_gui -llegacy_stdio_definitions
$lc = $LASTEXITCODE
Pop-Location
if ($lc -ne 0) { Write-Output "LINK_FAILED code=$lc"; exit 1 }
Write-Output "GALLERY_BUILD_OK"

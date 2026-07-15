$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$galleryComponents = "examples\gui_gallery\components"
$registryPath = Join-Path $root "stdlib\Gui\CustomComponents.zan"
$registryOriginal = [System.IO.File]::ReadAllText($registryPath)
$failed = $false

try {
    powershell -ExecutionPolicy Bypass -File scripts\scan_components.ps1 `
        -Source $galleryComponents -Out "stdlib\Gui\CustomComponents.zan"
    if ($LASTEXITCODE -ne 0) { throw "COMPONENT_SCAN_FAILED" }

    clang -O2 -std=c11 -I src\runtime -c src\runtime\rt_sync.c -o build\zanrt_sync_gallery.obj
    if ($LASTEXITCODE -ne 0) { throw "RTSYNC_FAILED" }

    $files = @()
    $files += (Get-ChildItem stdlib\Gui\*.zan).FullName
    $files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
    $files += (Get-ChildItem $galleryComponents\*.zan).FullName
    $files += (Join-Path (Get-Location) "examples\gui_gallery\gui_gallery.zan")
    Push-Location build
    try {
        $ir = & .\zanc.exe --emit-ir $files
        $code = $LASTEXITCODE
        if ($code -ne 0) {
            Write-Output $ir
            throw "IR_FAILED code=$code"
        }
        [System.IO.File]::WriteAllLines(
            (Join-Path (Get-Location) "gallery_test.ll"), $ir)
        clang gallery_test.ll zanrt_sync_gallery.obj zan_icon.res -o gallery_test.exe -O2 `
            -Xlinker /STACK:268435456 -Xlinker /SUBSYSTEM:WINDOWS `
            -Xlinker /ENTRY:mainCRTStartup -lzan_gui -llegacy_stdio_definitions
        if ($LASTEXITCODE -ne 0) { throw "LINK_FAILED code=$LASTEXITCODE" }
    } finally {
        Pop-Location
    }
} catch {
    Write-Output $_
    $failed = $true
} finally {
    [System.IO.File]::WriteAllText($registryPath, $registryOriginal)
}

if ($failed) { exit 1 }
Write-Output "GALLERY_BUILD_OK"

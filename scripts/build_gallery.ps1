$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# The GUI runtime now uses the unified SDL3 windowing backend (ZAN_GUI_SDL),
# the same one the IDE links. Build the gallery against it: rebuild the static
# GUI runtime lib with -DZAN_GUI_SDL, then link SDL3 (import lib + runtime DLL).
$cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $cache) { throw "SDL3 devel cache not found; run scripts\stage_sdl3.ps1 first." }
$pkg = Join-Path $cache.FullName ((Get-ChildItem $cache.FullName -Filter "SDL3-*" -Directory | Select-Object -First 1).Name)
$pkg = Join-Path $pkg "x86_64-w64-mingw32"
$inc = Join-Path $pkg "include"
$driverDir = Join-Path $root "stdlib\SDL3\drivers\win-x64"
$sdlLib = Join-Path $driverDir "SDL3.lib"
if (!(Test-Path $sdlLib)) { throw "SDL3.lib not found ($sdlLib); run scripts\build_ide.ps1 (or build_gui_sdl_smoke.ps1) first." }

$galleryComponents = "examples\gui_gallery\components"
$registryPath = Join-Path $root "stdlib\Gui\CustomComponents.zan"
$registryOriginal = [System.IO.File]::ReadAllText($registryPath)
$failed = $false

try {
    Write-Output "[1/5] Rebuilding native GUI runtime (SDL3 windowing)..."
    clang -O2 -DZAN_GUI_STATIC -DZAN_GUI_SDL "-I$inc" -c src\runtime\gui_runtime.c -o build\zan_gui_static.obj
    if ($LASTEXITCODE -ne 0) { throw "RUNTIME_COMPILE_FAILED" }
    llvm-lib /out:build\zan_gui.lib build\zan_gui_static.obj | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "RUNTIME_LIB_FAILED" }

    if (-not (Test-Path build\zan_icon.res)) {
        Write-Output "[2/5] Compiling app icon resource..."
        llvm-rc /fo build\zan_icon.res /I assets assets\zan.rc | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "ICON_RC_FAILED" }
    }

    Write-Output "[3/5] Scanning custom components..."
    powershell -ExecutionPolicy Bypass -File scripts\scan_components.ps1 `
        -Source $galleryComponents -Out "stdlib\Gui\CustomComponents.zan"
    if ($LASTEXITCODE -ne 0) { throw "COMPONENT_SCAN_FAILED" }

    clang -O2 -std=c11 -I src\runtime -c src\runtime\rt_sync.c -o build\zanrt_sync_gallery.obj
    if ($LASTEXITCODE -ne 0) { throw "RTSYNC_FAILED" }

    Write-Output "[4/5] Emitting LLVM IR from Zan sources..."
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
        Write-Output "[5/5] Linking gallery_test.exe (SDL3)..."
        clang gallery_test.ll zanrt_sync_gallery.obj zan_icon.res -o gallery_test.exe -O2 `
            -Xlinker /STACK:268435456 -Xlinker /SUBSYSTEM:WINDOWS `
            -Xlinker /ENTRY:mainCRTStartup -lzan_gui "$sdlLib" -llegacy_stdio_definitions -lws2_32
        if ($LASTEXITCODE -ne 0) { throw "LINK_FAILED code=$LASTEXITCODE" }
        Copy-Item -LiteralPath (Join-Path $driverDir "SDL3.dll") `
            -Destination (Join-Path (Get-Location) "SDL3.dll") -Force
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
Write-Output "GALLERY_BUILD_OK build\gallery_test.exe"

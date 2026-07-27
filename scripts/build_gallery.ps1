$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# The GUI runtime uses the unified SDL3 windowing backend (ZAN_GUI_SDL), the
# same one the IDE links. The gallery links exactly like the IDE: a static
# mingw-ABI GUI runtime plus zanc's own link driver, which pulls in the async
# reactor (rt_io) and sync runtime (rt_sync) automatically.
$cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $cache) { throw "SDL3 devel cache not found; run scripts\stage_sdl3.ps1 first." }
$pkg = Join-Path $cache.FullName ((Get-ChildItem $cache.FullName -Filter "SDL3-*" -Directory | Select-Object -First 1).Name)
$pkg = Join-Path $pkg "x86_64-w64-mingw32"
$inc = Join-Path $pkg "include"
$sdlLibDir = Join-Path $pkg "lib"
$driverDir = Join-Path $root "stdlib\SDL3\drivers\win-x64"

$galleryComponents = "examples\gui_gallery\components"
$registryPath = Join-Path $root "stdlib\Gui\CustomComponents.zan"
$registryOriginal = [System.IO.File]::ReadAllText($registryPath)
$failed = $false

try {
    Write-Output "[1/3] Building native GUI runtime (SDL3, static, mingw ABI)..."
    clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC -DZAN_GUI_SDL "-I$inc" `
        -c src\runtime\gui_runtime.c -o build\zan_gui_gallery_gnu.o
    if ($LASTEXITCODE -ne 0) { throw "RUNTIME_COMPILE_FAILED" }
    llvm-ar rcs build\libzan_gui_gallery_gnu.a build\zan_gui_gallery_gnu.o
    if ($LASTEXITCODE -ne 0) { throw "RUNTIME_LIB_FAILED" }

    Write-Output "[2/3] Scanning custom components..."
    powershell -ExecutionPolicy Bypass -File scripts\scan_components.ps1 `
        -Source $galleryComponents -Out "stdlib\Gui\CustomComponents.zan"
    if ($LASTEXITCODE -ne 0) { throw "COMPONENT_SCAN_FAILED" }

    Write-Output "[3/3] Compiling and linking gallery_test.exe..."
    $files = @()
    $files += (Get-ChildItem stdlib\Gui\*.zan).FullName
    $files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
    $files += (Get-ChildItem $galleryComponents\*.zan).FullName
    $files += (Join-Path (Get-Location) "examples\gui_gallery\gui_gallery.zan")

    $zanArgs = @()
    $zanArgs += $files
    $zanArgs += @("-o", "build\gallery_test.exe", "--subsystem", "windows")
    $zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_gallery_gnu")
    $zanArgs += @("--libpath", $sdlLibDir, "--link-lib", "SDL3")
    $zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
    $zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
    $zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
    $zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
    $out = & build\zanc.exe @zanArgs 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        $out | Select-Object -Last 40
        throw "GALLERY_LINK_FAILED code=$code"
    }
    Copy-Item -LiteralPath (Join-Path $driverDir "SDL3.dll") `
        -Destination (Join-Path (Get-Location) "build\SDL3.dll") -Force
} catch {
    Write-Output $_
    $failed = $true
} finally {
    [System.IO.File]::WriteAllText($registryPath, $registryOriginal)
}

if ($failed) { exit 1 }
Write-Output "GALLERY_BUILD_OK build\gallery_test.exe"

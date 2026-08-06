$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# The gallery is a pure Gui stdlib program: on Windows its window shell is the
# Zan-side Win32Shell (Native.zan #if WINDOWS), so the native Win32 GUI
# runtime is enough -- no SDL3 is linked (the IDE's SDL3 link is a game/IDE
# thing, the gallery does not need it). The runtime is built as a static
# mingw-ABI archive plus zanc's own link driver, which pulls in the async
# reactor (rt_io) and sync runtime (rt_sync) automatically.

$galleryComponents = "examples\gui_gallery\components"
$registryPath = Join-Path $root "stdlib\Gui\CustomComponents.zan"
$registryOriginal = [System.IO.File]::ReadAllText($registryPath)
$failed = $false

try {
    Write-Output "[1/3] Building native GUI runtime (Win32, static, mingw ABI)..."
    clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
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
    # Native Win32 backend needs only the system libs it imports directly (the
    # runtime's #pragma libs: dwmapi/user32/gdi32/imm32) plus the reactor deps.
    $zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
    $zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
    $zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
    $zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4")
    $zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
    $out = & build\zanc.exe @zanArgs 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        $out | Select-Object -Last 40
        throw "GALLERY_LINK_FAILED code=$code"
    }
} catch {
    Write-Output $_
    $failed = $true
} finally {
    [System.IO.File]::WriteAllText($registryPath, $registryOriginal)
}

if ($failed) { exit 1 }
Write-Output "GALLERY_BUILD_OK build\gallery_test.exe"

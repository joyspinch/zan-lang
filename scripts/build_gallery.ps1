$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# The gallery is a pure Gui stdlib program: on Windows its window shell is the
# Zan-side Win32Shell (Native.zan #if WINDOWS), so the native Win32 GUI
# runtime is enough -- no SDL3 is linked (the IDE's game link is a game/IDE
# thing, the gallery does not need it). The runtime is built as a static
# mingw-ABI archive plus zanc's own link driver, which pulls in the async
# reactor (rt_io) and sync runtime (rt_sync) automatically.

$galleryComponents = "examples\gui_gallery\components"
$failed = $false

try {
    Write-Output "[1/2] Building independent native drivers (static, mingw ABI)..."
    & (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
        -Drivers @("zan_gui", "zan_image", "zan_audio")

    Write-Output "[2/2] Compiling and linking gallery_test.exe..."
    $files = @()
    $files += (Get-ChildItem packages\Zan.Gui\src\Gui\*.zan).FullName
    $files += (Get-ChildItem packages\Zan.Gui\src\Gui\Widget\*.zan).FullName
    $files += (Get-ChildItem $galleryComponents\*.zan).FullName
    $files += (Join-Path (Get-Location) "examples\gui_gallery\gui_gallery.zan")
    $files += (Join-Path (Get-Location) "examples\gui_gallery\MapChinaData.zan")

    $zanArgs = @()
    $zanArgs += $files
    $zanArgs += @("-o", "build\gallery_test.exe", "--subsystem", "windows")
    # Demo/props/events catalog + map geometry (assets/) travel inside the
    # exe; File.ReadAllText falls back to the embedded copy when the loose
    # file is not next to the binary.
    $zanArgs += @("--embed", "examples\gui_gallery\assets=assets")
    $zanArgs += @("--auto-stdlib", "--link-mode", "static")
    # Each selected driver contributes its own system dependency metadata.
    $zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
    $zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
    $zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32",
        "--link-lib", "ole32")
    $zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4", "--link-lib", "winpthread")
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
}

if ($failed) { exit 1 }
Write-Output "GALLERY_BUILD_OK build\gallery_test.exe"

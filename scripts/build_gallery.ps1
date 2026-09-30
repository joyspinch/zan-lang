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
    Write-Output "[1/2] Building native GUI runtime (Win32, static, mingw ABI)..."
    clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
        -c src\runtime\gui_runtime.c -o build\zan_gui_gallery_gnu.o
    if ($LASTEXITCODE -ne 0) { throw "RUNTIME_COMPILE_FAILED" }
    # irgen emits zan_rt_guard_fail2/soft_note2 (guard-string dedup, dda956d3)
    # from rt_timer.c; the archive must carry it or guard sites fail to link.
    clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
        -c src\runtime\rt_timer.c -o build\zan_gui_gallery_timer_gnu.o
    if ($LASTEXITCODE -ne 0) { throw "TIMER_COMPILE_FAILED" }
    # Text shaping goes through the DirectWrite backend: gui_runtime.c only
    # declares zan_dw_render, the implementation lives in gui_runtime_dwrite.cpp
    # (it LoadLibraryW's dwrite.dll at runtime, so no import lib is needed).
    # Leaving the object out = undefined reference at link.
    clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
        -fno-exceptions -fno-rtti `
        -c src\runtime\gui_runtime_dwrite.cpp -o build\zan_gui_dwrite_gallery_gnu.o
    if ($LASTEXITCODE -ne 0) { throw "RUNTIME_DWRITE_COMPILE_FAILED" }
    llvm-ar rcs build\libzan_gui_gallery_gnu.a build\zan_gui_gallery_gnu.o build\zan_gui_gallery_timer_gnu.o build\zan_gui_dwrite_gallery_gnu.o
    if ($LASTEXITCODE -ne 0) { throw "RUNTIME_LIB_FAILED" }

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
    $zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_gallery_gnu")
    # Native Win32 backend needs only the system libs it imports directly (the
    # runtime's #pragma libs: dwmapi/user32/gdi32/imm32) plus the reactor deps.
    # ole32: zan_audio 的 WASAPI 设备枚举走 COM（CoInitializeEx/CoCreateInstance
    # /CoTaskMemFree），静态驱动归档直接引用这些符号，缺 -lole32 链接失败
    # （同 build_ide.ps1，2026-09-13 gallery 实测）。
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

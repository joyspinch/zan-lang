$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- SDL3 windowing backend (unified with Game.*) -------------------------
# The native GUI runtime is an SDL3 window driven by the same stack games use.
# Locate the staged SDL3 mingw devel package (headers + libSDL3.dll.a import
# library) - it matches zanc's own x86_64-w64-windows-gnu link ABI.
$driverDir = Join-Path $root "stdlib\SDL3\drivers\win-x64"
$cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory `
    | Sort-Object Name -Descending | Select-Object -First 1
if (-not $cache) {
    Write-Output "SDL3 devel package not staged; running stage_sdl3.ps1 ..."
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "stage_sdl3.ps1")
    if ($LASTEXITCODE -ne 0) { Write-Output "SDL3_STAGE_FAILED"; exit 1 }
    $cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory `
        | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $cache) { Write-Output "SDL3_STAGE_MISSING"; exit 1 }
}
$pkg = Join-Path $cache.FullName ((Get-ChildItem $cache.FullName -Filter "SDL3-*" -Directory | Select-Object -First 1).Name)
$pkg = Join-Path $pkg "x86_64-w64-mingw32"
$inc = Join-Path $pkg "include"
$sdlLibDir = Join-Path $pkg "lib"   # contains libSDL3.dll.a (mingw import lib)

# ---- native GUI runtime (static, mingw ABI) -------------------------------
# Compiled for zanc's own x86_64-w64-windows-gnu link ABI so it can be linked
# straight through the compiler (which drives the bundled ld + MinGW runtime).
# Static (ZAN_GUI_STATIC) so the IDE carries no zan_gui.dll dependency.
Write-Output "[0/2] Building native GUI runtime (SDL3, static, mingw ABI)..."
clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC -DZAN_GUI_SDL "-I$inc" `
    -c src\runtime\gui_runtime.c -o build\zan_gui_ide_gnu.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_COMPILE_FAILED"; exit 1 }
llvm-ar rcs build\libzan_gui_ide_gnu.a build\zan_gui_ide_gnu.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_LIB_FAILED"; exit 1 }

# ---- application icon resource --------------------------------------------
# Compiled to a COFF object with windres so the bundled GNU ld can link it
# (GNU ld cannot consume a .res directly).
Write-Output "Compiling application icon (assets\zan.rc -> build\zan_icon.o) ..."
windres assets\zan.rc -O coff -o build\zan_icon.o "--include-dir=assets"
if ($LASTEXITCODE -ne 0) { Write-Output "ICON_RC_FAILED"; exit 1 }

$registryPath = Join-Path $root "stdlib\Gui\CustomComponents.zan"
$registryOriginal = [System.IO.File]::ReadAllText($registryPath)
$failed = $false

try {
    & powershell -ExecutionPolicy Bypass -File scripts\scan_components.ps1 `
        -Source src\ide_zan\components -Out stdlib\Gui\CustomComponents.zan
    if ($LASTEXITCODE -ne 0) { throw "SCAN_FAILED" }

    $files = @()
    $files += (Get-ChildItem stdlib\Gui\*.zan).FullName
    $files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
    $files += (Get-ChildItem src\ide_zan\components\*.zan).FullName
    if (Test-Path src\ide_zan\views) {
        $viewFiles = Get-ChildItem src\ide_zan\views\*.zan -ErrorAction SilentlyContinue
        if ($viewFiles) { $files += $viewFiles.FullName }
    }
    $files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\Diagnostics\Process.zan")
    $files += (Join-Path (Get-Location) "stdlib\Game\Scene\SceneDoc.zan")
    # Crypto + encrypted resource pack (.zrp publishing from the Asset Manager).
    $files += (Get-ChildItem stdlib\System\Security\Cryptography\*.zan).FullName
    $files += (Join-Path (Get-Location) "stdlib\System\Resources\ResourcePack.zan")
    $files += (Join-Path (Get-Location) "src\ide_zan\SceneDesigner.zan")
    $files += (Join-Path (Get-Location) "src\ide_zan\AssetManager.zan")
    $files += (Join-Path (Get-Location) "src\ide_zan\ZanIDE.zan")

    # Link the IDE straight through zanc: it compiles all sources and drives the
    # bundled ld itself, auto-linking the socket-async reactor (rt_io) and the
    # atomic/sync runtime (rt_sync) as needed - no hand-compiled runtime objects.
    # 256 MB stack + WINDOWS subsystem are applied by zanc's own link path.
    $zanArgs = @()
    $zanArgs += $files
    $zanArgs += @("-o", "build\ZanIDE.exe", "--subsystem", "windows")
    $zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_ide_gnu")
    $zanArgs += @("--libpath", $sdlLibDir, "--link-lib", "SDL3")
    $zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
    $zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
    $zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
    $zanArgs += @("--link-input", (Join-Path (Get-Location) "build\zan_icon.o"))
    $out = & build\zanc.exe @zanArgs 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        $out | Select-Object -Last 40
        throw "IDE_LINK_FAILED code=$code"
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
Write-Output "IDE_BUILD_OK"

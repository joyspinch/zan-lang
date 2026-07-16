$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- SDL3 windowing backend (unified with Game.*) -------------------------
# The native GUI runtime is now built with ZAN_GUI_SDL so the IDE window is an
# SDL3 window driven by the same stack games use. Locate the staged SDL3 mingw
# devel package (headers) and synthesize an MSVC import lib (SDL3.lib) from the
# driver DLL â€?mirrors scripts\build_gui_sdl_smoke.ps1.
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

$sdlLib = Join-Path $driverDir "SDL3.lib"
if (!(Test-Path $sdlLib)) {
    Write-Output "Generating SDL3.lib import library from SDL3.dll ..."
    $dll = Join-Path $driverDir "SDL3.dll"
    $def = Join-Path $driverDir "SDL3.def"
    $dump = & llvm-objdump -p $dll
    $names = @()
    $inExports = $false
    foreach ($line in $dump) {
        if ($line -match "Export Table:") { $inExports = $true; continue }
        if ($inExports) {
            $m = [regex]::Match($line, "^\s+\d+\s+0x[0-9a-fA-F]+\s+(\S+)\s*$")
            if ($m.Success) { $names += $m.Groups[1].Value }
        }
    }
    if ($names.Count -eq 0) { Write-Output "SDL3_DEF_EMPTY"; exit 1 }
    "LIBRARY SDL3.dll`nEXPORTS" | Set-Content -Encoding ascii $def
    $names | ForEach-Object { $_ } | Add-Content -Encoding ascii $def
    & llvm-dlltool -m i386:x86-64 -d $def -l $sdlLib -D "SDL3.dll"
    if ($LASTEXITCODE -ne 0) { Write-Output "SDL3_DLLTOOL_FAILED"; exit 1 }
    Write-Output "Wrote $sdlLib ($($names.Count) exports)"
}

Write-Output "[0/2] Rebuilding native GUI runtime (SDL3 windowing)..."
clang -O2 -DZAN_GUI_STATIC -DZAN_GUI_SDL "-I$inc" -c src\runtime\gui_runtime.c -o build\zan_gui_static.obj
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_COMPILE_FAILED"; exit 1 }
llvm-lib /out:build\zan_gui.lib build\zan_gui_static.obj | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_LIB_FAILED"; exit 1 }

# Build the atomic/dispatch/thread runtime (rt_sync.c) with the same MSVC-target
# clang used to link the IDE, so its libc references match (the CMake-built
# zanrt_sync.obj targets the GNU runtime and pulls in __mingw_* symbols).
clang -O2 -std=c11 -I src\runtime -c src\runtime\rt_sync.c -o build\zanrt_sync_ide.obj
if ($LASTEXITCODE -ne 0) { Write-Output "SYNC_COMPILE_FAILED"; exit 1 }

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
    $files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\Diagnostics\Process.zan")
    $files += (Join-Path (Get-Location) "src\ide_zan\ZanIDE.zan")
    Push-Location build
    try {
        $ir = & .\zanc.exe --emit-ir $files
        $code = $LASTEXITCODE
        if ($code -ne 0) {
            $ir | Select-Object -Last 30
            throw "IR_FAILED code=$code"
        }
        [System.IO.File]::WriteAllLines((Join-Path (Get-Location) "ZanIDE.ll"), $ir)
        clang ZanIDE.ll zanrt_sync_ide.obj zan_icon.res -o ZanIDE.exe -O2 `
            -Xlinker /STACK:268435456 -Xlinker /SUBSYSTEM:WINDOWS `
            -Xlinker /ENTRY:mainCRTStartup -lzan_gui "$sdlLib" -llegacy_stdio_definitions
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
Write-Output "IDE_BUILD_OK"

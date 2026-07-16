# Builds the cross-platform SDL3 windowing smoke for the zan_gui runtime.
# Reuses build\zan_gui.lib (produced by build_ide.ps1) and the SDL3 devel
# package staged by scripts\stage_sdl3.ps1. Output: build\gui_sdl_smoke.exe
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (!(Test-Path "build\zan_gui.lib")) {
    Write-Output "build\zan_gui.lib missing — compiling gui_runtime.c ..."
    clang -O2 -DZAN_GUI_STATIC -c src\runtime\gui_runtime.c -o build\zan_gui_static.obj
    llvm-lib /out:build\zan_gui.lib build\zan_gui_static.obj | Out-Null
}

# Locate the staged SDL3 mingw devel package (include + import lib).
$cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $cache) { throw "SDL3 devel cache not found; run scripts\stage_sdl3.ps1 first." }
$pkg = Join-Path $cache.FullName ((Get-ChildItem $cache.FullName -Filter "SDL3-*" -Directory | Select-Object -First 1).Name)
$pkg = Join-Path $pkg "x86_64-w64-mingw32"
$inc = Join-Path $pkg "include"
$lib = Join-Path $pkg "lib"
$driverDir = Join-Path $root "stdlib\SDL3\drivers\win-x64"

Write-Output "SDL3 include: $inc"

# MSVC-target clang needs an MSVC import lib (SDL3.lib). The mingw devel package
# only ships libSDL3.dll.a, so synthesize SDL3.lib from the DLL's export table
# (llvm-objdump -> .def -> llvm-dlltool). Cached next to the driver DLL.
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
            # rows look like: "   <ordinal>  0x....  SDL_Init"
            $m = [regex]::Match($line, "^\s+\d+\s+0x[0-9a-fA-F]+\s+(\S+)\s*$")
            if ($m.Success) { $names += $m.Groups[1].Value }
        }
    }
    if ($names.Count -eq 0) { throw "no exports parsed from SDL3.dll" }
    "LIBRARY SDL3.dll`nEXPORTS" | Set-Content -Encoding ascii $def
    $names | ForEach-Object { $_ } | Add-Content -Encoding ascii $def
    & llvm-dlltool -m i386:x86-64 -d $def -l $sdlLib -D "SDL3.dll"
    if ($LASTEXITCODE -ne 0) { throw "llvm-dlltool failed" }
    Write-Output "Wrote $sdlLib ($($names.Count) exports)"
}

clang tests\runtime\gui_sdl_smoke.c `
    "-I$inc" `
    -Lbuild -lzan_gui `
    "$sdlLib" `
    -llegacy_stdio_definitions `
    -o build\gui_sdl_smoke.exe
if ($LASTEXITCODE -ne 0) { throw "smoke link failed" }

Copy-Item -LiteralPath (Join-Path $driverDir "SDL3.dll") -Destination "build\SDL3.dll" -Force
Write-Output "GUI_SDL_SMOKE_OK build\gui_sdl_smoke.exe"

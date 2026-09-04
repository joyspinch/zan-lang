# Builds the SDL3-backend zan_gui runtime as a static GNU archive for Windows
# games: build\libzan_gui_sdl_static.a + staged copy at
# stdlib\Gui\drivers\win-x64\static-sdl\libzan_gui.a (with its .libs manifest).
#
# This variant compiles gui_runtime.c with BOTH -DZAN_GUI_STATIC (archive
# mode, EXPORT stripped) and -DZAN_GUI_SDL (windowing/input/present through
# SDL3). Games that adopt a Gui.App HUD over their own SDL window (Game.Ui.Hud)
# link this archive instead of the Win32-backend static\libzan_gui.a, which
# lacks the zan_gui_adopt_sdl_window / zan_gui_scene_upload / zan_gui_scene_present
# exports. zanc resolves it automatically when static-sdl\libzan_gui.a exists
# (driver static dir lookup prefers static-sdl over static) or via
# --link-lib zan_gui_sdl_static.
#
# SDL3 headers + static lib come from the devel package staged by
# scripts\stage_sdl3.ps1 (%TEMP%\zan-sdl3-<ver>\SDL3-<ver>\x86_64-w64-mingw32).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $cache) { throw "SDL3 devel cache not found; run scripts\stage_sdl3.ps1 first." }
$pkg = Join-Path $cache.FullName ((Get-ChildItem $cache.FullName -Filter "SDL3-*" -Directory | Select-Object -First 1).Name)
$pkg = Join-Path $pkg "x86_64-w64-mingw32"
$inc = Join-Path $pkg "include"
$sdlStatic = Join-Path $pkg "lib\libSDL3.a"
if (!(Test-Path $sdlStatic)) { throw "static SDL3 archive missing: $sdlStatic" }

Write-Output "[1/2] Compiling gui_runtime.c (ZAN_GUI_STATIC + ZAN_GUI_SDL) ..."
clang --target=x86_64-w64-windows-gnu -O2 `
    -DZAN_GUI_STATIC -DZAN_GUI_SDL `
    "-I$inc" `
    -c src\runtime\gui_runtime.c `
    -o build\zan_gui_sdl_static.obj
if ($LASTEXITCODE -ne 0) { throw "gui_runtime compile failed" }

Write-Output "[2/2] Archiving ..."
llvm-ar rcs build\libzan_gui_sdl_static.a build\zan_gui_sdl_static.obj
if ($LASTEXITCODE -ne 0) { throw "llvm-ar failed" }

# Stage for zanc's driver static lookup: static-sdl subdir + manifest naming
# the SDL3 static archive so --link-mode static games link everything.
$stage = Join-Path $root "stdlib\Gui\drivers\win-x64\static-sdl"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item build\libzan_gui_sdl_static.a (Join-Path $stage "libzan_gui.a") -Force
@(
    '# SDL3-backend zan_gui (game HUD compositor). Built by scripts/build_gui_sdl_static.ps1:',
    '# gui_runtime.c with -DZAN_GUI_STATIC -DZAN_GUI_SDL against the staged SDL3 devel package.',
    '# System libs come from the mingw runtime; SDL3 is the static devel archive.',
    '-lSDL3'
) | Set-Content -Encoding ascii (Join-Path $stage "zan_gui.libs")

Write-Output "GUI_SDL_STATIC_OK build\libzan_gui_sdl_static.a (+ staged static-sdl\libzan_gui.a)"

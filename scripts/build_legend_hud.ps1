# Builds the legend idle game with the GameHud (Gui control tree) compositor:
#   build\legend_hud.exe
#
# The HUD bridge (stdlib/Game/Ui/Hud.zan) imports zan_gui's scene compositor
# (zan_gui_adopt_sdl_window / zan_gui_scene_set_renderer / zan_gui_scene_upload
# / zan_gui_scene_present), which only exists in the SDL3-backend build of the
# gui runtime. zanc picks that archive up automatically from the staged
# static-sdl dir (stdlib/Gui/drivers/win-x64/static-sdl/libzan_gui.a, built by
# scripts/build_gui_sdl_static.ps1) via --link-lib zan_gui_sdl_static below;
# the Win32-backend static\libzan_gui.a lacks those exports.
param(
    [switch]$Run,
    [int]$HudShotFrames = 0   # >0 sets LEGEND_HUDSHOT=<n> for a debug capture
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$zanc = Join-Path $root "build\zanc.exe"
if (!(Test-Path -LiteralPath $zanc)) { throw "zanc not found: $zanc" }

$sdlStatic = Join-Path $root "build\libzan_gui_sdl_static.a"
if (!(Test-Path -LiteralPath $sdlStatic)) {
    Write-Output "libzan_gui_sdl_static.a missing - building it ..."
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build_gui_sdl_static.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build_gui_sdl_static.ps1 failed" }
}

# SDL3 devel package (headers for nothing here, but the static archive dir)
$cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $cache) { throw "SDL3 devel cache not found; run scripts\stage_sdl3.ps1 first." }
$sdlLibDir = Join-Path $cache.FullName ((Get-ChildItem $cache.FullName -Filter "SDL3-*" -Directory | Select-Object -First 1).Name)
$sdlLibDir = Join-Path $sdlLibDir "x86_64-w64-mingw32\lib"

Write-Output "[1/2] Compiling legend_hud.exe ..."
& $zanc `
    examples/game/legend/main.zan examples/game/legend/Game.zan examples/game/legend/Data.zan examples/game/common/GameKit.zan `
    --auto-stdlib `
    --embed examples/game/legend/assets=examples/game/legend/assets `
    --embed examples/game/common/assets=examples/game/common/assets `
    --libpath build `
    --link-lib zan_gui_sdl_static `
    --libpath "$sdlLibDir" `
    --link-lib SDL3 `
    --libpath (Join-Path $root "stdlib\SDL3\drivers\win-x64") `
    -o build/legend_hud.exe
if ($LASTEXITCODE -ne 0) { throw "legend_hud compile failed" }

Write-Output "[2/2] Staging SDL3 runtime DLLs ..."
foreach ($name in @("SDL3.dll")) {
    Copy-Item (Join-Path $root "stdlib\SDL3\drivers\win-x64\$name") (Join-Path $root "build\$name") -Force
}

Write-Output "LEGEND_HUD_OK build\legend_hud.exe"
if ($Run) {
    $env:LEGEND_HUDSHOT = if ($HudShotFrames -gt 0) { "$HudShotFrames" } else { $null }
    & (Join-Path $root "build\legend_hud.exe")
    if ($LASTEXITCODE -ne 0) { throw "legend_hud exited with code $LASTEXITCODE." }
}

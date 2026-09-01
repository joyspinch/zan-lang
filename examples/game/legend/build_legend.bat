@echo off
REM Build legend idle game with ALL ASSETS BAKED INTO the exe.
REM Usage: double-click this file. The project root is derived from this
REM script's own location, so it works no matter where the project lives.

cd /d "%~dp0..\..\.."
echo PROJECT ROOT: %CD%

REM ---- sanity: did we land in the right place? ----
if not exist "examples\game\legend\main.zan" (
    echo ERROR: project root not found from this script location.
    echo        main.zan is missing at examples\game\legend\main.zan
    pause
    exit /b 1
)

REM ---- sanity: is the Zan compiler present? ----
if not exist "build\zanc.exe" (
    echo ERROR: build\zanc.exe not found.
    echo        The Zan compiler must exist at build\zanc.exe to build the game.
    pause
    exit /b 1
)

REM ---- compile (assets embedded via --embed, ~12 MB exe) ----
echo.
echo [1/2] Compiling legend.exe with embedded assets ...
build\zanc.exe examples/game/legend/main.zan examples/game/legend/Game.zan examples/game/legend/Data.zan examples/game/common/GameKit.zan --auto-stdlib --embed examples/game/legend/assets=examples/game/legend/assets --embed examples/game/common/assets=examples/game/common/assets -o build/legend.exe
if errorlevel 1 (
    echo.
    echo BUILD FAILED (zanc exited with an error)
    pause
    exit /b 1
)
echo [OK] Compile done. Assets are INSIDE build\legend.exe.

REM ---- copy SDL runtime dlls next to the exe (required to launch) ----
echo [2/2] Ensuring SDL runtime dlls ...
if not exist "build\zan_sdl3.dll" (
    echo WARNING: build\zan_sdl3.dll missing - the exe may not start.
)

echo.
echo BUILD OK: build\legend.exe  (single exe, assets embedded)
echo Double-click build\legend.bat to play.
pause

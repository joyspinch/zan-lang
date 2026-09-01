@echo off
REM Build legend idle game.
REM Usage: double-click this file. The project root is derived from this
REM script's own location, so it works no matter where the project lives.
REM
REM Note: assets load from the repo source tree (examples/game/legend/assets)
REM via Assets.Find, same as ddz/gomoku/xiangqi. No copy step is required.

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

REM ---- compile ----
echo.
echo [1/2] Compiling legend.exe ...
build\zanc.exe examples/game/legend/main.zan examples/game/legend/Game.zan examples/game/legend/Data.zan examples/game/common/GameKit.zan --auto-stdlib -o build/legend.exe
if errorlevel 1 (
    echo.
    echo BUILD FAILED (zanc exited with an error)
    pause
    exit /b 1
)
echo [OK] Compile done.

REM ---- copy assets next to the exe (optional, for standalone distribution) ----
echo [2/2] Copying assets for standalone distribution ...
if not exist "build\assets\legend" mkdir "build\assets\legend"
xcopy /E /Y /Q "examples\game\legend\assets\*" "build\assets\legend\" >nul

if not exist "build\assets\common" mkdir "build\assets\common"
xcopy /E /Y /Q "examples\game\common\assets\*" "build\assets\common\" >nul

echo.
echo BUILD OK: build\legend.exe
echo Double-click build\legend.bat to play.
pause

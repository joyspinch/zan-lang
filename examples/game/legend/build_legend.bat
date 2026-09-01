@echo off
REM 编译传奇放置挂机并把资源复制到 exe 同级 assets 目录
REM 用法:双击此文件,或从任意目录调用它

cd /d D:\project\zan-lang

REM 编译
build\zanc.exe examples/game/legend/main.zan examples/game/legend/Game.zan examples/game/legend/Data.zan examples/game/common/GameKit.zan --auto-stdlib -o build/legend.exe
if errorlevel 1 (
    echo 编译失败
    pause
    exit /b 1
)

REM 把资源复制到 build/assets,与 legend.exe 同级
if not exist "build\assets\legend" mkdir "build\assets\legend"
xcopy /E /Y /Q "examples\game\legend\assets\*" "build\assets\legend\" >nul

if not exist "build\assets\common" mkdir "build\assets\common"
xcopy /E /Y /Q "examples\game\common\assets\*" "build\assets\common\" >nul

echo 构建完成: build\legend.exe
echo 资源目录: build\assets\
pause

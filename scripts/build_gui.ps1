# Builds build\gui_demo.exe as a SINGLE self-contained Windows app:
#   * native GUI runtime statically linked (no zan_gui.dll)
#   * no console window (/SUBSYSTEM:WINDOWS)
#   * embedded application icon (assets\zan.ico)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Output "[1/4] Compiling native GUI runtime (static)..."
clang -O2 -DZAN_GUI_STATIC -c src\runtime\gui_runtime.c -o build\zan_gui_static.obj
if ($LASTEXITCODE -ne 0) { throw "runtime compile failed" }
llvm-lib /out:build\zan_gui.lib build\zan_gui_static.obj | Out-Null
if ($LASTEXITCODE -ne 0) { throw "llvm-lib failed" }

Write-Output "[2/4] Compiling app icon resource..."
llvm-rc /fo build\zan_icon.res assets\zan.rc | Out-Null
if ($LASTEXITCODE -ne 0) { throw "llvm-rc failed" }

Write-Output "[3/4] Emitting LLVM IR from Zan sources..."
$files = @()
$files += (Get-ChildItem stdlib\Gui\*.zan).FullName
$files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
$files += (Join-Path $root "examples\gui_demo.zan")
Push-Location build
$ir = & .\zanc.exe --emit-ir $files
$code = $LASTEXITCODE
if ($code -eq 0) {
    [System.IO.File]::WriteAllLines((Join-Path (Get-Location) "gui_demo.ll"), $ir)
}
Pop-Location
if ($code -ne 0) { throw "zanc --emit-ir failed" }

Write-Output "[4/4] Linking standalone gui_demo.exe..."
Push-Location build
clang gui_demo.ll zan_icon.res -o gui_demo.exe -O2 `
    -Xlinker /STACK:268435456 `
    -Xlinker /SUBSYSTEM:WINDOWS `
    -Xlinker /ENTRY:mainCRTStartup `
    -lzan_gui
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { throw "link failed" }

Write-Output "Done: build\gui_demo.exe (standalone, no console, embedded icon)"

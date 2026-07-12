$ErrorActionPreference = "Stop"
Set-Location 'd:\project\zan-lang'

Write-Output "[0/2] Rebuilding native GUI runtime (multi-window)..."
clang -O2 -DZAN_GUI_STATIC -c src\runtime\gui_runtime.c -o build\zan_gui_static.obj
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_COMPILE_FAILED"; exit 1 }
llvm-lib /out:build\zan_gui.lib build\zan_gui_static.obj | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_LIB_FAILED"; exit 1 }

$files = @()
$files += (Get-ChildItem stdlib\Gui\*.zan).FullName
$files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
$files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
$files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
$files += (Join-Path (Get-Location) "stdlib\System\Diagnostics\Process.zan")
$files += (Join-Path (Get-Location) "src\ide_zan\ide_demo.zan")
Push-Location build
$ir = & .\zanc.exe --emit-ir $files
$code = $LASTEXITCODE
if ($code -eq 0) {
    [System.IO.File]::WriteAllLines((Join-Path (Get-Location) "ide_demo.ll"), $ir)
} else {
    Write-Output "IR_FAILED code=$code"
    $ir | Select-Object -Last 30
    Pop-Location
    exit 1
}
clang ide_demo.ll zan_icon.res -o ide_demo.exe -O2 -Xlinker /STACK:268435456 -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup -lzan_gui -llegacy_stdio_definitions
$lc = $LASTEXITCODE
Pop-Location
if ($lc -ne 0) { Write-Output "LINK_FAILED code=$lc"; exit 1 }
Write-Output "IDE_BUILD_OK"

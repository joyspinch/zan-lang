# Builds build\jsonbind.exe: the declarative JSON + two-way binding demo,
# a standalone windowed app (native GUI runtime statically linked).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

clang -O2 -DZAN_GUI_STATIC -c src\runtime\gui_runtime.c -o build\zan_gui_static.obj
if ($LASTEXITCODE -ne 0) { throw "runtime compile failed" }
llvm-lib /out:build\zan_gui.lib build\zan_gui_static.obj | Out-Null
if ($LASTEXITCODE -ne 0) { throw "llvm-lib failed" }
clang -O2 -std=c11 -I src\runtime -c src\runtime\rt_sync.c -o build\zanrt_sync_jsonbind.obj
if ($LASTEXITCODE -ne 0) { throw "rt_sync compile failed" }

$files = @()
$files += (Get-ChildItem stdlib\Gui\*.zan).FullName
$files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
$files += (Join-Path (Get-Location) "examples\json_ui_binding\json_ui_binding.zan")
Push-Location build
$ir = & .\zanc.exe --emit-ir $files
$code = $LASTEXITCODE
if ($code -eq 0) {
    [System.IO.File]::WriteAllLines((Join-Path (Get-Location) "jsonbind.ll"), $ir)
} else {
    Write-Output "IR_FAILED code=$code"
    Write-Output $ir
    Pop-Location
    exit 1
}
clang jsonbind.ll zanrt_sync_jsonbind.obj zan_icon.res -o jsonbind.exe -O2 -Xlinker /STACK:268435456 -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup -lzan_gui -llegacy_stdio_definitions
$lc = $LASTEXITCODE
Pop-Location
if ($lc -ne 0) { Write-Output "LINK_FAILED code=$lc"; exit 1 }
Write-Output "JSONBIND_BUILD_OK"

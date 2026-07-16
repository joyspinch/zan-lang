$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Output "[0/2] Rebuilding native GUI runtime (multi-window)..."
clang -O2 -DZAN_GUI_STATIC -c src\runtime\gui_runtime.c -o build\zan_gui_static.obj
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
            -Xlinker /ENTRY:mainCRTStartup -lzan_gui -llegacy_stdio_definitions
        if ($LASTEXITCODE -ne 0) { throw "LINK_FAILED code=$LASTEXITCODE" }
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

# Generates the self-contained `zan_gui` native driver bundle that lets zanc
# link GUI / WebView apps with its own bundled GNU ld -- no external clang or
# LLVM required (mirrors the SDL3 driver under stdlib\SDL3\drivers). The
# Win32-backend runtime DLL (build\zan_gui.dll, from the CMake zan_gui_runtime
# target) imports only system DLLs, so a linked GUI exe needs only zan_gui.dll
# beside it. zanc auto-discovers this via stdlib\Gui\drivers\driver.manifest.
#
# Run after `cmake --build build` (which produces build\zan_gui.dll).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$dll = Join-Path $root "build\zan_gui.dll"
if (!(Test-Path $dll)) {
    Write-Output "NO_ZAN_GUI_DLL (build the zan_gui_runtime CMake target first)"
    exit 1
}

$drvDir = Join-Path $root "stdlib\Gui\drivers\win-x64"
New-Item -ItemType Directory -Force -Path $drvDir | Out-Null

$man = Join-Path $root "stdlib\Gui\drivers\driver.manifest"
@"
# Native [DllImport] libraries owned by this module (one -l basename per line).
# blank lines and '#' comments are ignored. zanc discovers this file to bundle
# the driver on --publish; see src/compiler/main.c (zan_discover_drivers).
zan_gui
"@ | Set-Content -Encoding ascii $man

Copy-Item -LiteralPath $dll -Destination (Join-Path $drvDir "zan_gui.dll") -Force

# Build the GNU import library (libzan_gui.dll.a) from the DLL's export table.
$def = Join-Path $drvDir "zan_gui.def"
$dump = & llvm-objdump -p $dll
$names = @()
$inExports = $false
foreach ($line in $dump) {
    if ($line -match "Export Table:") { $inExports = $true; continue }
    if ($inExports) {
        $m = [regex]::Match($line, "^\s+\d+\s+0x[0-9a-fA-F]+\s+(\S+)\s*$")
        if ($m.Success) { $names += $m.Groups[1].Value }
    }
}
if ($names.Count -eq 0) { Write-Output "DEF_EMPTY"; exit 1 }
"LIBRARY zan_gui.dll`nEXPORTS" | Set-Content -Encoding ascii $def
$names | ForEach-Object { $_ } | Add-Content -Encoding ascii $def
& llvm-dlltool -m i386:x86-64 -d $def -l (Join-Path $drvDir "libzan_gui.dll.a") -D "zan_gui.dll"
if ($LASTEXITCODE -ne 0) { Write-Output "DLLTOOL_FAILED"; exit 1 }

Write-Output ("GUI_DRIVER_OK exports=" + $names.Count)

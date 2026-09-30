$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- Standard Zan GUI project build for the IDE ---------------------------
# src/ide_zan is a normal zan.proj project: the `entry` manifest key names the
# designed form (src/IdeForm.html), the GenForm generator projects it into a
# partial class with the program's Main, code-behind src/IdeForm.zan holds the
# logic and the rest of the shell/editor lives under src/. zanc compiles
# everything, drives the bundled linker, and links the static native GUI
# runtime (Win32 backend) + the embedded skin packs.

Write-Output "IDE_BUILD_START"

# ---- 0) clang on PATH ------------------------------------------------------
# Sandbox/background sessions run with a trimmed PATH that may lack both the
# mozbuild LLVM and the VS-bundled clang this repo builds with. Probe the two
# known install locations before failing: the script needs clang, llvm-ar and
# llvm-readobj, all in the same bin directory. NOTE: a bare "clang" command
# name is unreliable in some restricted sessions even with the dir prepended,
# so every native call below uses the full path via $clangExe / $clangDir.
$clangExe = ""
$clangHints = @(
    "$env:USERPROFILE\.mozbuild\clang\bin",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"
)
foreach ($hint in $clangHints) {
    if (Test-Path (Join-Path $hint "clang.exe")) {
        $clangExe = (Get-Item $hint).FullName + "\clang.exe"
        break
    }
}
if ($clangExe -eq "") {
    Write-Output "CLANG_NOT_FOUND (neither .mozbuild nor VS Llvm)"; exit 1
}
$clangDir = Split-Path -Parent $clangExe

# ---- 1) native GUI runtime (static, mingw ABI) ----------------------------
# Win32 native backend (ZAN_GUI_STATIC): the IDE shell, editor and
# all retained widgets are pure Win32. Compiled for zanc's own
# x86_64-w64-windows-gnu link ABI so it links straight through the compiler.
# Rebuilt on every run (the compiler driver auto-links the async reactor).
	$runtimeLib = Join-Path $root "build\libzan_gui_ide_gnu.a"
	& $clangExe --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
	    -c src\runtime\gui_runtime.c -o build\zan_gui_ide_gnu.o
	if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_COMPILE_FAILED"; exit 1 }
	$clangCxx = Join-Path $clangDir "clang++.exe"
	if (-not (Test-Path $clangCxx)) { $clangCxx = $clangExe }
	& $clangCxx --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
	    -fno-exceptions -fno-rtti `
	    -c src\runtime\gui_runtime_dwrite.cpp -o build\zan_gui_dwrite_ide_gnu.o
	if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_DWRITE_COMPILE_FAILED"; exit 1 }
	& (Join-Path $clangDir "llvm-ar.exe") rcs $runtimeLib build\zan_gui_ide_gnu.o build\zan_gui_dwrite_ide_gnu.o
	if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_LIB_FAILED"; exit 1 }

# ---- 2) embedded resources (skins, help topics, ide.css) -------------------
# Everything the IDE reads as data ships inside the exe and is looked up by name
# through zan_embed_*: skin packs, the Help page's topics.json and the
# page-layout stylesheet. Regenerate every build so editing one shows up without
# a manual step.
#
# All three groups go into ONE object on purpose: zan_embed_register replaces the
# registered table, so a second generated object would hide the first one's
# resources (that is how ide.css and topics.json used to need disk copies).
# The 'ide' group is filtered to *.css because assets\ also holds docs\.
& powershell -ExecutionPolicy Bypass -File scripts\gen_embed.ps1 `
    -Group "packages\Zan.Gui\src\Gui\skins=skins;src\ide_zan\assets\docs=docs;src\ide_zan\assets=ide:*.css" `
    -OutC build\embed_gen.c -OutO build\embed_gen.o -Clang $clangExe
if ($LASTEXITCODE -ne 0) { Write-Output "EMBED_GEN_FAILED"; exit 1 }

# ---- 3) sources: entry first, then project + Gui stdlib -------------------
$zanc = if (Test-Path "build\zanc.exe") { "build\zanc.exe" } else { "dist\win-x64\toolchain\zanc.exe" }
Write-Output "[zanc] $zanc"

$files = @()
# IDE sources (the standard project's own code). The IDE's own components
# register their kinds into HeavyControls at startup (IdeBootstrap.Install),
# so no build-time component registry is generated any more.
$files += (Get-ChildItem src\ide_zan\*.zan -Recurse).FullName
# The GUI stdlib is namespaced across subfolders (Gui root + Widget /
# Component / Designer); recurse so every part is compiled.
$files += (Get-ChildItem packages\Zan.Gui\src\Gui -Recurse -Include *.zan).FullName
if (Test-Path packages\Zan.Gui.Charts) {
    $files += (Get-ChildItem packages\Zan.Gui.Charts\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Gui.Browser) {
    $files += (Get-ChildItem packages\Zan.Gui.Browser\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Industrial) {
    $files += (Get-ChildItem packages\Zan.Industrial\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Data) {
    $files += (Get-ChildItem packages\Zan.Data\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Desktop) {
    $files += (Get-ChildItem packages\Zan.Desktop\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Security) {
    $files += (Get-ChildItem packages\Zan.Security\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Net) {
    $files += (Get-ChildItem packages\Zan.Net\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Diagnostics) {
    $files += (Get-ChildItem packages\Zan.Diagnostics\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Text) {
    $files += (Get-ChildItem packages\Zan.Text\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Linq) {
    $files += (Get-ChildItem packages\Zan.Linq\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Scripting) {
    $files += (Get-ChildItem packages\Zan.Scripting\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Knowledge) {
    $files += (Get-ChildItem packages\Zan.Knowledge\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Globalization) {
    $files += (Get-ChildItem packages\Zan.Globalization\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Resources) {
    $files += (Get-ChildItem packages\Zan.Resources\src -Recurse -Include *.zan).FullName
}
# System pieces the editor/workspace rely on.
$files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
$files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
$files += (Join-Path (Get-Location) "stdlib\System\IO\FileInfo.zan")
# The publish path archives a macOS/Linux output directory as tar (the only
# common format that carries the unix mode bit) -- see ZanIDE.Package.zan.
$files += (Join-Path (Get-Location) "stdlib\System\IO\Compression\Tar.zan")

# Entry FIRST: zanc gives the generated Main() to the first design document on
# the command line (genrun.c passes emitMain only for paths[0]), so the form
# named by the manifest's `entry` key has to lead the input list.
$entryRel = ([regex]::Match(
    [System.IO.File]::ReadAllText((Join-Path (Get-Location) "src\ide_zan\zan.proj")),
    '(?m)^\s*entry\s*=\s*(.+?)\s*$')).Groups[1].Value
if ($entryRel -eq "") { Write-Output "PROJECT_ENTRY_MISSING"; exit 1 }
$entry = Join-Path (Get-Location) (Join-Path "src\ide_zan" ($entryRel -replace '/', '\'))
if (!(Test-Path -LiteralPath $entry)) { Write-Output "PROJECT_ENTRY_NOT_FOUND $entry"; exit 1 }
Write-Output "[entry] $entry"

# Every designed document in the project is compiled, not just the entry: one
# .html per visual unit (window / panel / dialog / page) with a same-named
# code-behind. The entry is passed separately (first), so skip it here to
# avoid handing zanc the same path twice.
$designs = @(Get-ChildItem src\ide_zan -Recurse -Include *.html |
    Where-Object { $_.FullName -ne $entry } |
    ForEach-Object { $_.FullName })
if ($designs.Count -gt 0) {
    $files += $designs
    Write-Output ("[designs] " + ($designs.Count + 1))
}

$exeOut = Join-Path (Get-Location) "build\ZanIDE.exe"
$exeOld = Join-Path (Get-Location) "build\ZanIDE.prev.exe"

# The source list goes through a zanc @file response file: the explicit list
# crossed the Windows 32K command-line limit when Zan.Security joined the
# package groups ("The filename or extension is too long" from CreateProcess).
$rsp = Join-Path (Get-Location) "build\ide_files.rsp"
$files | ForEach-Object { '"' + $_ + '"' } | Set-Content -Path $rsp -Encoding UTF8

$zanArgs = @()
$zanArgs += $entry
$zanArgs += ("@" + $rsp)
$zanArgs += @("-o", $exeOut, "--subsystem", "windows")
# Release shape (--publish): Os codegen + the optimization sweep + a stripped
# link. The unoptimized dev default (O0 + fast_codegen) made the exe ~21 MB,
# of which 13.8 MB was .text alone; the same sources publish to ~14 MB. The
# debug ARC guards are off either way (below), so a long-running editor loses
# nothing it was using. Crash line numbers in build\zan_crash.log were the
# -g DWARF line tables: drop the crash-log source resolution for now; module
# +offset still lands there, and ZAN_IDE_ZANC_ARGS="-g" brings the tables back
# for a dedicated debug build when a crash needs triaging.
$zanArgs += @("--publish")
if ($env:IDE_NO_PUBLISH -eq "1") { $zanArgs = $zanArgs -ne "--publish" }
# ...but not the debug ARC guards --publish omits either (they were the -g
# defaults). --arc-guard never
# returns a released object to the allocator (it quarantines it so a stale
# retain/release traps), and --check-leaks adds an atomic pair to every
# allocation and release. In a long-running editor that reads as an
# ever-growing process and a permanent CPU tax. Ask for them explicitly with
# ZAN_IDE_ZANC_ARGS="--arc-guard --check-leaks" when chasing a leak.
$zanArgs += @("--no-arc-guard", "--no-check-leaks")
$zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_ide_gnu")
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_gen.o"))
# Native Win32 backend system deps (dwmapi/user32/gdi32/imm32 + reactor).
# ole32: zan_audio 的 WASAPI 设备枚举走 COM（CoInitializeEx/CoCreateInstance/
# CoTaskMemFree），静态驱动归档 libzan_gui_ide_gnu.a 直接引用这些符号；
# 缺 -lole32 整个链接失败、ZanIDE.exe 出不来（2026-09-09 实测）。
$zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
$zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
$zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32",
    "--link-lib", "ole32")
# (winpthread is not named here: zanc already links it as the static archive
# libwinpthread.a. Naming it makes ld prefer libwinpthread.dll.a instead, and
# the exe then needs libwinpthread_64-1.dll beside it -- a DLL the toolchain
# does not ship, so the IDE would not start on another machine.)
$zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4")
$zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
# Escape hatch for diagnostic builds: ZAN_IDE_ZANC_ARGS="--arc-guard" builds the
# IDE with extra compiler flags without editing this script.
if ($env:ZAN_IDE_ZANC_ARGS) {
    $zanArgs += ($env:ZAN_IDE_ZANC_ARGS -split '\s+' | Where-Object { $_ })
}

# A running IDE holds a lock on its own executable, so the linker cannot
# overwrite it ("Permission denied"). Windows does allow RENAMING a running
# image: park it aside (and clear the previous parked copy) so a rebuild never
# needs the editor to be closed first.
if (Test-Path -LiteralPath $exeOld) {
    Remove-Item -LiteralPath $exeOld -Force -ErrorAction SilentlyContinue
}
if (Test-Path -LiteralPath $exeOut) {
    Move-Item -LiteralPath $exeOut -Destination $exeOld -Force `
        -ErrorAction SilentlyContinue
}

# Under $ErrorActionPreference = "Stop" a redirected native stderr line is
# promoted to a terminating NativeCommandError, so a mere compiler warning
# aborted an otherwise successful build. Only the exit code decides here.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$out = & $zanc @zanArgs 2>&1
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($code -ne 0) {
    # Show every diagnostic, not just the tail: a failed build is read from
    # this output and a truncated list hides the first (usually root) error.
    $out | Select-String -Pattern 'error|warning' | ForEach-Object { $_.Line }
    $out | Select-Object -Last 10
    Write-Output "IDE_LINK_FAILED code=$code"
    exit 1
}
if ($env:ZAN_IDE_SHOW_OUT) { $out | ForEach-Object { Write-Output $_ } }

# The stylesheet is baked into the exe (embed_gen above) and read from memory;
# a copy beside the executable would only shadow it, so drop any stale one --
# and the dev build then exercises the same path the release ships.
Remove-Item -LiteralPath (Join-Path (Get-Location) "build\ide.css") `
    -Force -ErrorAction SilentlyContinue

# ---- record the non-system DLLs the IDE needs beside it -------------------
# ZanIDE.exe imports more than the Windows API: the runtime objects pull in
# OpenSSL and SQLite, and zanc stages driver DLLs (zan_gui, WebView2Loader)
# next to the exe. A release that ships the exe without them dies on launch
# with "cannot find <dll>" on any machine that has no copy on PATH -- which is
# every machine but this one. Read the truth from the exe's own import table
# (llvm-readobj is already required by this script for clang/llvm-ar) and let
# publish_ide.ps1 ship exactly that list.
$sysDll = @(
    'kernel32','kernelbase','user32','gdi32','gdiplus','advapi32','shell32',
    'shlwapi','shcore','ole32','oleaut32','comctl32','comdlg32','crypt32',
    'ws2_32','mswsock','iphlpapi','psapi','rpcrt4','dwmapi','imm32','uxtheme',
    'winmm','version','dbghelp','setupapi','bcrypt','ncrypt','secur32',
    'wintrust','userenv','netapi32','powrprof','ntdll','msvcrt','msimg32',
    'winspool','wtsapi32','dnsapi','normaliz','usp10','opengl32','d3d11',
    'dxgi','avrt','propsys','windowscodecs'
)
# Dynamically loaded ones never show up in the import table (WebView2Loader is
# LoadLibrary'd on demand), so union in whatever zanc reported staging.
$staged = @($out | ForEach-Object {
    $m = [regex]::Match([string]$_, "^\s*bundled driver '[^']+' \S+ (?<f>\S+\.dll)\s*$")
    if ($m.Success) { $m.Groups['f'].Value }
})
$imports = @(& (Join-Path $clangDir "llvm-readobj.exe") --coff-imports (Join-Path (Get-Location) "build\ZanIDE.exe") 2>&1 |
    ForEach-Object {
        $m = [regex]::Match([string]$_, '^\s*Name:\s*(?<f>\S+\.dll)\s*$')
        if ($m.Success) { $m.Groups['f'].Value }
    } | Sort-Object -Unique)
if ($imports.Count -eq 0) {
    Write-Output "IDE_DEPS_WARN: could not read the import table (llvm-readobj missing?); build\ZanIDE.deps.txt left as-is"
} else {
    $deps = @($imports | Where-Object {
        $base = [IO.Path]::GetFileNameWithoutExtension($_).ToLower()
        ($sysDll -notcontains $base) -and ($base -notlike 'api-ms-*') -and
        ($base -notlike 'ext-ms-*')
    })
    $deps = @(($deps + $staged) | Sort-Object -Unique)
    foreach ($dep in $deps) {
        if (-not (Test-Path (Join-Path (Get-Location) "build\$dep"))) {
            Write-Output "IDE_DEPS_WARN: $dep is imported but not in build\ (the release cannot ship it)"
        }
    }
    [System.IO.File]::WriteAllLines(
        (Join-Path (Get-Location) "build\ZanIDE.deps.txt"), $deps)
    Write-Output ("[deps] " + ($deps -join " "))
}

Write-Output "IDE_BUILD_OK build\ZanIDE.exe"

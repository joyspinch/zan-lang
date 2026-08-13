$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- Standard Zan GUI project build for the IDE ---------------------------
# src/ide_zan is a normal zan.proj project: the `entry` manifest key names the
# designed form (src/IdeForm.zform), the GenForm generator projects it into a
# partial class with the program's Main, code-behind src/IdeForm.zan holds the
# logic and the rest of the shell/editor lives under src/. zanc compiles
# everything, drives the bundled linker, and links the static native GUI
# runtime (Win32 backend) + the embedded skin packs.

Write-Output "IDE_BUILD_START"

# ---- 1) native GUI runtime (static, mingw ABI) ----------------------------
# Win32 native backend (ZAN_GUI_STATIC, no SDL3): the IDE shell, editor and
# all retained widgets are pure Win32. Compiled for zanc's own
# x86_64-w64-windows-gnu link ABI so it links straight through the compiler.
# Rebuilt on every run (the compiler driver auto-links the async reactor), so
# a stale SDL-backed archive from an older build cannot leak in.
$runtimeLib = Join-Path $root "build\libzan_gui_ide_gnu.a"
clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
    -c src\runtime\gui_runtime.c -o build\zan_gui_ide_gnu.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_COMPILE_FAILED"; exit 1 }
llvm-ar rcs $runtimeLib build\zan_gui_ide_gnu.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_LIB_FAILED"; exit 1 }

# ---- 2) embedded resources (skins) ----------------------------------------
# The skin packs ship inside the exe: bake stdlib/Gui/skins into an object the
# runtime's zan_embed_* API reads by name. Regenerate every build so a skin
# edit shows up without a manual step.
& powershell -ExecutionPolicy Bypass -File scripts\gen_embed.ps1 `
    -Root stdlib\Gui\skins -Prefix skins `
    -OutC build\embed_gen.c -OutO build\embed_gen.o -Clang clang
if ($LASTEXITCODE -ne 0) { Write-Output "EMBED_GEN_FAILED"; exit 1 }

# The IDE's own text assets (the Help page's topics.json) ship the same way:
# a second embed object registers its files with the same runtime registry, so
# the documentation is available offline without an external data file.
& powershell -ExecutionPolicy Bypass -File scripts\gen_embed.ps1 `
    -Root src\ide_zan\assets\docs -Prefix docs `
    -OutC build\embed_docs.c -OutO build\embed_docs.o -Clang clang
if ($LASTEXITCODE -ne 0) { Write-Output "EMBED_DOCS_FAILED"; exit 1 }

# ---- 3) sources: entry first, then project + Gui stdlib -------------------
$zanc = if (Test-Path "build\zanc.exe") { "build\zanc.exe" } else { "dist\win-x64\toolchain\zanc.exe" }
Write-Output "[zanc] $zanc"

$registryPath = Join-Path (Get-Location) "build\ProjectComponents.ide.zan"
& powershell -ExecutionPolicy Bypass -File scripts\scan_components.ps1 `
    -Source "src\ide_zan" -Out "build\ProjectComponents.ide.zan"
if ($LASTEXITCODE -ne 0) { Write-Output "COMPONENT_SCAN_FAILED"; exit 1 }
# An empty registry links fine and fails only at runtime: UiNode.Make yields a
# null node for every `type` it cannot resolve, so each declarative child
# window silently loses its title bar, its cards and the whole docs page while
# the hand-drawn dialogs still look right. Refuse to ship that build instead
# of leaving it to be spotted on screen.
$registryText = [System.IO.File]::ReadAllText($registryPath)
$componentCount = ([regex]::Matches($registryText, 'k\.Add\(')).Count
if ($componentCount -lt 1) {
    Write-Output "COMPONENT_REGISTRY_EMPTY"; exit 1
}

$files = @()
# IDE sources (the standard project's own code).
$files += (Get-ChildItem src\ide_zan\*.zan -Recurse).FullName
$files += $registryPath
# The GUI stdlib is namespaced across subfolders (Gui root + Widget /
# Component / Designer / Hmi); recurse so every part is compiled.
$files += (Get-ChildItem stdlib\Gui -Recurse -Include *.zan |
    Where-Object { $_.Name -ne "ProjectComponents.zan" }).FullName
# System pieces the editor/workspace rely on.
$files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
$files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
$files += (Join-Path (Get-Location) "stdlib\System\IO\FileInfo.zan")
$files += (Join-Path (Get-Location) "stdlib\System\Diagnostics\Process.zan")
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
# .zform per visual unit (window / panel / dialog / page) with a same-named
# code-behind. The entry is passed separately (first), so skip it here to
# avoid handing zanc the same path twice.
$designs = @(Get-ChildItem src\ide_zan -Recurse -Include *.zform |
    Where-Object { $_.FullName -ne $entry } |
    ForEach-Object { $_.FullName })
if ($designs.Count -gt 0) {
    $files += $designs
    Write-Output ("[designs] " + ($designs.Count + 1))
}

$zanArgs = @()
$zanArgs += $entry
$zanArgs += $files
$zanArgs += @("-DZAN_PROJECT_COMPONENTS")
$zanArgs += @("-o", "build\ZanIDE.exe", "--subsystem", "windows")
# DWARF line tables: the crash handler resolves every in-module frame to
# `function  <file>.zan:line` in build\zan_crash.log, so a dev build always
# says which source line faulted instead of module+offset.
$zanArgs += @("-g")
# ...but not the debug ARC guards -g turns on by default. --arc-guard never
# returns a released object to the allocator (it quarantines it so a stale
# retain/release traps), and --check-leaks adds an atomic pair to every
# allocation and release. In a long-running editor that reads as an
# ever-growing process and a permanent CPU tax. Ask for them explicitly with
# ZAN_IDE_ZANC_ARGS="--arc-guard --check-leaks" when chasing a leak.
$zanArgs += @("--no-arc-guard", "--no-check-leaks")
$zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_ide_gnu")
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_gen.o"))
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_docs.o"))
# Native Win32 backend system deps (dwmapi/user32/gdi32/imm32 + reactor).
$zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
$zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
$zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
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
$exeOut = Join-Path (Get-Location) "build\ZanIDE.exe"
$exeOld = Join-Path (Get-Location) "build\ZanIDE.prev.exe"
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

# The IDE's own stylesheet (page layout) ships next to the executable.
Copy-Item -LiteralPath (Join-Path (Get-Location) "src\ide_zan\assets\ide.css") `
    -Destination (Join-Path (Get-Location) "build\ide.css") -Force

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
$imports = @(& llvm-readobj --coff-imports (Join-Path (Get-Location) "build\ZanIDE.exe") 2>&1 |
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

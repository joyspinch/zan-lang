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
$zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_ide_gnu")
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_gen.o"))
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_docs.o"))
# Native Win32 backend system deps (dwmapi/user32/gdi32/imm32 + reactor).
$zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
$zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
$zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
$zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4", "--link-lib", "winpthread")
$zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))

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
    $out | Select-Object -Last 60
    Write-Output "IDE_LINK_FAILED code=$code"
    exit 1
}

# The IDE's own stylesheet (page layout) ships next to the executable.
Copy-Item -LiteralPath (Join-Path (Get-Location) "src\ide_zan\ide.css") `
    -Destination (Join-Path (Get-Location) "build\ide.css") -Force `
    -ErrorAction SilentlyContinue

Write-Output "IDE_BUILD_OK build\ZanIDE.exe"

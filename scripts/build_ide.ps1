$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- SDL3 windowing backend (unified with Game.*) -------------------------
# The native GUI runtime is an SDL3 window driven by the same stack games use.
# Locate the staged SDL3 mingw devel package (headers + libSDL3.dll.a import
# library) - it matches zanc's own x86_64-w64-windows-gnu link ABI.
$driverDir = Join-Path $root "stdlib\SDL3\drivers\win-x64"
$cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory `
    | Sort-Object Name -Descending | Select-Object -First 1
if (-not $cache) {
    Write-Output "SDL3 devel package not staged; running stage_sdl3.ps1 ..."
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "stage_sdl3.ps1")
    if ($LASTEXITCODE -ne 0) { Write-Output "SDL3_STAGE_FAILED"; exit 1 }
    $cache = Get-ChildItem "$env:TEMP" -Filter "zan-sdl3-*" -Directory `
        | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $cache) { Write-Output "SDL3_STAGE_MISSING"; exit 1 }
}
$pkg = Join-Path $cache.FullName ((Get-ChildItem $cache.FullName -Filter "SDL3-*" -Directory | Select-Object -First 1).Name)
$pkg = Join-Path $pkg "x86_64-w64-mingw32"
$inc = Join-Path $pkg "include"
$sdlLibDir = Join-Path $pkg "lib"
# The IDE links SDL3 statically (no SDL3.dll beside the exe); the static
# archive is built from source by stage_sdl3.ps1 the first time.
$sdlStatic = Join-Path $sdlLibDir "libSDL3.a"
if (-not (Test-Path -LiteralPath $sdlStatic)) {
    Write-Output "Static libSDL3.a not staged; running stage_sdl3.ps1 ..."
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "stage_sdl3.ps1")
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sdlStatic)) {
        Write-Output "SDL3_STATIC_STAGE_FAILED"; exit 1
    }
}
# Copy under a distinct name so ld cannot pick the import lib (libSDL3.dll.a)
# that sits in the same staged directory.
Copy-Item -LiteralPath $sdlStatic -Destination "build\libSDL3_s.a" -Force

# ---- native GUI runtime (static, mingw ABI) -------------------------------
# Compiled for zanc's own x86_64-w64-windows-gnu link ABI so it can be linked
# straight through the compiler (which drives the bundled ld + MinGW runtime).
# Static (ZAN_GUI_STATIC) so the IDE carries no zan_gui.dll dependency.
Write-Output "[0/2] Building native GUI runtime (SDL3, static, mingw ABI)..."
clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC -DZAN_GUI_SDL "-I$inc" `
    -c src\runtime\gui_runtime.c -o build\zan_gui_ide_gnu.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_COMPILE_FAILED"; exit 1 }
# Generic embedded-resource read API (skins/DB drivers/etc. are pulled from the
# exe by name at runtime); the data comes from a generated object linked below.
clang --target=x86_64-w64-windows-gnu -O2 `
    -c src\runtime\zan_embed_api.c -o build\zan_embed_api.o
if ($LASTEXITCODE -ne 0) { Write-Output "EMBED_API_COMPILE_FAILED"; exit 1 }
llvm-ar rcs build\libzan_gui_ide_gnu.a build\zan_gui_ide_gnu.o build\zan_embed_api.o
if ($LASTEXITCODE -ne 0) { Write-Output "RUNTIME_LIB_FAILED"; exit 1 }

# Bake the skin packs (skin.css + base.css + artwork) into the IDE as embedded
# resources so they ship inside ZanIDE.exe -- no external skins\ folder needed.
& powershell -ExecutionPolicy Bypass -File scripts\gen_embed.ps1 `
    -Root stdlib\Gui\skins -Prefix skins `
    -OutC build\embed_gen.c -OutO build\embed_gen.o -Clang clang
if ($LASTEXITCODE -ne 0) { Write-Output "EMBED_GEN_FAILED"; exit 1 }


$zanc = if (Test-Path "build\zanc.exe") { "build\zanc.exe" } else { "dist\win-x64\toolchain\zanc.exe" }
Write-Output "[zanc] $zanc"

$registryPath = Join-Path $root "stdlib\Gui\CustomComponents.zan"
$registryOriginal = [System.IO.File]::ReadAllText($registryPath)
$failed = $false

try {
    & powershell -ExecutionPolicy Bypass -File scripts\scan_components.ps1 `
        -Source src\ide_zan\components -Out stdlib\Gui\CustomComponents.zan
    if ($LASTEXITCODE -ne 0) { throw "SCAN_FAILED" }

    $files = @()
    # The GUI stdlib is namespaced across subfolders (Gui root + Widget /
    # Component/* / Backend / Designer); recurse so every part is compiled.
    $files += (Get-ChildItem stdlib\Gui -Recurse -Include *.zan).FullName
    $files += (Get-ChildItem src\ide_zan\components\*.zan).FullName
    if (Test-Path src\ide_zan\views) {
        $viewFiles = Get-ChildItem src\ide_zan\views\*.zan -ErrorAction SilentlyContinue
        if ($viewFiles) { $files += $viewFiles.FullName }
    }
    $files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
    # File timestamps: the editor uses them to notice outside changes.
    $files += (Join-Path (Get-Location) "stdlib\System\IO\FileInfo.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\Diagnostics\Process.zan")
    $files += (Get-ChildItem stdlib\Game\Scene\*.zan).FullName
    # Crypto + encrypted resource pack (.zrp publishing from the Asset Manager).
    $files += (Get-ChildItem stdlib\System\Security\Cryptography\*.zan).FullName
    $files += (Join-Path (Get-Location) "stdlib\System\Resources\ResourcePack.zan")
    # All IDE shell sources: ZanIDE.zan + its partial-class parts,
    # SceneDesigner, AssetManager and the session classes.
    $files += (Get-ChildItem src\ide_zan\*.zan).FullName

    # Link the IDE straight through zanc: it compiles all sources and drives the
    # bundled ld itself, auto-linking the socket-async reactor (rt_io) and the
    # atomic/sync runtime (rt_sync) as needed - no hand-compiled runtime objects.
    # 256 MB stack + WINDOWS subsystem are applied by zanc's own link path.
    $zanArgs = @()
    $zanArgs += $files
    $zanArgs += @("-o", "build\ZanIDE.exe", "--subsystem", "windows")
    $zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_ide_gnu")
    $zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_gen.o"))
    $zanArgs += @("--link-lib", "SDL3_s")
    $zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
    $zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
    $zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
    # Static SDL3 needs its private system deps (sdl3.pc Libs.private).
    $zanArgs += @("--link-lib", "user32", "--link-lib", "winmm")
    $zanArgs += @("--link-lib", "ole32", "--link-lib", "oleaut32")
    $zanArgs += @("--link-lib", "version", "--link-lib", "uuid")
    $zanArgs += @("--link-lib", "setupapi", "--link-lib", "shell32")
    $zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
    # A running IDE holds a lock on its own executable, so the linker cannot
    # overwrite it ("Permission denied"). Windows does allow RENAMING a running
    # image: park it aside (and clear the previous parked copy, which is only
    # deletable once that older process exited) so a rebuild never needs the
    # editor to be closed first.
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
    # (e.g. the unbundled sqlite3 driver) aborted an otherwise successful
    # build. Only the exit code decides here.
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & $zanc @zanArgs 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    if ($code -ne 0) {
        $out | Select-Object -Last 40
        throw "IDE_LINK_FAILED code=$code"
    }
    # SDL3 is statically linked: make sure no stale SDL3.dll shadows that fact.
    Remove-Item -LiteralPath (Join-Path (Get-Location) "build\SDL3.dll") `
        -Force -ErrorAction SilentlyContinue
    # The IDE's own stylesheet (page layout) ships next to the executable.
    Copy-Item -LiteralPath (Join-Path (Get-Location) "src\ide_zan\ide.css") `
        -Destination (Join-Path (Get-Location) "build\ide.css") -Force
    # Skin packs are baked into the exe (embed_gen above); the on-disk copy
    # beside the executable is redundant, so drop any stale one.
    Remove-Item -LiteralPath (Join-Path (Get-Location) "build\skins") `
        -Recurse -Force -ErrorAction SilentlyContinue
} catch {
    Write-Output $_
    $failed = $true
} finally {
    [System.IO.File]::WriteAllText($registryPath, $registryOriginal)
}

if ($failed) { exit 1 }

# Single-file packing needs the packer beside zanc (ZanIDE.CanPackSingle), the
# same layout publish_ide.ps1 ships. Without it a dev build silently publishes
# loose DLLs even with "single-file exe" checked in the project.
$tc = Join-Path (Get-Location) "build\toolchain"
New-Item -ItemType Directory -Force -Path $tc | Out-Null
Copy-Item -LiteralPath (Join-Path (Get-Location) "scripts\pack_single.ps1") `
    -Destination (Join-Path $tc "pack_single.ps1") -Force
$stub = Join-Path $tc "pkg_stub.exe"
$stubSrc = Join-Path (Get-Location) "scripts\pkg_stub.c"
if (-not (Test-Path $stub) -or
    (Get-Item $stubSrc).LastWriteTime -gt (Get-Item $stub).LastWriteTime) {
    & clang -O2 -mwindows $stubSrc -o $stub
    if ($LASTEXITCODE -ne 0) { Write-Output "PKG_STUB_BUILD_FAILED"; exit 1 }
}

Write-Output "IDE_BUILD_OK"

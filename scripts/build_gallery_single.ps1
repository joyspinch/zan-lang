# build_gallery_single.ps1 -- Publish gui_gallery as a TRUE single file, the
# same way the IDE ships (ZanIDE.exe): the native GUI runtime statically
# linked, and the skin packs (base.css + all skin.css) embedded inside the exe.
# The result has no DLL beside it and no sibling folders -- exactly like a .NET
# single-file publish, and unlike the old pkg_stub self-extracting launcher
# (which unpacked the whole payload to %LOCALAPPDATA%\ZanGames and ran the
# program from there).
#
# The gallery is a pure Gui stdlib program: on Windows its window shell is the
# Zan-side Win32Shell (Native.zan #if WINDOWS), so the native Win32 GUI runtime
# is enough -- no SDL3 is linked (the IDE's SDL3 static link is a game/IDE
# thing, the gallery does not need it).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\build_gallery_single.ps1
# Output: build\gui_gallery_dist\gui_gallery.exe   (single file, no deps)
#
# Prerequisites:
#   - clang / llvm-ar on PATH (LLVM toolchain)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- native GUI runtime + embed API (static, mingw ABI, Win32 backend) ----
Write-Output "[1/4] Building static native GUI runtime + embed API ..."
clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
    -c src\runtime\gui_runtime.c -o build\zan_gui_gallery_gnu.o
if ($LASTEXITCODE -ne 0) { throw "RUNTIME_COMPILE_FAILED" }
clang --target=x86_64-w64-windows-gnu -O2 `
    -c src\runtime\zan_embed_api.c -o build\zan_embed_api.o
if ($LASTEXITCODE -ne 0) { throw "EMBED_API_COMPILE_FAILED" }
llvm-ar rcs build\libzan_gui_gallery_gnu.a build\zan_gui_gallery_gnu.o build\zan_embed_api.o
if ($LASTEXITCODE -ne 0) { throw "RUNTIME_LIB_FAILED" }

# ---- bake the skin packs (base.css + skins) into the exe -----------------
Write-Output "[2/4] Embedding skin packs (base.css + all skins) ..."
& powershell -ExecutionPolicy Bypass -File scripts\gen_embed.ps1 `
    -Root stdlib\Gui\skins -Prefix skins `
    -OutC build\embed_gen_gallery.c -OutO build\embed_gen_gallery.o -Clang clang
if ($LASTEXITCODE -ne 0) { throw "EMBED_GEN_FAILED" }

# ---- static driver dir so zanc's --link-mode static resolves zan_gui ----
$staticDriver = Join-Path $root "build\static_driver\static"
New-Item -ItemType Directory -Path $staticDriver -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root "build\libzan_gui_gallery_gnu.a") `
    -Destination (Join-Path $staticDriver "libzan_gui.a") -Force

# ---- compile + link through zanc (its own bundled ld) --------------------
Write-Output "[3/4] Compiling and linking gui_gallery (static, single file) ..."
$files = @()
$files += (Get-ChildItem stdlib\Gui\*.zan).FullName
$files += (Get-ChildItem stdlib\Gui\Widget\*.zan).FullName
$files += (Get-ChildItem examples\gui_gallery\components\*.zan).FullName
$files += (Join-Path (Get-Location) "examples\gui_gallery\gui_gallery.zan")

$outDir = Join-Path $root "build\gui_gallery_dist"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$outExe = Join-Path $outDir "gui_gallery.exe"

$zanArgs = @()
$zanArgs += $files
$zanArgs += @("-o", $outExe, "--subsystem", "windows")
$zanArgs += @("--link-mode", "static", "--driver-dir", (Join-Path $root "build\static_driver"))
# Demo/props/events catalog + map geometry + photos (assets/) travel inside
# the exe; File.ReadAllText falls back to the embedded copy when the loose
# file is not next to the binary (the single-file output has no siblings).
$zanArgs += @("--embed", "examples\gui_gallery\assets=assets")
$zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_gallery_gnu")
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_gen_gallery.o"))
# Native Win32 backend needs only the system libs it imports directly (the
# runtime's #pragma libs: dwmapi/user32/gdi32/imm32) plus the reactor deps.
$zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
$zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
$zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
$zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4")
$zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
$out = & build\zanc.exe @zanArgs 2>&1
$code = $LASTEXITCODE
if ($code -ne 0) {
    $out | Select-Object -Last 40
    throw "GALLERY_LINK_FAILED code=$code"
}

# ---- verify: no bundled dll beside the exe -------------------------------
Write-Output "[4/4] Verifying single-file output ..."
$loose = Get-ChildItem $outDir -Filter *.dll -ErrorAction SilentlyContinue
if ($loose) {
    foreach ($l in $loose) { Remove-Item $l.FullName -Force }
}
Write-Output ("GALLERY_SINGLE_OK -> " + $outExe + " (" + (Get-Item $outExe).Length + " bytes, no DLL deps)")

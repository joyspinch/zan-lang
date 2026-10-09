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
# is enough -- no SDL3 is linked (the IDE's static link is a game/IDE
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

# ---- independent native drivers (static, mingw ABI, Win32 backend) --------
Write-Output "[1/4] Building static native drivers ..."
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
    -Drivers @("zan_gui", "zan_image", "zan_audio") -StageRoot "build\static_driver"

# ---- bake the skin packs (base.css + skins) into the exe -----------------
Write-Output "[2/4] Embedding skin packs (base.css + all skins) ..."
& powershell -ExecutionPolicy Bypass -File scripts\gen_embed.ps1 `
    -Root packages\Zan.Gui\src\Gui\skins -Prefix skins `
    -OutC build\embed_gen_gallery.c -OutO build\embed_gen_gallery.o -Clang clang
if ($LASTEXITCODE -ne 0) { throw "EMBED_GEN_FAILED" }

# ---- static driver dir so zanc's --link-mode static resolves zan_gui ----
# --driver-dir REPLACES the per-module stdlib driver dirs for every driver
# the program links, not just zan_gui. The gallery deliberately does NOT
# stage the ssl/crypto static archives here: the only https entry point is
# ImageHttp -> HttpClient.CreateHttps, and the catalog demo fetches images
# from a local http://127.0.0.1 server, so no TlsStream call site is live.
# Without the archives, zanc's static-publish stub kicks in: every
# [DllImport("ssl"/"crypto")] import is stubbed (calls fail at run time) and
# -lssl/-lcrypto is dropped from the link line, keeping ~5.5 MB of OpenSSL
# out of the exe. A program that DOES use https stages the archives (or
# publishes shared) and links the real thing.
# The unified recipe already staged GUI/image/audio archives and their .libs
# into build/static_driver/static. Runtime/timer/embed objects are zanc-owned.

# ---- compile + link through zanc (its own bundled ld) --------------------
Write-Output "[3/4] Compiling and linking gui_gallery (static, single file) ..."
$files = @()
$files += (Get-ChildItem packages\Zan.Gui\src\Gui\*.zan).FullName
$files += (Get-ChildItem packages\Zan.Gui\src\Gui\Widget\*.zan).FullName
$files += (Get-ChildItem examples\gui_gallery\components\*.zan).FullName
$files += (Join-Path (Get-Location) "examples\gui_gallery\gui_gallery.zan")

$outDir = Join-Path $root "build\gui_gallery_dist"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$outExe = Join-Path $outDir "gui_gallery.exe"

$zanArgs = @()
$zanArgs += $files
$zanArgs += @("-o", $outExe, "--subsystem", "windows")
# Optimized release: --publish (strip + --gc-sections) with an explicit -Oz.
# --publish alone means -Os; -Oz overrides it (main.c honors an explicit -O
# over the publish default) and buys roughly a tenth of the Gui stdlib text.
$zanArgs += @("--publish", "-Oz")
$zanArgs += @("--link-mode", "static", "--driver-dir", (Join-Path $root "build\static_driver"))
# Demo/props/events catalog + map geometry + photos (assets/) travel inside
# the exe; File.ReadAllText falls back to the embedded copy when the loose
# file is not next to the binary (the single-file output has no siblings).
$zanArgs += @("--embed", "examples\gui_gallery\assets=assets")
$zanArgs += @("--auto-stdlib")
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_gen_gallery.o"))
# Native driver system dependencies come from the staged .libs files;
# keep the existing runtime/process dependencies for the gallery link.
$zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
$zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
$zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
$zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4")
$zanArgs += @("--link-lib", "ole32")
$zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
# EAP=Stop makes PowerShell promote zanc's stderr *notes* (redirected via
# 2>&1) into terminating NativeCommandErrors, killing the script mid-link
# and orphaning the zanc process. Downgrade around the native call.
$ErrorActionPreference = "Continue"
$out = & build\zanc.exe @zanArgs 2>&1
$code = $LASTEXITCODE
$ErrorActionPreference = "Stop"
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

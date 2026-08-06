param(
    [string]$Version = "5.4.6",
    [string]$Compiler = "",
    [string]$Dest = ""
)

# Build the Lua runtime that System.Scripting.Lua loads at run time and stage it
# into the module's driver directory,
# stdlib\System\Scripting\drivers\win-x64 (and into build\, so dev builds
# resolve it beside the executables zanc produces there).
#
# Lua ships source only, so the DLL is compiled here with MinGW instead of
# downloading a third-party binary: the same sources produce the Linux/macOS
# libraries in stage_lua.sh, so all three platforms run the identical 5.4 ABI
# that Lua.zan resolves symbols against.

$ErrorActionPreference = "Stop"

if (-not $Compiler) {
    $Compiler = "C:\TDM-GCC-64\bin\x86_64-w64-mingw32-gcc.exe"
    if (-not (Test-Path -LiteralPath $Compiler)) {
        $cc = Get-Command x86_64-w64-mingw32-gcc, gcc -ErrorAction SilentlyContinue |
              Select-Object -First 1
        if ($cc) { $Compiler = $cc.Source }
    }
}
if (!(Test-Path -LiteralPath $Compiler)) {
    throw "MinGW compiler not found: $Compiler"
}

$root = Split-Path -Parent $PSScriptRoot
if (-not $Dest) {
    $Dest = Join-Path $root "stdlib\System\Scripting\drivers\win-x64"
}
$work = Join-Path $env:TEMP "zan-lua-$Version"
$archive = Join-Path $work "lua-$Version.tar.gz"
$srcDir = Join-Path $work "lua-$Version\src"

New-Item -ItemType Directory -Force -Path $work, $Dest | Out-Null

if (!(Test-Path -LiteralPath $archive)) {
    Write-Output "Downloading Lua $Version..."
    # curl.exe, not Invoke-WebRequest: the cmdlet silently writes a truncated
    # file when the transfer is interrupted, and the failure only shows up as a
    # corrupt archive later.
    & curl.exe -fsSL --retry 3 -o $archive "https://www.lua.org/ftp/lua-$Version.tar.gz"
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $archive -ErrorAction SilentlyContinue
        throw "download failed: lua-$Version.tar.gz"
    }
}
if (!(Test-Path -LiteralPath $srcDir)) {
    Write-Output "Extracting Lua $Version..."
    Push-Location $work
    tar -xzf $archive
    Pop-Location
}

# lua.c / luac.c are the CLI drivers; both define main() and must stay out of
# the shared library.
$sources = Get-ChildItem -LiteralPath $srcDir -Filter *.c |
           Where-Object { $_.Name -ne "lua.c" -and $_.Name -ne "luac.c" } |
           ForEach-Object { $_.Name }

$dll = Join-Path $Dest "lua54.dll"
Push-Location $srcDir
try {
    & $Compiler -O2 -DLUA_BUILD_AS_DLL -shared -o $dll @sources
    if ($LASTEXITCODE -ne 0) { throw "lua build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

# The bundle manifest is what zanc copies next to a published executable.
Set-Content -LiteralPath (Join-Path $Dest "lua.bundle") -Encoding ascii `
            -Value "lua54.dll"

# Dev convenience: programs look beside their own exe, which for repo builds is
# build\.
$devDir = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $devDir | Out-Null
Copy-Item $dll $devDir -Force

Write-Output "STAGE_LUA_OK $dll ($((Get-Item $dll).Length) bytes)"

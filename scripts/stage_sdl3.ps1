param(
    [string]$Version = "3.4.12",
    [string]$Compiler = ""
)

$ErrorActionPreference = "Stop"

# Resolve the MinGW C compiler used to build the SDL3 bridge DLL. Prefer an
# explicit -Compiler, then a local TDM-GCC install, then any MinGW gcc on PATH
# (e.g. the msys2/choco mingw used in CI).
if (-not $Compiler) {
    $Compiler = "C:\TDM-GCC-64\bin\x86_64-w64-mingw32-gcc.exe"
    if (-not (Test-Path -LiteralPath $Compiler)) {
        $cc = Get-Command x86_64-w64-mingw32-gcc, gcc -ErrorAction SilentlyContinue |
              Select-Object -First 1
        if ($cc) { $Compiler = $cc.Source }
    }
}

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root "stdlib\SDL3\native\zan_sdl3.c"
$driverDir = Join-Path $root "stdlib\SDL3\drivers\win-x64"
$work = Join-Path $env:TEMP "zan-sdl3-$Version"
$archive = Join-Path $work "SDL3-devel-$Version-mingw.zip"
$packageDir = Join-Path $work "SDL3-$Version\x86_64-w64-mingw32"

if (!(Test-Path -LiteralPath $Compiler)) {
    throw "MinGW compiler not found: $Compiler"
}

New-Item -ItemType Directory -Force -Path $work, $driverDir | Out-Null

if (!(Test-Path -LiteralPath $archive)) {
    Write-Output "Resolving official SDL $Version release..."
    $release = Invoke-RestMethod `
        -Headers @{ "User-Agent" = "zan-lang" } `
        -Uri "https://api.github.com/repos/libsdl-org/SDL/releases/tags/release-$Version"
    $assetName = "SDL3-devel-$Version-mingw.zip"
    $asset = $release.assets | Where-Object { $_.name -eq $assetName } | Select-Object -First 1
    if ($null -eq $asset) {
        throw "Release asset not found: $assetName"
    }
    Invoke-WebRequest `
        -Headers @{ "User-Agent" = "zan-lang"; "Accept" = "application/octet-stream" } `
        -Uri $asset.url `
        -OutFile $archive
}

if (!(Test-Path -LiteralPath $packageDir)) {
    Write-Output "Extracting SDL $Version..."
    Expand-Archive -LiteralPath $archive -DestinationPath $work -Force
}

$includeDir = Join-Path $packageDir "include"
$libraryDir = Join-Path $packageDir "lib"
$sdlDll = Join-Path $packageDir "bin\SDL3.dll"
$sdlLicense = Join-Path (Split-Path -Parent $packageDir) "LICENSE.txt"
$bridgeDll = Join-Path $driverDir "zan_sdl3.dll"
$bridgeImport = Join-Path $driverDir "libzan_sdl3.dll.a"

Write-Output "Building zan_sdl3 bridge..."
& $Compiler `
    -std=c11 -O2 -Wall -Wextra -Werror=implicit-function-declaration -shared `
    "-I$includeDir" `
    $source `
    "-L$libraryDir" -lSDL3 `
    "-Wl,--out-implib,$bridgeImport" `
    -static-libgcc `
    -o $bridgeDll
if ($LASTEXITCODE -ne 0) {
    throw "zan_sdl3 bridge build failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath $sdlDll -Destination (Join-Path $driverDir "SDL3.dll") -Force
Copy-Item -LiteralPath $sdlLicense -Destination (Join-Path $driverDir "SDL3-LICENSE.txt") -Force

Write-Output "SDL3 $Version staged in $driverDir"

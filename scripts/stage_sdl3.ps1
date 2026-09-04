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
$sdlLicense = Join-Path (Split-Path -Parent $packageDir) "LICENSE.txt"
$bridgeDll = Join-Path $driverDir "zan_sdl3.dll"
$bridgeImport = Join-Path $driverDir "libzan_sdl3.dll.a"

# ---- static library ---------------------------------------------------------
# The official mingw devel package ships only the import lib (libSDL3.dll.a);
# the IDE and the zan_sdl3 bridge link SDL3 statically, so build libSDL3.a
# from the source release once and drop it beside the import lib.
$staticLib = Join-Path $libraryDir "libSDL3.a"
if (!(Test-Path -LiteralPath $staticLib)) {
    $srcArchive = Join-Path $work "SDL3-$Version-src.zip"
    $srcDir = Join-Path $work "SDL3-$Version"   # source unzips over the devel tree
    if (!(Test-Path -LiteralPath (Join-Path $srcDir "CMakeLists.txt"))) {
        Write-Output "Downloading SDL $Version source for the static build..."
        $release = Invoke-RestMethod `
            -Headers @{ "User-Agent" = "zan-lang" } `
            -Uri "https://api.github.com/repos/libsdl-org/SDL/releases/tags/release-$Version"
        $srcAsset = $release.assets | Where-Object { $_.name -eq "SDL3-$Version.zip" } | Select-Object -First 1
        if ($null -eq $srcAsset) { throw "Release asset not found: SDL3-$Version.zip" }
        Invoke-WebRequest `
            -Headers @{ "User-Agent" = "zan-lang"; "Accept" = "application/octet-stream" } `
            -Uri $srcAsset.url `
            -OutFile $srcArchive
        Expand-Archive -LiteralPath $srcArchive -DestinationPath $work -Force
    }
    Write-Output "Building static libSDL3.a (mingw)..."
    $bld = Join-Path $work "build-static"
    & cmake -S $srcDir -B $bld -G Ninja `
        "-DCMAKE_C_COMPILER=$Compiler" `
        -DCMAKE_BUILD_TYPE=Release `
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF
    if ($LASTEXITCODE -ne 0) { throw "SDL3 static configure failed" }
    & cmake --build $bld --target SDL3-static
    if ($LASTEXITCODE -ne 0) { throw "SDL3 static build failed" }
    Copy-Item -LiteralPath (Join-Path $bld "libSDL3.a") -Destination $staticLib -Force
}

# ---- zan_sdl3 bridge --------------------------------------------------------
# SDL3 is linked statically INTO the bridge DLL, so the bridge itself needs no
# separate SDL3.dll. The win-x64 bundle still ships SDL3.dll (like every other
# target) because Game.Kit Host calls SDL_RestoreWindow directly via [DllImport("SDL3")]
# (see drivers/win-x64/*.bundle).
Write-Output "Building zan_sdl3 bridge (static SDL3)..."
& $Compiler `
    -std=c11 -O2 -Wall -Wextra -Werror=implicit-function-declaration -shared `
    "-I$includeDir" `
    $source `
    "-L$libraryDir" "-l:libSDL3.a" `
    -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 `
    -lversion -luuid -ladvapi32 -lsetupapi -lshell32 `
    "-Wl,--out-implib,$bridgeImport" `
    -static-libgcc `
    -o $bridgeDll
if ($LASTEXITCODE -ne 0) {
    throw "zan_sdl3 bridge build failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath $sdlLicense -Destination (Join-Path $driverDir "SDL3-LICENSE.txt") -Force

Write-Output "SDL3 $Version staged in $driverDir"

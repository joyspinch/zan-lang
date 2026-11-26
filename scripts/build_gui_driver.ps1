# Build/stage the independent GUI, image, audio and game native drivers.
# The historical filename stays compatible with existing callers.
# Shared: cmake --build build --target zan_gui zan_image zan_audio zan_game,
#         then powershell -File scripts/build_gui_driver.ps1.
# Static: powershell -File scripts/build_gui_driver.ps1 -Static.
# -StageRoot additionally creates a common --driver-dir (including /static).
# -BuildLocal writes the x64 bundles under BuildDir/local-drivers.
# Driver ownership manifests are maintained with the Zan modules, not here.
param(
    [switch]$Static,
    [ValidateSet("win-x64", "win-arm64")][string]$Target = "win-x64",
    [string[]]$Drivers = @("zan_gui", "zan_image", "zan_audio", "zan_video", "zan_game"),
    [string]$BuildDir = "build",
    [string]$StageRoot = "",
    [string]$Clang = "clang",
    [switch]$BuildLocal
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$owners = @{
    zan_gui = "packages\Zan.Gui\src\Gui\drivers"
    zan_image = "packages\Zan.Drawing\src\System\Drawing\Imaging\drivers"
    zan_audio = "packages\Zan.Desktop\src\System\Media\drivers"
    zan_video = "packages\Zan.Media\src\System\Media\Video\drivers"
    zan_game = "packages\Zan.Game\src\Game\Graphics\drivers"
}
$syslibs = @{
    zan_gui = @("dwmapi", "gdi32", "imm32", "user32", "shcore", "ole32", "rpcrt4")
    zan_image = @()
    zan_audio = @("ole32")
    zan_video = @()
    # .libs dependencies are flat: include the GUI system closure explicitly.
    zan_game = @("zan_gui", "zan_image", "dwmapi", "gdi32", "imm32", "user32", "shcore", "ole32", "rpcrt4")
}
foreach ($driver in $Drivers) {
    if (-not $owners.ContainsKey($driver)) { throw "UNKNOWN_NATIVE_DRIVER: $driver" }
}
if ($BuildLocal -and $Target -ne 'win-x64') { throw 'BuildLocal requires win-x64' }
if ($StageRoot -and -not [IO.Path]::IsPathRooted($StageRoot)) { $StageRoot = Join-Path $root $StageRoot }

function Invoke-Native([string]$tool, [string[]]$arguments) {
    & $tool @arguments
    if ($LASTEXITCODE -ne 0) { throw "$tool failed with exit code $LASTEXITCODE" }
}
function Driver-Dir([string]$driver) {
    if ($BuildLocal) { return (Join-Path $build 'local-drivers') }
    Join-Path (Join-Path $root $owners[$driver]) $Target
}
function Write-Bundle([string]$driver, [string]$directory) {
    $entries = @("$driver.dll")
    if ($driver -eq "zan_gui") { $entries += "WebView2Loader.dll if WebView" }
    if ($driver -eq "zan_game") { $entries = @("@driver/zan_gui", "@driver/zan_image", "$driver.dll") }
    $entries | Set-Content -Encoding ascii (Join-Path $directory "$driver.bundle")
}

if ($Static) {
    $clangExe = (Get-Command $Clang -ErrorAction Stop).Source
    $clangDir = Split-Path -Parent $clangExe
    $clangCxx = Join-Path $clangDir "clang++.exe"
    if (-not (Test-Path $clangCxx)) { $clangCxx = $clangExe }
    $ar = Join-Path $clangDir "llvm-ar.exe"
    if (-not (Test-Path $ar)) { $ar = "llvm-ar" }
    $triple = if ($Target -eq "win-arm64") { "aarch64-w64-windows-gnu" } else { "x86_64-w64-windows-gnu" }
    $work = Join-Path $build "native_drivers\$Target"
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    foreach ($driver in $Drivers) {
        $directory = Join-Path (Driver-Dir $driver) "static"
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        $driverSubdir = switch ($driver) {
            "zan_gui" { "gui" }
            "zan_image" { "image" }
            "zan_audio" { "audio" }
            "zan_video" { "video" }
            "zan_game" { "game" }
        }
        $sourceName = if ($driver -eq "zan_gui") { "gui_runtime.c" } else { "$driver.c" }
        $source = Join-Path $root "src\drivers\$driverSubdir\$sourceName"
        $object = Join-Path $work "$driver.o"
        $definition = "-D" + $driver.ToUpperInvariant() + "_STATIC"
        # Static archive members need separate code/data sections for publish GC.
        Invoke-Native $clangExe @("--target=$triple", "-O2", "-DNDEBUG", $definition,
            "-ffunction-sections", "-fdata-sections",
            "-DWINVER=0x0601", "-D_WIN32_WINNT=0x0601",
            "-I", (Join-Path $root "src\drivers"),
            "-I", (Join-Path $root "src\third_party"),
            "-I", (Join-Path $root "src\runtime"),
            "-c", $source, "-o", $object)
        $objects = @($object)
        if ($driver -eq "zan_gui") {
            $dwrite = Join-Path $work "zan_gui_dwrite.o"
            $dwriteSource = Join-Path $root "src\drivers\gui\gui_runtime_dwrite.cpp"
            Invoke-Native $clangCxx @("--target=$triple", "-O2", "-DNDEBUG", "-DZAN_GUI_STATIC",
                "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti",
                "-I", (Join-Path $root "src\drivers"),
                "-I", (Join-Path $root "src\runtime"),
                "-c", $dwriteSource, "-o", $dwrite)
            $objects += $dwrite
        }
        $archive = Join-Path $directory "lib$driver.a"
        # ar rcs alone retains old unity/audio members; recreate the archive.
        Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
        Invoke-Native $ar (@("rcs", $archive) + $objects)
        $dependencies = [string[]]$syslibs[$driver]
        if ($dependencies.Count -eq 0) { $dependencies = @("# Pure decoder/cache: no system-library dependency.") }
        [IO.File]::WriteAllLines((Join-Path $directory "$driver.libs"), $dependencies)
        if ($StageRoot) {
            $commonStatic = Join-Path $StageRoot "static"
            New-Item -ItemType Directory -Force -Path $commonStatic | Out-Null
            Copy-Item -LiteralPath $archive -Destination $commonStatic -Force
            Copy-Item -LiteralPath (Join-Path $directory "$driver.libs") -Destination $commonStatic -Force
        }
        Write-Output "NATIVE_STATIC_DRIVER_OK $driver -> $archive"
    }
} else {
    foreach ($driver in $Drivers) {
        $dll = Join-Path $build "$driver.dll"
        if (-not (Test-Path -LiteralPath $dll)) { throw "MISSING_NATIVE_DLL: build the $driver CMake target first ($dll)" }
        $directory = Driver-Dir $driver
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        # The CMake helper reports unavailable tools as warnings; require them
        # here and remove the old archive so a warning cannot stage stale exports.
        Get-Command llvm-objdump, llvm-dlltool -ErrorAction Stop | Out-Null
        $dump = & llvm-objdump -f $dll
        if ($LASTEXITCODE -ne 0) { throw "OBJDUMP_FAILED: $dll" }
        $format = if ($Target -eq "win-arm64") { "coff-arm64" } else { "coff-x86-64" }
        if (($dump -join "`n") -notmatch "file format $format") {
            throw "NATIVE_DLL_TARGET_MISMATCH: $dll must match $Target"
        }
        $importDir = if ($BuildLocal) { $directory } else { $build }
        $import = Join-Path $importDir "lib$driver.dll.a"
        Remove-Item -LiteralPath $import -Force -ErrorAction SilentlyContinue
        Invoke-Native "cmake" @("-DZAN_OBJDUMP=llvm-objdump", "-DDLL=$dll",
            "-DOUT=$import", "-DDEF_OUT=$(Join-Path $directory "$driver.def")",
            "-P", (Join-Path $root "cmake\zan_dll_import_lib.cmake"))
        if (-not (Test-Path -LiteralPath $import)) { throw "MISSING_NATIVE_IMPORT_LIB: $import" }
        Copy-Item -LiteralPath $dll -Destination $directory -Force
        if (-not $BuildLocal) { Copy-Item -LiteralPath $import -Destination $directory -Force }
        Write-Bundle $driver $directory
        if ($BuildLocal -and $driver -eq 'zan_gui') {
            $loader = Join-Path (Join-Path $root $owners[$driver]) "$Target\WebView2Loader.dll"
            Copy-Item -LiteralPath $loader -Destination $directory -Force
        }
        if ($StageRoot) {
            New-Item -ItemType Directory -Force -Path $StageRoot | Out-Null
            Copy-Item -LiteralPath $dll, $import, (Join-Path $directory "$driver.bundle") -Destination $StageRoot -Force
            if ($driver -eq "zan_gui") {
                $loader = Join-Path $directory "WebView2Loader.dll"
                if (Test-Path $loader) { Copy-Item -LiteralPath $loader -Destination $StageRoot -Force }
            }
        }
        Write-Output "NATIVE_SHARED_DRIVER_OK $driver -> $directory"
    }
}

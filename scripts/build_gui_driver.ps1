# Build/stage the independent GUI, image, audio and game native drivers.
# The historical filename stays compatible with existing callers.
# Shared: cmake --build build --target zan_gui zan_image zan_audio zan_game,
#         then powershell -File scripts/build_gui_driver.ps1.
# Static: powershell -File scripts/build_gui_driver.ps1 -Static.
# -StageRoot additionally creates a common --driver-dir (including /static).
# Driver ownership manifests are maintained with the Zan modules, not here.
param(
    [switch]$Static,
    [ValidateSet("win-x64", "win-arm64")][string]$Target = "win-x64",
    [string[]]$Drivers = @("zan_gui", "zan_image", "zan_audio", "zan_game"),
    [string]$BuildDir = "build",
    [string]$StageRoot = "",
    [string]$Clang = "clang"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$owners = @{
    zan_gui = "packages\Zan.Gui\src\Gui\drivers"
    zan_image = "packages\Zan.Image\src\System\Drawing\Imaging\drivers"
    zan_audio = "packages\Zan.Desktop\src\System\Media\drivers"
    zan_game = "packages\Zan.Game\src\Game\Graphics\drivers"
}
$syslibs = @{
    zan_gui = @("dwmapi", "gdi32", "imm32", "user32", "shcore", "ole32", "rpcrt4")
    zan_image = @()
    zan_audio = @("ole32")
    # .libs dependencies are flat: include the GUI system closure explicitly.
    zan_game = @("zan_gui", "zan_image", "dwmapi", "gdi32", "imm32", "user32", "shcore", "ole32", "rpcrt4")
}
foreach ($driver in $Drivers) {
    if (-not $owners.ContainsKey($driver)) { throw "UNKNOWN_NATIVE_DRIVER: $driver" }
}
if ($StageRoot -and -not [IO.Path]::IsPathRooted($StageRoot)) { $StageRoot = Join-Path $root $StageRoot }

function Invoke-Native([string]$tool, [string[]]$arguments) {
    & $tool @arguments
    if ($LASTEXITCODE -ne 0) { throw "$tool failed with exit code $LASTEXITCODE" }
}
function Driver-Dir([string]$driver) {
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
        $source = if ($driver -eq "zan_gui") { "gui_runtime.c" } else { "$driver.c" }
        $object = Join-Path $work "$driver.o"
        $definition = "-D" + $driver.ToUpperInvariant() + "_STATIC"
        Invoke-Native $clangExe @("--target=$triple", "-O2", "-DNDEBUG", $definition,
            "-DWINVER=0x0601", "-D_WIN32_WINNT=0x0601", "-c",
            (Join-Path $root "src\runtime\$source"), "-o", $object)
        $objects = @($object)
        if ($driver -eq "zan_gui") {
            $dwrite = Join-Path $work "zan_gui_dwrite.o"
            Invoke-Native $clangCxx @("--target=$triple", "-O2", "-DNDEBUG", "-DZAN_GUI_STATIC",
                "-fno-exceptions", "-fno-rtti", "-c",
                (Join-Path $root "src\runtime\gui_runtime_dwrite.cpp"), "-o", $dwrite)
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
        Remove-Item -LiteralPath (Join-Path $build "lib$driver.dll.a") -Force -ErrorAction SilentlyContinue
        $import = Join-Path $build "lib$driver.dll.a"
        Invoke-Native "cmake" @("-DZAN_OBJDUMP=llvm-objdump", "-DDLL=$dll",
            "-DOUT=$import", "-DDEF_OUT=$(Join-Path $directory "$driver.def")",
            "-P", (Join-Path $root "cmake\zan_dll_import_lib.cmake"))
        if (-not (Test-Path -LiteralPath $import)) { throw "MISSING_NATIVE_IMPORT_LIB: $import" }
        Copy-Item -LiteralPath $dll, $import -Destination $directory -Force
        Write-Bundle $driver $directory
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

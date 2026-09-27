# Builds the native static driver archives used by Windows x64 single-file
# publishes. The GUI recipe matches build_ide.ps1; SQLite is built from the
# pinned amalgamation used by .github/workflows/drivers.yml.
#
# OpenSSL static archives are staged by the drivers workflow from the MSYS2
# OpenSSL package. This developer-side script does not build OpenSSL from
# source.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Fail([string]$mark, [string]$message) {
    Write-Output ($mark + ": " + $message)
    exit 1
}

$guiDir = Join-Path $root "stdlib\Gui\drivers\win-x64"
$sqliteDir = Join-Path $root "stdlib\System\Data\Sqlite\drivers\win-x64"
$guiStatic = Join-Path $guiDir "static"
$sqliteStatic = Join-Path $sqliteDir "static"
$work = Join-Path $root "build\win_static_drivers"
New-Item -ItemType Directory -Force -Path $guiStatic, $sqliteStatic, $work | Out-Null

$guiObj = Join-Path $work "zan_gui_win_x64.o"
$guiArchive = Join-Path $guiStatic "libzan_gui.a"
# -DNDEBUG drops the vendored libs' assert() strings, whose __FILE__ otherwise
# leak build-machine paths (D:\<repo>\src\runtime\libwebp/...) into every
# single-file publish. On-demand subsystems use archive-member granularity
# instead of --gc-sections: GNU ld's PE link keeps .text$-grouped sections
# no matter what, so ZAN_GUI_AUDIO_SEPARATE splits the WASAPI mixer
# (+ stb_vorbis) into its own member that is pulled only when the program's
# DllImport surface actually references zan_audio_*.
try {
    & clang --target=x86_64-w64-windows-gnu -O2 -DNDEBUG -DZAN_GUI_STATIC `
        -DZAN_GUI_AUDIO_SEPARATE `
        -c (Join-Path $root "src\runtime\gui_runtime.c") -o $guiObj
    if ($LASTEXITCODE -ne 0) {
        Fail "GUI_RUNTIME_COMPILE_FAILED" "clang returned $LASTEXITCODE"
    }
} catch {
    Fail "GUI_RUNTIME_COMPILE_FAILED" $_.Exception.Message
}

$dwriteObj = Join-Path $work "zan_gui_dwrite_win_x64.o"
try {
    & clang++ --target=x86_64-w64-windows-gnu -O2 -DNDEBUG `
        -fno-exceptions -fno-rtti `
        -c (Join-Path $root "src\runtime\gui_runtime_dwrite.cpp") -o $dwriteObj
    if ($LASTEXITCODE -ne 0) {
        Fail "GUI_DWRITE_COMPILE_FAILED" "clang++ returned $LASTEXITCODE"
    }
} catch {
    Fail "GUI_DWRITE_COMPILE_FAILED" $_.Exception.Message
}

# Audio as a separate archive member (see ZAN_GUI_AUDIO_SEPARATE above).
$audioObj = Join-Path $work "zan_audio_win_x64.o"
try {
    & clang --target=x86_64-w64-windows-gnu -O2 -DNDEBUG `
        -c (Join-Path $root "src\runtime\zan_audio.c") -o $audioObj
    if ($LASTEXITCODE -ne 0) {
        Fail "GUI_AUDIO_COMPILE_FAILED" "clang returned $LASTEXITCODE"
    }
} catch {
    Fail "GUI_AUDIO_COMPILE_FAILED" $_.Exception.Message
}

try {
    if (Test-Path -LiteralPath $guiArchive) {
        Remove-Item -LiteralPath $guiArchive -Force
    }
    & llvm-ar rcs $guiArchive $guiObj $dwriteObj $audioObj
    if ($LASTEXITCODE -ne 0) {
        Fail "GUI_RUNTIME_LIB_FAILED" "llvm-ar returned $LASTEXITCODE"
    }
} catch {
    Fail "GUI_RUNTIME_LIB_FAILED" $_.Exception.Message
}

@"
# Win32 dependencies from scripts/build_ide.ps1, merged with the committed
# static bundle's list: ole32 backs the WASAPI mixer's COM calls (CoInitialize/
# CoTaskMemFree in zan_audio.c), shcore backs the Per-Monitor-DPI queries in
# gui_runtime_dwrite.cpp. The async reactor and process helpers bring the rest.
dwmapi
gdi32
imm32
user32
shcore
ole32
rpcrt4
ws2_32
mswsock
psapi
advapi32
"@ | Set-Content -Encoding ascii (Join-Path $guiStatic "zan_gui.libs")

$sqliteUrl = "https://sqlite.org/2024/sqlite-amalgamation-3460100.zip"
$sqliteZip = Join-Path $work "sq.zip"
$sqlitePart = Join-Path $work "sq.zip.part"
$sqliteSourceDir = Join-Path $work "sqlite-amalgamation-3460100"
$sqliteObj = Join-Path $work "sqlite3_win_x64.o"
$sqliteArchive = Join-Path $sqliteStatic "libsqlite3.a"
$checksumFile = Join-Path $root "deps\checksums.txt"
$checksumLine = Select-String -Path $checksumFile -Pattern `
    "^[0-9a-fA-F]{64}\s+sq\.zip\s*$" | Select-Object -First 1
if ($null -eq $checksumLine) {
    Fail "SQLITE_CHECKSUM_FAILED" "sq.zip entry missing from deps\checksums.txt"
}
$expectedHash = ($checksumLine.Line -split "\s+")[0].ToLowerInvariant()

$zipReady = $false
for ($attempt = 1; $attempt -le 2 -and !$zipReady; $attempt++) {
    try {
        if (Test-Path -LiteralPath $sqliteZip) {
            $actualHash = (Get-FileHash -Algorithm SHA256 `
                -LiteralPath $sqliteZip).Hash.ToLowerInvariant()
            if ($actualHash -eq $expectedHash) {
                $zipReady = $true
                break
            }
            Remove-Item -LiteralPath $sqliteZip -Force
        }
        if (Test-Path -LiteralPath $sqlitePart) {
            Remove-Item -LiteralPath $sqlitePart -Force
        }
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $sqliteUrl `
                -OutFile $sqlitePart
        } catch {
            Fail "SQLITE_DOWNLOAD_FAILED" $_.Exception.Message
        }
        $actualHash = (Get-FileHash -Algorithm SHA256 `
            -LiteralPath $sqlitePart).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            Remove-Item -LiteralPath $sqlitePart -Force `
                -ErrorAction SilentlyContinue
            if ($attempt -eq 2) {
                Fail "SQLITE_CHECKSUM_FAILED" `
                    ("expected " + $expectedHash + ", got " + $actualHash)
            }
            continue
        }
        Move-Item -LiteralPath $sqlitePart -Destination $sqliteZip -Force
        $zipReady = $true
    } catch {
        if ($attempt -eq 2) {
            Fail "SQLITE_CHECKSUM_FAILED" $_.Exception.Message
        }
    }
}
if (!$zipReady) {
    Fail "SQLITE_CHECKSUM_FAILED" "sqlite archive was not verified"
}

try {
    if (Test-Path -LiteralPath $sqliteSourceDir) {
        Remove-Item -LiteralPath $sqliteSourceDir -Recurse -Force
    }
    Expand-Archive -LiteralPath $sqliteZip -DestinationPath $work -Force
    if (!(Test-Path -LiteralPath (Join-Path $sqliteSourceDir "sqlite3.c"))) {
        Fail "SQLITE_UNZIP_FAILED" "sqlite3.c not found after extraction"
    }
} catch {
    Fail "SQLITE_UNZIP_FAILED" $_.Exception.Message
}

try {
    & clang --target=x86_64-w64-windows-gnu -O2 -DNDEBUG `
        -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1 -DSQLITE_ENABLE_RTREE `
        -c (Join-Path $sqliteSourceDir "sqlite3.c") -o $sqliteObj
    if ($LASTEXITCODE -ne 0) {
        Fail "SQLITE_COMPILE_FAILED" "clang returned $LASTEXITCODE"
    }
} catch {
    Fail "SQLITE_COMPILE_FAILED" $_.Exception.Message
}

try {
    if (Test-Path -LiteralPath $sqliteArchive) {
        Remove-Item -LiteralPath $sqliteArchive -Force
    }
    & llvm-ar rcs $sqliteArchive $sqliteObj
    if ($LASTEXITCODE -ne 0) {
        Fail "SQLITE_LIB_FAILED" "llvm-ar returned $LASTEXITCODE"
    }
} catch {
    Fail "SQLITE_LIB_FAILED" $_.Exception.Message
}

Write-Output ("WIN_STATIC_DRIVERS_OK gui=" + $guiArchive +
    " sqlite=" + $sqliteArchive)

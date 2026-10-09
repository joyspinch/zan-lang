# Builds the native static driver archives used by Windows x64 single-file
# publishes. GUI/image/audio/game reuse build_gui_driver.ps1; SQLite is built
# from the pinned amalgamation used by .github/workflows/drivers.yml.
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

# Each native feature gets its own owner directory/archive; compiling the game
# driver here does not force it into a GUI application's link.
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static

$sqliteDir = Join-Path $root "packages\Zan.Data\src\System\Data\Sqlite\drivers\win-x64"
$sqliteStatic = Join-Path $sqliteDir "static"
$work = Join-Path $root "build\win_static_drivers"
New-Item -ItemType Directory -Force -Path $sqliteStatic, $work | Out-Null

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

Write-Output ("WIN_STATIC_DRIVERS_OK native=zan_gui,zan_image,zan_audio,zan_game sqlite=" + $sqliteArchive)

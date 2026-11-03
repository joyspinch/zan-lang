<#
.SYNOPSIS
    Installs a pinned, portable JRE for APK signing (apksigner) into ~/.zan/jre-win.

.DESCRIPTION
    B-ID104: JRE 安装器职责从编译器 (apk.c) 独立出来作为独立 setup 工具。
    默认下载不可变 Adoptium Eclipse Temurin JRE 21.0.13+9，校验 SHA-256 后解压到
    $HOME/.zan/jre-win。支持通过环境变量或参数覆盖。

.PARAMETER Url
    JRE 下载 URL。默认环境变量 $env:ZAN_JDK_URL，或 Adoptium JRE 21 钉扎 URL。

.PARAMETER Sha256
    期望的 SHA-256 校验和。默认环境变量 $env:ZAN_JDK_SHA256，或钉扎 SHA-256。

.PARAMETER Destination
    安装目标目录。默认 $HOME/.zan/jre-win。

.PARAMETER Force
    强制重新下载安装，即便目标已存在。
#>

[CmdletBinding()]
param(
    [string]$Url = $env:ZAN_JDK_URL,
    [string]$Sha256 = $env:ZAN_JDK_SHA256,
    [string]$Destination = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

if (-not $Url) {
    # 钉扎不可变发行版（与 deps/checksums.txt 同纪律）
    $Url = "https://api.adoptium.net/v3/binary/version/jdk-21.0.13%2B9/windows/x64/jre/hotspot/normal/eclipse?project=jdk"
    if (-not $Sha256) {
        $Sha256 = "2332c0b4cff6bed722da3567c91d5e47568bcba863fa4945d0a8d6fc76158628"
    }
}

$HomeDir = if ($env:USERPROFILE) { $env:USERPROFILE } else { $env:HOME }
if (-not $HomeDir) {
    Write-Error "Neither USERPROFILE nor HOME environment variable is set."
    exit 1
}

$ZanDir = Join-Path $HomeDir ".zan"
if (-not $Destination) {
    $Destination = Join-Path $ZanDir "jre-win"
}

$DoneMarker = Join-Path $Destination ".done"
$JavaExe = Join-Path $Destination "bin\java.exe"

if (-not $Force -and (Test-Path $DoneMarker) -and (Test-Path $JavaExe)) {
    Write-Host "Portable JRE already installed at $Destination"
    try { & $JavaExe -version } catch {}
    exit 0
}

if (-not (Test-Path $ZanDir)) {
    New-Item -ItemType Directory -Path $ZanDir -Force | Out-Null
}

$ZipPath = Join-Path $ZanDir "jre_download.zip"

Write-Host "Downloading portable JRE (~50 MB)..."
Write-Host "  URL: $Url"

try {
    $wc = New-Object System.Net.WebClient
    $wc.DownloadFile($Url, $ZipPath)

    if ($Sha256) {
        Write-Host "Verifying SHA-256 integrity..."
        $actualHash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $expectedHash = $Sha256.Trim().ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            Remove-Item -LiteralPath $ZipPath -Force -ErrorAction SilentlyContinue
            Write-Error "Downloaded JRE failed SHA-256 integrity check!`n  Expected: $expectedHash`n  Got:      $actualHash"
            exit 1
        }
        Write-Host "  SHA-256 verified: $actualHash"
    } else {
        Write-Warning "ZAN_JDK_URL set without SHA-256 verification!"
    }

    Write-Host "Unpacking to $ZanDir..."
    Expand-Archive -Force -LiteralPath $ZipPath -DestinationPath $ZanDir

    $extracted = Get-ChildItem -LiteralPath $ZanDir -Directory | Where-Object { $_.Name -like "jdk-*" }
    if ($extracted) {
        if (Test-Path $Destination) {
            Remove-Item -LiteralPath $Destination -Recurse -Force
        }
        Move-Item -Force -LiteralPath $extracted.FullName -Destination $Destination
    }

    Set-Content -LiteralPath $DoneMarker -Value "ok" -Encoding ascii

    if (-not (Test-Path $JavaExe)) {
        Write-Error "JRE installation did not produce expected java executable at $JavaExe"
        exit 1
    }

    Write-Host "Successfully installed portable JRE at $Destination"
    try { & $JavaExe -version } catch {}
} finally {
    if (Test-Path $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force -ErrorAction SilentlyContinue
    }
}

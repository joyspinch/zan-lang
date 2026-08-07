param(
    [string]$Version = "3.12.7",
    [string]$Dest = ""
)

# Stage a self-contained CPython into the module's driver directory,
# stdlib\System\Scripting\drivers\win-x64 (and into build\ for dev runs), so a
# published program using System.Scripting.Python runs on a machine with no
# Python installed.
#
# Uses python.org's official Windows "embeddable package": pythonXY.dll plus
# the standard library as pythonXY.zip, i.e. the interpreter *and* its stdlib,
# which a bare DLL copy would be missing (Py_Initialize then falls back to the
# build-time prefix and `import json` fails).

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $Dest) {
    $Dest = Join-Path $root "stdlib\System\Scripting\drivers\win-x64"
}
$work = Join-Path $env:TEMP "zan-python-$Version"
$archive = Join-Path $work "python-$Version-embed-amd64.zip"

New-Item -ItemType Directory -Force -Path $work, $Dest | Out-Null

if (!(Test-Path -LiteralPath $archive)) {
    Write-Output "Downloading CPython $Version (embeddable)..."
    # curl.exe, not Invoke-WebRequest: the cmdlet writes a truncated file on an
    # interrupted transfer, which only surfaces as a corrupt archive later.
    & curl.exe -fsSL --retry 3 -o $archive `
        "https://www.python.org/ftp/python/$Version/python-$Version-embed-amd64.zip"
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $archive -ErrorAction SilentlyContinue
        throw "download failed: python-$Version-embed-amd64.zip"
    }
}

Expand-Archive -LiteralPath $archive -DestinationPath $Dest -Force

$dll = Get-ChildItem -LiteralPath $Dest -Filter "python3??.dll" | Select-Object -First 1
if (-not $dll) { throw "no pythonXY.dll in $archive" }

# Drop what only an *installed* interpreter needs. Python.zan loads
# pythonXY.dll in-process and never spawns the interpreter, so the launchers
# (python.exe / pythonw.exe), the installer signature catalog (python.cat) and
# the MSI-building extension (_msi.pyd) are dead weight beside a published
# program -- together ~0.8 MB per copy, and every copy is committed.
#
# `pythonXY._pth` goes too: the embeddable package uses it to pin sys.path and
# disable site imports, and dropping it lets PYTHONHOME (set by Python.zan when
# it loads a bundled runtime) drive the path, so applications can add their own
# modules next to the exe. (Earlier revisions renamed it to *.disabled; remove
# those leftovers as well.)
$unused = @("python.exe", "pythonw.exe", "python.cat", "_msi.pyd")
Get-ChildItem -LiteralPath $Dest -File |
    Where-Object { $unused -contains $_.Name -or $_.Name -like "python3??._pth*" } |
    Remove-Item -Force

# Keep only the core interpreter: pythonXY.dll, python3.dll, pythonXY.zip,
# vcruntime140*.dll and LICENSE.txt. The embeddable's optional C extension
# modules (*.pyd) and their support DLLs (libssl/libcrypto/libffi/sqlite3) are
# dropped -- a published program only needs the core interpreter plus the Lua
# driver, so a script that imports ssl / sqlite3 / ctypes / socket / etc. is out
# of scope for the shipped driver. Re-add a module (and its DLLs) here if it
# becomes required.
$optionalDlls = @("libcrypto-3.dll", "libssl-3.dll", "libffi-8.dll", "sqlite3.dll")
Get-ChildItem -LiteralPath $Dest -File |
    Where-Object { $_.Extension -eq ".pyd" -or $optionalDlls -contains $_.Name } |
    Remove-Item -Force

# zanc copies the files listed here next to a published executable; ship the
# trimmed core-interpreter payload (stdlib zip + DLLs). Everything the module
# itself owns is excluded (lua*, *.bundle).
$payload = Get-ChildItem -LiteralPath $Dest -File |
           Where-Object { $_.Name -notlike "*.bundle" -and $_.Name -notlike "lua*" } |
           ForEach-Object { $_.Name }
Set-Content -LiteralPath (Join-Path $Dest "python.bundle") -Encoding ascii `
            -Value $payload

$devDir = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $devDir | Out-Null
foreach ($f in $payload) { Copy-Item (Join-Path $Dest $f) $devDir -Force }

Write-Output "STAGE_PYTHON_OK $Dest ($($dll.Name))"

param(
    [string]$Version = "3.12.7",
    [string]$Dest = "",
    [ValidateSet("core", "full")]
    [string]$Trim = "core"
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
#
# -Trim core (default) keeps only what Py_Initialize needs to boot: the
# encodings codecs it probes at startup, the frozen-importlib companions, and
# a handful of root modules (os/io/codecs/abc/...). The result is ~7.5 MB
# instead of ~22 MB -- verified against tests/conformance/
# python_embed_smoke.zan -- but `import json`-style stdlib imports fail with
# ModuleNotFoundError. Re-stage with -Trim full to restore them.

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

# -Trim core: rebuild pythonXY.zip down to the Py_Initialize bootstrap set.
# CPython needs the encodings package (aliases + the codecs probed at startup),
# importlib's frozen-bootstrap companions, and the root modules importlib pulls
# in while setting up sys.path (os -> stat/ntpath/genericpath, abc,
# _collections_abc, codecs, io). Everything else in the stdlib zip is import-
# on-demand; the conformance smoke runs green against exactly this set.
if ($Trim -eq "core") {
    $zip = Get-ChildItem -LiteralPath $Dest -Filter "python3??.zip" | Select-Object -First 1
    if (-not $zip) { throw "no pythonXY.zip in $Dest" }
    $encodings = @("encodings/__init.pyc", "encodings/aliases.pyc",
                   "encodings/utf_8.pyc", "encodings/latin_1.pyc",
                   "encodings/ascii.pyc", "encodings/cp1252.pyc",
                   "encodings/utf_8_sig.pyc", "encodings/mbcs.pyc")
    $importlib = @("importlib/__init__.pyc", "importlib/_bootstrap.pyc",
                   "importlib/_bootstrap_external.pyc", "importlib/_abc.pyc",
                   "importlib/machinery.pyc")
    $roots = @("codecs.pyc", "io.pyc", "os.pyc", "stat.pyc", "abc.pyc",
               "_collections_abc.pyc", "genericpath.pyc", "ntpath.pyc")
    $keep = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($e in $encodings + $importlib + $roots) { $keep.Add($e) | Out-Null }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zin = [System.IO.Compression.ZipFile]::OpenRead($zip.FullName)
    try {
        $znew = "$($zip.FullName).new"
        $zout = [System.IO.Compression.ZipFile]::Open($znew, "Create")
        try {
            foreach ($entry in $zin.Entries) {
                if ($keep.Contains($entry.FullName)) {
                    $dst2 = $zout.CreateEntry($entry).Open()
                    $src2 = $entry.Open()
                    $src2.CopyTo($dst2)
                    $src2.Dispose(); $dst2.Dispose()
                }
            }
        } finally { $zout.Dispose() }
    } finally { $zin.Dispose() }
    $booted = $znew
    Remove-Item -LiteralPath $zip.FullName -Force
    Move-Item -LiteralPath $znew -Destination $zip.FullName

    # The .pyd extension modules and their OpenSSL/sqlite DLL dependencies are
    # import-on-demand stdlib pieces; core-tier programs never import them, so
    # shipping 14 MB of them beside a 124 KB zip defeats the point. A program
    # that does need one re-stages with -Trim full.
    $keepCore = @("python3??.dll", "python3??.zip", "vcruntime140.dll",
                  "vcruntime140_1.dll")
    Get-ChildItem -LiteralPath $Dest -File |
        Where-Object { $f = $_;
            ($f.Name -notlike "*.bundle" -and $f.Name -notlike "lua*") -and
            -not ($keepCore | Where-Object { $f.Name -like $_ })
        } |
        Remove-Item -Force
}

# zanc copies the files listed here next to a published executable; the whole
# payload is needed (stdlib zip and .pyd extension modules), not just
# the DLL. Everything the module itself owns is excluded (lua*, *.bundle).
$payload = Get-ChildItem -LiteralPath $Dest -File |
           Where-Object { $_.Name -notlike "*.bundle" -and $_.Name -notlike "lua*" } |
           ForEach-Object { $_.Name }
Set-Content -LiteralPath (Join-Path $Dest "python.bundle") -Encoding ascii `
            -Value $payload

$devDir = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $devDir | Out-Null
foreach ($f in $payload) { Copy-Item (Join-Path $Dest $f) $devDir -Force }

Write-Output "STAGE_PYTHON_OK $Dest ($($dll.Name))"

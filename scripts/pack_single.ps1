# pack_single.ps1 -- Wrap a staged program folder into ONE self-extracting exe.
#
# Zips everything under -Stage, appends it to the pkg_stub launcher (-Stub)
# with the 16-byte <size>"ZANPKG1`0" footer, and optionally stamps -Icon on
# the result. The stub extracts to %LOCALAPPDATA%\ZanGames\<exe-basename>\ on
# first run and starts <exe-basename>.exe (or game.exe) from there, so the
# stage must contain an exe named like the -Out file.
#
# Used by the IDE's Publish flow (shipped in toolchain\ next to pkg_stub.exe)
# and reusable from the command line:
#   powershell -File pack_single.ps1 -Stage <dir> -Stub <pkg_stub.exe> -Out <single.exe> [-Icon <ico>]
param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [Parameter(Mandatory=$true)][string]$Stub,
    [Parameter(Mandatory=$true)][string]$Out,
    [string]$Icon = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Stage)) { Write-Output "PACK_FAILED: stage missing: $Stage"; exit 1 }
if (-not (Test-Path $Stub))  { Write-Output "PACK_FAILED: stub missing: $Stub"; exit 1 }

$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$zip = [IO.Path]::ChangeExtension($Out, ".payload.zip")
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $zip -CompressionLevel Optimal

Copy-Item $Stub $Out -Force

if ($Icon -and (Test-Path $Icon)) {
    Add-Type @"
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class PkgRes {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr BeginUpdateResource(string file, bool deleteExisting);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool UpdateResource(IntPtr h, IntPtr type, IntPtr name, ushort lang, byte[] data, uint len);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool EndUpdateResource(IntPtr h, bool discard);

    public static void SetIcon(string exe, string ico) {
        byte[] src = File.ReadAllBytes(ico);
        int count = BitConverter.ToUInt16(src, 4);
        IntPtr h = BeginUpdateResource(exe, false);
        if (h == IntPtr.Zero) throw new Exception("BeginUpdateResource failed");
        byte[] grp = new byte[6 + 14 * count];
        Array.Copy(src, 0, grp, 0, 6);
        for (int i = 0; i < count; i++) {
            int e = 6 + 16 * i;
            int size = BitConverter.ToInt32(src, e + 8);
            int off = BitConverter.ToInt32(src, e + 12);
            byte[] img = new byte[size];
            Array.Copy(src, off, img, 0, size);
            if (!UpdateResource(h, (IntPtr)3, (IntPtr)(i + 1), 0, img, (uint)size))
                throw new Exception("UpdateResource RT_ICON failed");
            int g = 6 + 14 * i;
            Array.Copy(src, e, grp, g, 12);
            grp[g + 12] = (byte)(i + 1);
            grp[g + 13] = 0;
        }
        if (!UpdateResource(h, (IntPtr)14, (IntPtr)1, 0, grp, (uint)grp.Length))
            throw new Exception("UpdateResource RT_GROUP_ICON failed");
        if (!EndUpdateResource(h, false)) throw new Exception("EndUpdateResource failed");
    }
}
"@
    [PkgRes]::SetIcon($Out, $Icon)
}

# payload must be appended after resource edits (they rewrite the file)
$zipLen = (Get-Item $zip).Length
$outFs = [IO.File]::Open($Out, [IO.FileMode]::Append)
$inFs = [IO.File]::OpenRead($zip)
$inFs.CopyTo($outFs)
$inFs.Close()
$footer = New-Object byte[] 16
[Array]::Copy([BitConverter]::GetBytes([long]$zipLen), $footer, 8)
[Array]::Copy([Text.Encoding]::ASCII.GetBytes("ZANPKG1"), 0, $footer, 8, 7)
$outFs.Write($footer, 0, $footer.Length)
$outFs.Close()
Remove-Item $zip -Force

Write-Output ("PACK_OK -> " + $Out + " (" + ((Get-Item $Out).Length) + " bytes)")

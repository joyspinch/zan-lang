# Packages each game into a single standalone .exe (no DLLs or asset folders
# to ship): compiles the game, stages game.exe + SDL DLLs + assets into a zip,
# appends it to the self-extracting pkg_stub launcher, and stamps the per-game
# icon + GUI subsystem on the result. Output: dist\<game>.exe
param(
    [string[]]$Games = @("snake", "ddz")
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$zanc = Join-Path $root "build\zanc.exe"
if (!(Test-Path -LiteralPath $zanc)) {
    throw "zanc was not found: $zanc (build the compiler first)"
}

# gcc for the launcher stub (TDM-GCC / MinGW)
$gcc = "C:\TDM-GCC-64\bin\gcc.exe"
if (!(Test-Path -LiteralPath $gcc)) {
    $cc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $cc) { throw "gcc not found (needed for the launcher stub)" }
    $gcc = $cc.Source
}

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

    // IMAGE_SUBSYSTEM_WINDOWS_GUI = 2 (word at e_lfanew + 0x5C): no console.
    public static void SetGuiSubsystem(string exe) {
        byte[] b = File.ReadAllBytes(exe);
        int pe = BitConverter.ToInt32(b, 0x3C);
        int sub = pe + 0x5C;
        b[sub] = 2; b[sub + 1] = 0;
        File.WriteAllBytes(exe, b);
    }
}
"@

$stub = Join-Path $root "build\pkg_stub.exe"
& $gcc -O2 -mwindows (Join-Path $root "scripts\pkg_stub.c") -o $stub
if ($LASTEXITCODE -ne 0) { throw "launcher stub build failed" }

$driver = Join-Path $root "stdlib\SDL3\drivers\win-x64"
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

foreach ($game in $Games) {
    $src = Join-Path $root "examples\game\$game\main.zan"
    $kit = Join-Path $root "examples\game\common\GameKit.zan"
    if (!(Test-Path -LiteralPath $src)) { throw "game not found: $src" }

    $stage = Join-Path $root "build\pkg_$game"
    if (Test-Path -LiteralPath $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    # Release build: -DPUBLISH arms the tamper defense (dev builds stay
    # debuggable); --publish optimizes and strips debug info.
    $gameExe = Join-Path $stage "game.exe"
    & $zanc $src $kit --auto-stdlib --publish -DPUBLISH -o $gameExe
    if ($LASTEXITCODE -ne 0) { throw "$game compilation failed." }
    [PkgRes]::SetGuiSubsystem($gameExe)

    foreach ($name in @("zan_sdl3.dll", "SDL3.dll")) {
        Copy-Item -LiteralPath (Join-Path $driver $name) -Destination $stage -Force
    }

    # assets, laid out repo-relative so Assets.Find resolves them from the cwd
    foreach ($adir in @("examples\game\$game\assets", "examples\game\common\assets")) {
        $from = Join-Path $root $adir
        if (Test-Path -LiteralPath $from) {
            $to = Join-Path $stage $adir
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
            Copy-Item -Recurse -Force $from $to
        }
    }

    $zip = Join-Path $root "build\pkg_$game.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

    # stub + payload + footer(size, magic)
    $out = Join-Path $dist "$game.exe"
    $stubBytes = [IO.File]::ReadAllBytes($stub)
    $zipBytes = [IO.File]::ReadAllBytes($zip)
    $footer = New-Object byte[] 16
    [Array]::Copy([BitConverter]::GetBytes([long]$zipBytes.Length), $footer, 8)
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes("ZANPKG1"), 0, $footer, 8, 7)
    $fs = [IO.File]::Create($out)
    $fs.Write($stubBytes, 0, $stubBytes.Length)
    $fs.Close()

    $ico = Join-Path $root "examples\game\$game\assets\icon.ico"
    if (Test-Path -LiteralPath $ico) { [PkgRes]::SetIcon($out, $ico) }

    # payload must be appended after resource edits (they rewrite the file)
    $fs = [IO.File]::Open($out, [IO.FileMode]::Append)
    $fs.Write($zipBytes, 0, $zipBytes.Length)
    $fs.Write($footer, 0, $footer.Length)
    $fs.Close()

    Write-Output ("PACKAGED=$out size=" + ((Get-Item $out).Length))
}

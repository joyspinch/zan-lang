# Launch a built game exe, capture its window by title substring to a PNG,
# then terminate it. Used for visual verification of the game products.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Title,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$GameArgs = "",
    [int]$WaitMs = 1800,
    [string]$Keys = "",
    [int]$PostKeyWaitMs = 1200,
    [switch]$KeepOpen
)
$ErrorActionPreference = "Stop"

Add-Type -Name DPI -Namespace Win -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[Win.DPI]::SetProcessDPIAware() | Out-Null

$procName = [IO.Path]::GetFileNameWithoutExtension($Exe)
Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

if ($GameArgs -ne "") {
    Start-Process $Exe -ArgumentList ($GameArgs.Split(" "))
} else {
    Start-Process $Exe
}
Start-Sleep -Milliseconds $WaitMs

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WGShot{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found = IntPtr.Zero;
 public static string Needle = "";
 [StructLayout(LayoutKind.Sequential)] public struct RECT{ public int L; public int T; public int R; public int B; }
 public static bool CB(IntPtr h,IntPtr l){
   if(!IsWindowVisible(h))return true;
   var s=new StringBuilder(512);GetWindowText(h,s,512);
   if(s.ToString().Contains(Needle)){ Found=h; return false; }
   return true;
 }
}
"@
[WGShot]::Needle = $Title
[WGShot]::Found = [IntPtr]::Zero
$cb = [WGShot+EnumProc]{ param($h,$l) [WGShot]::CB($h,$l) }
[WGShot]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WGShot]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "WINDOW-NOT-FOUND"; if(-not $KeepOpen){ Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force }; exit 1 }

# SW_MAXIMIZE=3 so the game fills the screen (verifies DPI/integer-scale fit);
# z-order toggle uses NOSIZE|NOMOVE|NOACTIVATE (0x13) so we never un-maximize it.
[WGShot]::ShowWindow($h, 3) | Out-Null
[WGShot]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x13) | Out-Null
[WGShot]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x13) | Out-Null
[WGShot]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700

if ($Keys -ne "") {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
    Start-Sleep -Milliseconds $PostKeyWaitMs
}

$r = New-Object WGShot+RECT
[WGShot]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$outDir = Split-Path -Parent $Out
if ($outDir -and !(Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output ("SHOT " + $Out + " " + $w + "x" + $ht)

if (-not $KeepOpen) {
    Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
}

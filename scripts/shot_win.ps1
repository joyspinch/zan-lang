param([string]$A = "", [string]$Out = "d:\project\zan-lang\build\shot_win.png")
$exe = "d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
$argList = @()
if ($A -ne "") { $argList = $A.Split(" ") }
if ($argList.Count -gt 0) { Start-Process $exe -ArgumentList $argList } else { Start-Process $exe }
Start-Sleep -Milliseconds 1500

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WG2{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found = IntPtr.Zero;
 [StructLayout(LayoutKind.Sequential)] public struct RECT{ public int L; public int T; public int R; public int B; }
 public static bool CB(IntPtr h,IntPtr l){
   if(!IsWindowVisible(h))return true;
   var s=new StringBuilder(512);GetWindowText(h,s,512);
   if(s.ToString().Contains("Zan GUI Components")){ Found=h; return false; }
   return true;
 }
}
"@
$cb = [WG2+EnumProc]{ param($h,$l) [WG2]::CB($h,$l) }
[WG2]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WG2]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }
[WG2]::ShowWindow($h, 1) | Out-Null
Start-Sleep -Milliseconds 300
# TOPMOST then back, and move+size, so it sits above any fullscreen app.
[WG2]::SetWindowPos($h, [IntPtr](-1), 30, 30, 1500, 950, 0x40) | Out-Null
[WG2]::SetWindowPos($h, [IntPtr](-2), 30, 30, 1500, 950, 0x40) | Out-Null
[WG2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
$r = New-Object WG2+RECT
[WG2]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
# Also save a small title-bar strip (full width x 48px) for easy download.
$strip = New-Object Drawing.Bitmap($w,48)
$g2 = [Drawing.Graphics]::FromImage($strip)
$g2.DrawImage($bmp,(New-Object Drawing.Rectangle(0,0,$w,48)),(New-Object Drawing.Rectangle(0,0,$w,48)),[Drawing.GraphicsUnit]::Pixel)
$strip.Save(($Out -replace '\.png$','_tb.png'))
Write-Output ("ok size=" + $w + "x" + $ht + " -> " + $Out)

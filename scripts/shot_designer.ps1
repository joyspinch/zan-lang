param(
    [string]$Out = "d:\project\zan-lang\build\shot_designer_custom.png",
    [int]$PaletteX = 1200,
    [int]$PaletteY = 700,
    [int]$WheelSteps = 12
)

$exe = "d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
Start-Process $exe -ArgumentList @("Designer","custom","liquidglass")
Start-Sleep -Milliseconds 1600

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WGD {
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,int d,IntPtr e);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found=IntPtr.Zero;
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L; public int T; public int R; public int B; }
 public static bool CB(IntPtr h,IntPtr l) {
   if(!IsWindowVisible(h)) return true;
   var s=new StringBuilder(512); GetWindowText(h,s,512);
   if(s.ToString().Contains("Zan GUI Components")) { Found=h; return false; }
   return true;
 }
}
"@

$cb = [WGD+EnumProc]{ param($h,$l) [WGD]::CB($h,$l) }
[WGD]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WGD]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }

[WGD]::ShowWindow($h,3) | Out-Null
[WGD]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 600
$r = New-Object WGD+RECT
[WGD]::GetWindowRect($h,[ref]$r) | Out-Null
[WGD]::SetCursorPos($r.L + $PaletteX,$r.T + $PaletteY) | Out-Null
Start-Sleep -Milliseconds 250

$WHEEL = 0x0800
for ($i = 0; $i -lt $WheelSteps; $i++) {
    [WGD]::mouse_event($WHEEL,0,0,-120,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 35
}
Start-Sleep -Milliseconds 700

[WGD]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L
$ht = $r.B - $r.T
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
$g.Dispose()
$bmp.Dispose()
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output ("DESIGNER_SHOT_OK size=" + $w + "x" + $ht + " -> " + $Out)

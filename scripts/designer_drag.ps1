param(
  [int]$X1 = 0, [int]$Y1 = 0, [int]$X2 = 0, [int]$Y2 = 0,
  [string]$Out = "d:\project\zan-lang\build\shot_drag.png",
  [switch]$Relaunch,
  [switch]$MoveOnly
)
$exe = "d:\project\zan-lang\build\gallery_test.exe"
if ($Relaunch) {
  Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 300
  Start-Process $exe -ArgumentList @("Designer")
  Start-Sleep -Milliseconds 1400
}

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WG{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
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
$cb = [WG+EnumProc]{ param($h,$l) [WG]::CB($h,$l) }
[WG]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WG]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }
[WG]::ShowWindow($h, 3) | Out-Null
[WG]::SetWindowPos($h, [IntPtr](-1), 0,0,0,0, 0x43) | Out-Null
[WG]::SetWindowPos($h, [IntPtr](-2), 0,0,0,0, 0x43) | Out-Null
[WG]::BringWindowToTop($h) | Out-Null
[WG]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object WG+RECT
[WG]::GetWindowRect($h,[ref]$r) | Out-Null

$LEFTDOWN = 0x0002; $LEFTUP = 0x0004
$sx1 = $r.L + $X1; $sy1 = $r.T + $Y1
$sx2 = $r.L + $X2; $sy2 = $r.T + $Y2

if ($MoveOnly) {
  [WG]::SetCursorPos($sx1, $sy1) | Out-Null
  Start-Sleep -Milliseconds 300
  # nudge so a WM_MOUSEMOVE is delivered
  [WG]::SetCursorPos($sx1 + 1, $sy1 + 1) | Out-Null
  Start-Sleep -Milliseconds 300
} else {
  [WG]::SetCursorPos($sx1, $sy1) | Out-Null
  Start-Sleep -Milliseconds 120
  [WG]::mouse_event($LEFTDOWN, 0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 120
  $steps = 24
  for ($i = 1; $i -le $steps; $i++) {
    $cx = [int]($sx1 + ($sx2 - $sx1) * $i / $steps)
    $cy = [int]($sy1 + ($sy2 - $sy1) * $i / $steps)
    [WG]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 25
  }
  Start-Sleep -Milliseconds 120
  [WG]::mouse_event($LEFTUP, 0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

Add-Type -AssemblyName System.Drawing
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
Write-Output ("drag rect=" + $r.L + "," + $r.T + " " + $w + "x" + $ht + " from " + $sx1 + "," + $sy1 + " to " + $sx2 + "," + $sy2 + " -> " + $Out)


param([int]$Wheel = 12, [string]$Out = "d:\project\zan-lang\build\shot_scroll.png")
$exe = "d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
Start-Process $exe -ArgumentList "Chart"
Start-Sleep -Milliseconds 1400
Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WG2{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,int d,UIntPtr e);
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
[WG2]::ShowWindow($h, 3) | Out-Null
[WG2]::SetWindowPos($h, [IntPtr](-1), 0,0,0,0, 0x43) | Out-Null
[WG2]::SetWindowPos($h, [IntPtr](-2), 0,0,0,0, 0x43) | Out-Null
[WG2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object WG2+RECT
[WG2]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
# cursor over the detail content (right ~60% of the window)
$cx = $r.L + [int]($w * 0.62); $cy = $r.T + [int]($ht * 0.5)
[WG2]::SetCursorPos($cx,$cy) | Out-Null
Start-Sleep -Milliseconds 200
for ($i=0; $i -lt $Wheel; $i++){ [WG2]::mouse_event(0x0800,0,0,-120,[UIntPtr]::Zero); Start-Sleep -Milliseconds 60 }
# let intro animations settle
Start-Sleep -Milliseconds 1200
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
Write-Output ("ok size=" + $w + "x" + $ht + " -> " + $Out)

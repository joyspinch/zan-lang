param([string]$A = "", [int]$CX = 0, [int]$CY = 0, [string]$Out = "d:\project\zan-lang\build\shot_click.png")
$exe = "d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
$argList = @()
if ($A -ne "") { $argList = $A.Split(" ") }
if ($argList.Count -gt 0) { Start-Process $exe -ArgumentList $argList } else { Start-Process $exe }
Start-Sleep -Milliseconds 1500

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WGC{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found = IntPtr.Zero;
 [StructLayout(LayoutKind.Sequential)] public struct RECT{ public int L; public int T; public int R; public int B; }
 public static bool CB(IntPtr h,IntPtr l){
   if(!IsWindowVisible(h))return true;
   var s=new StringBuilder(512);GetWindowText(h,s,512);
   if(s.ToString().Contains("Zan GUI Components")){ Found=h; return false; }
   return true;
 }
 public static void Click(int x,int y){ SetCursorPos(x,y); System.Threading.Thread.Sleep(120); mouse_event(0x0002,0,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(40); mouse_event(0x0004,0,0,0,IntPtr.Zero); }
}
"@
$cb = [WGC+EnumProc]{ param($h,$l) [WGC]::CB($h,$l) }
[WGC]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WGC]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }
[WGC]::ShowWindow($h, 1) | Out-Null
Start-Sleep -Milliseconds 300
[WGC]::SetWindowPos($h, [IntPtr](-1), 30, 30, 1500, 950, 0x40) | Out-Null
[WGC]::SetWindowPos($h, [IntPtr](-2), 30, 30, 1500, 950, 0x40) | Out-Null
[WGC]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object WGC+RECT
[WGC]::GetWindowRect($h,[ref]$r) | Out-Null
if ($CX -ne 0) { [WGC]::Click($r.L + $CX, $r.T + $CY); Start-Sleep -Milliseconds 500 }
[WGC]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = 460
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
Write-Output ("ok click=" + $CX + "," + $CY + " -> " + $Out)

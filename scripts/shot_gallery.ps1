param([string]$Comp = "", [switch]$Open, [string]$State = "", [string]$Out = "d:\project\zan-lang\build\shot_gallery.png")
$exe = "d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
$argList = @()
if ($Comp -ne "") { $argList += $Comp }
if ($Open) { $argList += "open" }
if ($State -ne "") { $argList += $State }
if ($argList.Count -gt 0) { Start-Process $exe -ArgumentList $argList } else { Start-Process $exe }
Start-Sleep -Milliseconds 1400

# PowerShell is DPI-unaware by default: on a scaled display GetWindowRect and
# CopyFromScreen then work in virtualised coordinates, so the capture is a crop
# of the window's top-left corner (2/3 of it at 150%) instead of the whole
# window. Opt into per-monitor DPI awareness before measuring anything.
Add-Type @"
using System;using System.Runtime.InteropServices;
public class DPI{
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 public static void On(){ try{ if(SetProcessDpiAwarenessContext((IntPtr)(-4))) return; }catch{} try{ SetProcessDPIAware(); }catch{} }
}
"@
[DPI]::On()

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
 // PrintWindow asks the window for its own pixels, so it does not depend on
 // the window being foreground and can never capture another app's picture
 // (CopyFromScreen grabs whatever covers the screen when raising the window
 // loses the race). Flag 0x2 is PW_RENDERFULLCONTENT, required for windows
 // that render through DirectComposition.
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
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
Start-Sleep -Milliseconds 700
$r = New-Object WG+RECT
[WG]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [WG]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
if (-not $ok) { Write-Output "printwindow-failed"; exit 1 }
$bmp.Save($Out)
Write-Output ("ok size=" + $w + "x" + $ht + " -> " + $Out)

param([string]$Comp="Icon",[int]$W=900,[int]$H=620,[string]$Out="d:\project\zan-lang\build\small.png")
$exe = "d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
Start-Process $exe -ArgumentList @($Comp)
Start-Sleep -Milliseconds 1400
Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WS{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found=IntPtr.Zero;
 [StructLayout(LayoutKind.Sequential)] public struct RECT{ public int L;public int T;public int R;public int B; }
 public static bool CB(IntPtr h,IntPtr l){ if(!IsWindowVisible(h))return true; var s=new StringBuilder(512);GetWindowText(h,s,512); if(s.ToString().Contains("Zan GUI Components")){Found=h;return false;} return true; }
}
"@
$cb=[WS+EnumProc]{ param($h,$l) [WS]::CB($h,$l) }
[WS]::EnumWindows($cb,[IntPtr]::Zero)|Out-Null
$h=[WS]::Found
if($h -eq [IntPtr]::Zero){Write-Output "not-found";exit 1}
[WS]::ShowWindow($h,1)|Out-Null
[WS]::SetWindowPos($h,[IntPtr](-1),60,60,$W,$H,0x40)|Out-Null
[WS]::SetWindowPos($h,[IntPtr](-2),60,60,$W,$H,0x40)|Out-Null
[WS]::SetForegroundWindow($h)|Out-Null
Start-Sleep -Milliseconds 800
$r=New-Object WS+RECT
[WS]::GetWindowRect($h,[ref]$r)|Out-Null
$w=$r.R-$r.L;$ht=$r.B-$r.T
Add-Type -AssemblyName System.Drawing
$bmp=New-Object Drawing.Bitmap($w,$ht)
$g=[Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
Write-Output ("ok size=" + $w + "x" + $ht)

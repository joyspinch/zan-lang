param([string]$Comp="Icon",[int]$W=760,[int]$H=900,[string]$Out="d:/project/zan-lang/build/shot_resize.png")
$exe="d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Start-Process $exe -ArgumentList $Comp
Start-Sleep -Milliseconds 1600
Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;using System.Drawing;
public class WR{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found=IntPtr.Zero;
 [StructLayout(LayoutKind.Sequential)] public struct RECT{public int L;public int T;public int R;public int B;}
 public static bool CB(IntPtr h,IntPtr l){ if(!IsWindowVisible(h))return true; var s=new StringBuilder(512);GetWindowText(h,s,512); if(s.ToString().Contains("Zan GUI Components")){Found=h;return false;} return true; }
}
"@ -ReferencedAssemblies System.Drawing
$cb=[WR+EnumProc]{param($h,$l) [WR]::CB($h,$l)}
[WR]::EnumWindows($cb,[IntPtr]::Zero)|Out-Null
$h=[WR]::Found
if($h -eq [IntPtr]::Zero){Write-Output "not-found";exit 1}
[WR]::SetWindowPos($h,[IntPtr]::Zero,80,40,$W,$H,0x0040)|Out-Null
Start-Sleep -Milliseconds 900
[WR]::SetForegroundWindow($h)|Out-Null
Start-Sleep -Milliseconds 400
$r=New-Object WR+RECT
[WR]::GetWindowRect($h,[ref]$r)|Out-Null
$bw=$r.R-$r.L; $bh=$r.B-$r.T
$bmp=New-Object System.Drawing.Bitmap($bw,$bh)
$g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size($bw,$bh)))
$bmp.Save($Out,[System.Drawing.Imaging.ImageFormat]::Png)
Write-Output ("shot ok " + $bw + "x" + $bh + " -> " + $Out)

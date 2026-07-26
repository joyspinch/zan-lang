param([int]$Skin = 10, [string]$Wall = "", [int]$WallOp = 100, [int]$CX = -1, [int]$CY = -1, [string]$Out = "d:\project\zan-lang\build\v_skin.png", [switch]$NoRestart)

$cfgDir = "$env:APPDATA\ZanIDE"
New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
$cfgPath = "$cfgDir\config.cfg"
if (-not $NoRestart) {
  $cfg = "treeW=240;bottomH=190;winW=1400;winH=900;autoRun=0;skin=$Skin;rightW=600;opacity=100;wallOpacity=$WallOp;wallpaper=$Wall;"
  Set-Content -Path $cfgPath -Value $cfg -NoNewline
  $exe = "d:\project\zan-lang\build\ZanIDE.exe"
  Get-Process ZanIDE -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 600
  Start-Process $exe
  Start-Sleep -Milliseconds 3500
}

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WS{
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
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
   if(s.ToString().Contains("Zan IDE")){ Found=h; return false; }
   return true;
 }
 public static void Click(int x,int y){ SetCursorPos(x,y); System.Threading.Thread.Sleep(150); mouse_event(0x0002,0,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(60); mouse_event(0x0004,0,0,0,IntPtr.Zero); }
}
"@
[void][WS]::SetProcessDPIAware()
$cb = [WS+EnumProc]{ param($h,$l) [WS]::CB($h,$l) }
[WS]::Found = [IntPtr]::Zero
[WS]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WS]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }
[WS]::ShowWindow($h, 1) | Out-Null
[WS]::SetWindowPos($h, [IntPtr](-1), 20, 20, 1400, 900, 0x40) | Out-Null
[WS]::SetWindowPos($h, [IntPtr](-2), 0,0,0,0, 0x43) | Out-Null
[WS]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900
$r = New-Object WS+RECT
[WS]::GetWindowRect($h,[ref]$r) | Out-Null
if ($CX -ge 0) {
  [WS]::Click($r.L + $CX, $r.T + $CY)
  Start-Sleep -Milliseconds 900
}
[WS]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
Write-Output ("ok rect=" + $r.L + "," + $r.T + " " + $w + "x" + $ht + " -> " + $Out)

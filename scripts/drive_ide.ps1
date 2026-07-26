param(
  [string]$Clicks = "",          # "x,y;x,y" in window coords, screenshot after each
  [string]$Wheels = "",          # "x,y,steps;..." applied after the clicks
  [string]$OutPrefix = "d:\project\zan-lang\build\v_step",
  [switch]$Restart,
  [int]$Skin = 10,
  [string]$Wall = "",
  [int]$WallOp = 80,
  [string]$Win = "Zan IDE",
  [string]$Keys = "",
  [string]$CtrlClicks = ""
)

if ($Restart) {
  $cfgDir = "$env:APPDATA\ZanIDE"
  New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
  $cfg = "treeW=240;bottomH=190;winW=1400;winH=900;autoRun=0;skin=$Skin;rightW=600;opacity=100;wallOpacity=$WallOp;wallpaper=$Wall;"
  Set-Content -Path "$cfgDir\config.cfg" -Value $cfg -NoNewline
  Get-Process ZanIDE -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 600
  Start-Process "d:\project\zan-lang\build\ZanIDE.exe" -RedirectStandardOutput "d:\project\zan-lang\build\ide_out.log" -RedirectStandardError "d:\project\zan-lang\build\ide_err.log"
  Start-Sleep -Milliseconds 4000
}

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WS2{
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
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte scan,uint f,IntPtr e);
 public static void CtrlClick(int x,int y){ keybd_event(0x11,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(80); SetCursorPos(x,y); System.Threading.Thread.Sleep(200); mouse_event(0x0002,0,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(80); mouse_event(0x0004,0,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(80); keybd_event(0x11,0,2,IntPtr.Zero); }
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found = IntPtr.Zero;
 public static string Title = "Zan IDE";
 [StructLayout(LayoutKind.Sequential)] public struct RECT{ public int L; public int T; public int R; public int B; }
 public static bool CB(IntPtr h,IntPtr l){
   if(!IsWindowVisible(h))return true;
   var s=new StringBuilder(512);GetWindowText(h,s,512);
   if(s.ToString().Contains(Title)){ Found=h; return false; }
   return true;
 }
 public static void Wheel(int x,int y,int steps){ SetCursorPos(x,y); System.Threading.Thread.Sleep(120); int i=0; while(i<System.Math.Abs(steps)){ uint d=(uint)(steps>0?120:-120); mouse_event(0x0800,0,0,d,IntPtr.Zero); System.Threading.Thread.Sleep(80); i++; } }
 public static void Click(int x,int y){ SetCursorPos(x,y); System.Threading.Thread.Sleep(200); mouse_event(0x0002,0,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(80); mouse_event(0x0004,0,0,0,IntPtr.Zero); }
}
"@
[void][WS2]::SetProcessDPIAware()
$cb = [WS2+EnumProc]{ param($h,$l) [WS2]::CB($h,$l) }
[WS2]::Title = $Win
[WS2]::Found = [IntPtr]::Zero
[WS2]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WS2]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }
[WS2]::ShowWindow($h, 1) | Out-Null
[WS2]::SetWindowPos($h, [IntPtr](-1), 20, 20, 1400, 900, 0x40) | Out-Null
[WS2]::SetWindowPos($h, [IntPtr](-2), 0,0,0,0, 0x43) | Out-Null
[WS2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900
Add-Type -AssemblyName System.Drawing

function Shot([string]$tag) {
  $r = New-Object WS2+RECT
  [WS2]::GetWindowRect($h,[ref]$r) | Out-Null
  # full desktop, so picker child windows are visible too
  $bw = $r.R - $r.L; $bh = $r.B - $r.T
  $bmp = New-Object Drawing.Bitmap($bw,$bh)
  $g = [Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
  $p = "$OutPrefix$tag.png"
  $bmp.Save($p)
  $g.Dispose(); $bmp.Dispose()
  Write-Output ("shot $p rect=" + $r.L + "," + $r.T + " " + $bw + "x" + $bh + " alive=" + ((Get-Process ZanIDE -ErrorAction SilentlyContinue) -ne $null))
}

$r0 = New-Object WS2+RECT
[WS2]::GetWindowRect($h,[ref]$r0) | Out-Null
$n = 0
if ($Clicks -ne "") {
  foreach ($c in $Clicks.Split(";")) {
    if ($c -eq "") { continue }
    $p = $c.Split(",")
    [WS2]::Click($r0.L + [int]$p[0], $r0.T + [int]$p[1])
    Start-Sleep -Milliseconds 1200
    $n = $n + 1
    Shot("_c$n")
  }
}
if ($CtrlClicks -ne "") {
  foreach ($c in $CtrlClicks.Split(";")) {
    if ($c -eq "") { continue }
    $p = $c.Split(",")
    [WS2]::CtrlClick($r0.L + [int]$p[0], $r0.T + [int]$p[1])
    Start-Sleep -Milliseconds 1500
    $n = $n + 1
    Shot("_x$n")
  }
}
if ($Keys -ne "") {
  $sh = New-Object -ComObject WScript.Shell
  foreach ($k in $Keys.Split("|")) {
    if ($k -eq "") { continue }
    $sh.SendKeys($k)
    Start-Sleep -Milliseconds 900
    $n = $n + 1
    Shot("_k$n")
  }
}
if ($Wheels -ne "") {
  foreach ($w in $Wheels.Split(";")) {
    if ($w -eq "") { continue }
    $p = $w.Split(",")
    [WS2]::Wheel($r0.L + [int]$p[0], $r0.T + [int]$p[1], [int]$p[2])
    Start-Sleep -Milliseconds 800
    $n = $n + 1
    Shot("_w$n")
  }
}
Shot("_end")
Write-Output ("alive=" + ((Get-Process ZanIDE -ErrorAction SilentlyContinue) -ne $null))

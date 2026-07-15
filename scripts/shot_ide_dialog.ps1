param([int]$Skin = 2, [int]$ClickX = 45, [int]$ClickY = 70,
      [string]$Title = "New Project", [string]$Title2 = "新建项目",
      [string]$Out = "d:\project\zan-lang\build\shot_ide_dialog.png")

$cfgDir = "$env:APPDATA\ZanIDE"
New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
$cfgPath = "$cfgDir\config.cfg"
$cfg = ""
if (Test-Path $cfgPath) { $cfg = Get-Content $cfgPath -Raw }
if ($cfg -match "skin=\d+") { $cfg = $cfg -replace "skin=\d+", "skin=$Skin" }
else { $cfg = $cfg + "skin=$Skin;" }
Set-Content -Path $cfgPath -Value $cfg -NoNewline

$exe = "d:\project\zan-lang\build\ZanIDE.exe"
Get-Process ZanIDE -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Start-Process $exe
Start-Sleep -Milliseconds 2800

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WGD{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static string Want="";
 public static IntPtr Found=IntPtr.Zero;
 [StructLayout(LayoutKind.Sequential)] public struct RECT{ public int L; public int T; public int R; public int B; }
 public static bool CB(IntPtr h,IntPtr l){
   if(!IsWindowVisible(h))return true;
   var s=new StringBuilder(512);GetWindowText(h,s,512);
   if(s.ToString().Contains(Want)){ Found=h; return false; }
   return true;
 }
 public static IntPtr Find(string w){ Want=w; Found=IntPtr.Zero; EnumWindows(new EnumProc(CB),IntPtr.Zero); return Found; }
 public static void Click(int x,int y){ SetCursorPos(x,y); mouse_event(0x0002,0,0,0,IntPtr.Zero); mouse_event(0x0004,0,0,0,IntPtr.Zero); }
}
"@

$h = [WGD]::Find("Zan IDE")
if ($h -eq [IntPtr]::Zero) { Write-Output "ide-not-found"; exit 1 }
[WGD]::SetWindowPos($h, [IntPtr](-1), 30, 30, 1500, 950, 0x40) | Out-Null
[WGD]::SetWindowPos($h, [IntPtr](-2), 30, 30, 1500, 950, 0x40) | Out-Null
[WGD]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
# Click the New-Project ribbon command (client coords offset by window origin).
[WGD]::Click(30 + $ClickX, 30 + $ClickY)
Start-Sleep -Milliseconds 2800

Add-Type -AssemblyName System.Drawing
$d = [WGD]::Find($Title2)
if ($d -eq [IntPtr]::Zero) { $d = [WGD]::Find($Title) }
if ($d -eq [IntPtr]::Zero) {
  # Fall back to capturing the IDE main window so the click target can be seen.
  $r2 = New-Object WGD+RECT
  [WGD]::GetWindowRect($h,[ref]$r2) | Out-Null
  $w2 = $r2.R - $r2.L; $h2 = $r2.B - $r2.T
  $bmp2 = New-Object Drawing.Bitmap($w2,$h2)
  $g2 = [Drawing.Graphics]::FromImage($bmp2)
  $g2.CopyFromScreen($r2.L,$r2.T,0,0,$bmp2.Size)
  $bmp2.Save($Out)
  Get-Process ZanIDE -ErrorAction SilentlyContinue | Stop-Process -Force
  Write-Output ("dialog-not-found; main-window captured " + $w2 + "x" + $h2 + " -> " + $Out)
  exit 1
}
[WGD]::SetForegroundWindow($d) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object WGD+RECT
[WGD]::GetWindowRect($d,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
Get-Process ZanIDE -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output ("ok size=" + $w + "x" + $ht + " -> " + $Out)

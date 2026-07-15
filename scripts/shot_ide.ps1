param([int]$Skin = 2, [string]$Out = "d:\project\zan-lang\build\shot_ide.png")

# Pre-set the persisted skin index so the IDE launches in the chosen skin.
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
public class WGI{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static IntPtr Found = IntPtr.Zero;
 [StructLayout(LayoutKind.Sequential)] public struct RECT{ public int L; public int T; public int R; public int B; }
 public static bool CB(IntPtr h,IntPtr l){
   if(!IsWindowVisible(h))return true;
   var s=new StringBuilder(512);GetWindowText(h,s,512);
   if(s.ToString().Contains("Zan IDE")){ Found=h; return false; }
   return true;
 }
}
"@
$cb = [WGI+EnumProc]{ param($h,$l) [WGI]::CB($h,$l) }
[WGI]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WGI]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }
[WGI]::ShowWindow($h, 1) | Out-Null
Start-Sleep -Milliseconds 300
[WGI]::SetWindowPos($h, [IntPtr](-1), 30, 30, 1500, 950, 0x40) | Out-Null
[WGI]::SetWindowPos($h, [IntPtr](-2), 30, 30, 1500, 950, 0x40) | Out-Null
[WGI]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900
$r = New-Object WGI+RECT
[WGI]::GetWindowRect($h,[ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap($w,$ht)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$bmp.Save($Out)
# Release the exe so a subsequent rebuild can relink it.
Get-Process ZanIDE -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output ("ok size=" + $w + "x" + $ht + " -> " + $Out)

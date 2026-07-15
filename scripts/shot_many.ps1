param([string]$Comps = "")
$exe = "d:\project\zan-lang\build\gallery_test.exe"
$outdir = "d:\project\zan-lang\build\shots"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WM{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
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
Add-Type -AssemblyName System.Drawing

function Shot([string]$name){
  Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 250
  Start-Process $exe -ArgumentList $name
  Start-Sleep -Milliseconds 1300
  [WM]::Found=[IntPtr]::Zero
  $cb = [WM+EnumProc]{ param($h,$l) [WM]::CB($h,$l) }
  [WM]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
  $h=[WM]::Found
  if($h -eq [IntPtr]::Zero){ Write-Output "$name not-found"; return }
  [WM]::ShowWindow($h,3)|Out-Null
  [WM]::SetWindowPos($h,[IntPtr](-1),0,0,0,0,0x43)|Out-Null
  [WM]::SetWindowPos($h,[IntPtr](-2),0,0,0,0,0x43)|Out-Null
  [WM]::SetForegroundWindow($h)|Out-Null
  Start-Sleep -Milliseconds 600
  $r=New-Object WM+RECT
  [WM]::GetWindowRect($h,[ref]$r)|Out-Null
  $w=$r.R-$r.L; $ht=$r.B-$r.T
  $bmp=New-Object Drawing.Bitmap($w,$ht)
  $g=[Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
  $bmp.Save("$outdir\$name.png")
  $g.Dispose(); $bmp.Dispose()
  Write-Output "$name ok ${w}x${ht}"
}

foreach($n in $Comps.Split(",")){ $nm=$n.Trim().Trim('"').Trim(); if($nm -ne ""){ Shot $nm } }
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force

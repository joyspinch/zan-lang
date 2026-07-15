param([string]$Comp = "Button", [int]$Seconds = 5)
$exe = "d:\project\zan-lang\build\gallery_test.exe"
Get-Process gallery_test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Start-Process $exe -ArgumentList $Comp
Start-Sleep -Milliseconds 1600

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class WS{
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
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
$cb = [WS+EnumProc]{ param($h,$l) [WS]::CB($h,$l) }
[WS]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
$h = [WS]::Found
if ($h -eq [IntPtr]::Zero) { Write-Output "not-found"; exit 1 }
[WS]::SetForegroundWindow($h) | Out-Null
$r = New-Object WS+RECT
[WS]::GetWindowRect($h,[ref]$r) | Out-Null
# A point well inside the right detail pane.
$px = [int]($r.L + ($r.R-$r.L)*0.7)
$py = [int]($r.T + ($r.B-$r.T)*0.5)
[WS]::SetCursorPos($px,$py) | Out-Null
$lp = [IntPtr](($py -shl 16) -bor ($px -band 0xFFFF))

$p = Get-Process gallery_test
$t1 = $p.TotalProcessorTime.TotalMilliseconds
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$down = $true
$flip = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
  if ($flip.Elapsed.TotalMilliseconds -gt 500) { $down = -not $down; $flip.Restart() }
  $delta = if ($down) { -120 } else { 120 }
  $wp = [IntPtr]([int64]($delta -shl 16))
  [WS]::PostMessage($h, 0x020A, $wp, $lp) | Out-Null
  Start-Sleep -Milliseconds 16
}
$p.Refresh()
$t2 = $p.TotalProcessorTime.TotalMilliseconds
$pct = [math]::Round(($t2-$t1)/($Seconds*1000)*100,1)
Write-Output ("scroll_cpu_percent_of_one_core=" + $pct + " over " + $Seconds + "s comp=" + $Comp)

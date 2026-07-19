param([int]$x=800,[int]$y=500,[int]$ticks=-3)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class S {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int X,int Y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
}
"@
[S]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
[S]::SetCursorPos($x,$y) | Out-Null
Start-Sleep -Milliseconds 100
$i = 0
while ($i -lt [Math]::Abs($ticks)) {
  $delta = if ($ticks -lt 0) { [uint32]4294967176 } else { [uint32]120 }
  [S]::mouse_event(0x0800,0,0,$delta,[IntPtr]::Zero)  # WHEEL
  Start-Sleep -Milliseconds 60
  $i = $i + 1
}
Write-Output ("SCROLL " + $ticks)

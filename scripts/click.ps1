param([int]$x=0,[int]$y=0)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class C {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int X,int Y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
}
"@
[C]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
[C]::SetCursorPos($x,$y) | Out-Null
Start-Sleep -Milliseconds 120
[C]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero)  # left down
Start-Sleep -Milliseconds 60
[C]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)  # left up
Write-Output ("CLICK " + $x + "," + $y)

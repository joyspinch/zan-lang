Set-Location 'd:\project\zan-lang'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W2 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int ht,bool r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int ht,uint flags);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte scan,uint flags,IntPtr extra);
}
"@
$p = Get-Process gallery_test -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output "NOPROC"; exit }
$h = $p.MainWindowHandle
# ALT tap: lets a process take foreground past Windows' foreground lock.
[W2]::keybd_event(0x12,0,0,[IntPtr]::Zero)
[W2]::keybd_event(0x12,0,2,[IntPtr]::Zero)
[W2]::ShowWindow($h,9) | Out-Null      # SW_RESTORE
[W2]::MoveWindow($h,120,80,1600,1120,$true) | Out-Null
$TOP=[IntPtr](-1)
[W2]::SetWindowPos($h,$TOP,0,0,0,0,0x0003) | Out-Null    # TOPMOST, keep it there
[W2]::BringWindowToTop($h) | Out-Null
[W2]::SetForegroundWindow($h) | Out-Null
Write-Output ("HWND " + $h)

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class GW {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int ht,bool r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int ht,uint f);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte sc,uint f,IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
}
"@
[GW]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$target = $env:GW_TITLE
if (-not $target) { $target = "Zan GUI Components" }
$found = [IntPtr]::Zero
$cb = [GW+EnumWindowsProc]{
  param($h,$l)
  if (-not [GW]::IsWindowVisible($h)) { return $true }
  $sb = New-Object System.Text.StringBuilder 512
  [GW]::GetWindowText($h,$sb,512) | Out-Null
  if ($sb.ToString() -eq $script:target) { $script:found = $h; return $false }
  return $true
}
[GW]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
if ($found -eq [IntPtr]::Zero) { Write-Output "NOTFOUND"; exit }
$h = $found
if ($env:GW_MOVE -eq "1") { [GW]::MoveWindow($h,80,80,1760,1200,$true) | Out-Null }
[GW]::keybd_event(0x12,0,0,[IntPtr]::Zero)
[GW]::keybd_event(0x12,0,2,[IntPtr]::Zero)
[GW]::ShowWindow($h,9) | Out-Null
$TOP=[IntPtr](-1)
if ($env:GW_TOP -eq "0") { $TOP=[IntPtr](-2) }
[GW]::SetWindowPos($h,$TOP,0,0,0,0,0x0003) | Out-Null
[GW]::BringWindowToTop($h) | Out-Null
[GW]::SetForegroundWindow($h) | Out-Null
$r = New-Object GW+RECT
[GW]::GetWindowRect($h,[ref]$r) | Out-Null
Write-Output ("RECT " + $r.Left + " " + $r.Top + " " + $r.Right + " " + $r.Bottom)

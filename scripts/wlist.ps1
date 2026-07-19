Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WL {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
  public static List<string> Collect() {
    List<string> res = new List<string>();
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      StringBuilder sb = new StringBuilder(512);
      GetWindowText(h, sb, 512);
      string t = sb.ToString();
      if (t.Length == 0) return true;
      uint pid; GetWindowThreadProcessId(h, out pid);
      RECT r; GetWindowRect(h, out r);
      res.Add("pid=" + pid + " hwnd=" + h.ToInt64() + " rect=" + r.Left + "," + r.Top + "," + r.Right + "," + r.Bottom + " [" + t + "]");
      return true;
    }, IntPtr.Zero);
    return res;
  }
}
"@
foreach ($line in [WL]::Collect()) {
  $parts = $line -split ' ',2
  $pid2 = ($parts[0] -replace 'pid=','')
  $pn = ""
  try { $pn = (Get-Process -Id ([int]$pid2)).ProcessName } catch {}
  if ($pn -eq "gallery_test") { Write-Output ("GALLERY $line") }
}

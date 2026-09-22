param(
    [string]$exePath,
    [int]$delayMs = 800,
    [string]$outPng = "_scratch/gui_preview.png"
)

if (-not (Test-Path $exePath)) {
    Write-Error "Target exe not found: $exePath"
    exit 1
}

$parentDir = Split-Path -Parent $outPng
if ($parentDir -and -not (Test-Path $parentDir)) {
    New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;

public class WinCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    public static void CaptureWindow(IntPtr hWnd, string path) {
        RECT r;
        GetWindowRect(hWnd, out r);
        int w = r.Right - r.Left;
        int h = r.Bottom - r.Top;
        if (w <= 0 || h <= 0) { w = 900; h = 600; }
        using (Bitmap bmp = new Bitmap(w, h)) {
            using (Graphics g = Graphics.FromImage(bmp)) {
                g.CopyFromScreen(r.Left, r.Top, 0, 0, new Size(w, h));
            }
            bmp.Save(path, ImageFormat.Png);
        }
    }
}
"@ -ReferencedAssemblies System.Drawing, System.Windows.Forms

$proc = Start-Process -FilePath $exePath -PassThru
Start-Sleep -Milliseconds $delayMs

$hWnd = $proc.MainWindowHandle
$retry = 0
while ($hWnd -eq [IntPtr]::Zero -and $retry -lt 15) {
    Start-Sleep -Milliseconds 100
    $proc.Refresh()
    $hWnd = $proc.MainWindowHandle
    $retry++
}

if ($hWnd -ne [IntPtr]::Zero) {
    [WinCapture]::SetForegroundWindow($hWnd)
    Start-Sleep -Milliseconds 80
    [WinCapture]::CaptureWindow($hWnd, $outPng)
    Write-Output "Captured window handle $hWnd to $outPng"
} else {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bmp.Save($outPng, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Write-Output "Captured primary screen to $outPng (MainWindowHandle not ready)"
}

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue

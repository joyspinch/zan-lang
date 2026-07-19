# Bakes a high-resolution anti-aliased ASCII glyph atlas (code points 32..126)
# for the GameKit smooth font. The atlas is rendered at M x the design cell size
# so text stays razor sharp when the scene is rasterized at native 4K.
#
# Output:
#   examples/game/common/assets/font.png  - white glyphs, alpha = coverage
#   examples/game/common/assets/font.txt  - "designCellW designCellH cols pad atlasScale"
#                                            followed by 95 advance widths (design units)
param(
    [string]$FontName = "Segoe UI",
    [int]$M = 4,
    [int]$DesignCellH = 57,
    [int]$Cols = 16
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
public static class AtlasConv {
    // Convert white-on-black BGRA to white RGBA with alpha = luminance.
    public static void ToAlpha(byte[] buf) {
        for (int i = 0; i < buf.Length; i += 4) {
            byte a = buf[i + 2]; // red channel of white-on-black == coverage
            buf[i] = 255; buf[i + 1] = 255; buf[i + 2] = 255; buf[i + 3] = a;
        }
    }
}
"@

$style = [System.Drawing.FontStyle]::Bold
$fam = New-Object System.Drawing.FontFamily($FontName)
$em = $fam.GetEmHeight($style)
$lineSp = $fam.GetLineSpacing($style)
$physCellH = $DesignCellH * $M
$emSize = [double]$physCellH * $em / $lineSp
$font = New-Object System.Drawing.Font($fam, [single]$emSize, $style, [System.Drawing.GraphicsUnit]::Pixel)

$sf = [System.Drawing.StringFormat]::GenericTypographic
$sf.FormatFlags = $sf.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces

# Measure advances (design units) and the max cell width.
$tmp = New-Object System.Drawing.Bitmap(8, 8)
$tg = [System.Drawing.Graphics]::FromImage($tmp)
$tg.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$advDesign = New-Object int[] 95
$maxAdv = 1
for ($gi = 0; $gi -lt 95; $gi++) {
    $c = [char]($gi + 32)
    $w = $tg.MeasureString([string]$c, $font, [int]::MaxValue, $sf).Width
    if ($c -eq ' ') { $w = $emSize * 0.30 }
    $d = [int][math]::Ceiling($w / $M)
    if ($d -lt 2) { $d = 2 }
    $advDesign[$gi] = $d
    if ($d -gt $maxAdv) { $maxAdv = $d }
}
$tg.Dispose(); $tmp.Dispose()

$pad = 3
$designCellW = $maxAdv + $pad
$physCellW = $designCellW * $M
$rows = [int][math]::Ceiling(95.0 / $Cols)
$W = $Cols * $physCellW
$H = $rows * $physCellH

$bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Black)
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
$leftPad = [int]($M)
for ($gi = 0; $gi -lt 95; $gi++) {
    $c = [char]($gi + 32)
    $col = $gi % $Cols
    $row = [int][math]::Floor($gi / $Cols)
    $x = $col * $physCellW + $leftPad
    $y = $row * $physCellH
    $g.DrawString([string]$c, $font, $white, [single]$x, [single]$y, $sf)
}
$g.Dispose()

$rect = New-Object System.Drawing.Rectangle(0, 0, $W, $H)
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$len = $data.Stride * $H
$buf = New-Object byte[] $len
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $len)
[AtlasConv]::ToAlpha($buf)
[System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $data.Scan0, $len)
$bmp.UnlockBits($data)

$outPng = "examples\game\common\assets\font.png"
$outTxt = "examples\game\common\assets\font.txt"
$bmp.Save((Join-Path (Get-Location) $outPng), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

$sb = New-Object System.Text.StringBuilder
[void]$sb.Append("$designCellW $DesignCellH $Cols $pad $M`n")
[void]$sb.Append(($advDesign -join " "))
[System.IO.File]::WriteAllText((Join-Path (Get-Location) $outTxt), $sb.ToString())

Write-Output ("BAKED {0}x{1} font={2} cell={3}x{4} M={5} atlas={6}x{7}" -f $designCellW,$DesignCellH,$FontName,$physCellW,$physCellH,$M,$W,$H)

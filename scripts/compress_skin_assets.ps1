param(
  [Parameter(Mandatory=$true)][string]$Dir,
  [int]$Quality = 88,
  [int]$MaxWidth = 1600
)
# Re-encodes generated skin artwork for shipping: opaque backgrounds become
# JPEGs (the model returns lossless PNGs of 1-2 MB, which bloats the repo and
# the app's startup decode), artwork with transparency stays PNG but is scaled
# down. Visually identical at UI sizes, roughly 10x smaller.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function HasAlpha([System.Drawing.Bitmap]$bmp) {
  if (-not $bmp.PixelFormat.ToString().Contains("Argb")) { return $false }
  $sx = [Math]::Max(1, [int]($bmp.Width / 64))
  $sy = [Math]::Max(1, [int]($bmp.Height / 64))
  for ($y = 0; $y -lt $bmp.Height; $y += $sy) {
    for ($x = 0; $x -lt $bmp.Width; $x += $sx) {
      if ($bmp.GetPixel($x, $y).A -lt 250) { return $true }
    }
  }
  return $false
}

$enc = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
  Where-Object { $_.MimeType -eq "image/jpeg" }
$prm = New-Object System.Drawing.Imaging.EncoderParameters 1
$prm.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter(
  [System.Drawing.Imaging.Encoder]::Quality, [int]$Quality)

Get-ChildItem -Path $Dir -Recurse -Filter *.png | ForEach-Object {
  $src = $_.FullName
  $bmp = New-Object System.Drawing.Bitmap $src
  $w = $bmp.Width
  $h = $bmp.Height
  if ($w -gt $MaxWidth) { $h = [int]($h * $MaxWidth / $w); $w = $MaxWidth }
  $alpha = HasAlpha $bmp
  $scaled = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($scaled)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  if (-not $alpha) { $g.Clear([System.Drawing.Color]::White) }
  $g.DrawImage($bmp, 0, 0, $w, $h)
  $g.Dispose()
  $bmp.Dispose()
  $before = (Get-Item $src).Length
  if ($alpha) {
    $out = $src
    $scaled.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
  } else {
    $out = [IO.Path]::ChangeExtension($src, ".jpg")
    $scaled.Save($out, $enc, $prm)
    Remove-Item $src -Force
  }
  $scaled.Dispose()
  Write-Output ("{0} -> {1}  {2}KB -> {3}KB" -f $_.Name, [IO.Path]::GetFileName($out),
    [int]($before / 1024), [int]((Get-Item $out).Length / 1024))
}

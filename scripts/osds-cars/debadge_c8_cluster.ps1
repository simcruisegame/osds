# C8 (meridian_z, #43): paint the crossed-flags brand mark out of the digital cluster texture
# (Gauges_baseColor.png, tach centre; the image is stored upside down) with the cluster's own
# near-black background. Idempotent. Run from the repo root:
#   powershell -ExecutionPolicy Bypass -File scripts\osds-cars\debadge_c8_cluster.ps1
Add-Type -AssemblyName System.Drawing
$path = Join-Path (Get-Location) 'data\art\traffic\c8-790c40cc\textures\Gauges_baseColor.png'
$src = [System.Drawing.Bitmap]::FromFile($path)
$bmp = New-Object System.Drawing.Bitmap($src.Width, $src.Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.DrawImage($src, 0, 0, $src.Width, $src.Height); $g.Dispose(); $src.Dispose()
$bg = [System.Drawing.Color]::FromArgb(255, 16, 12, 24)   # sampled beside the mark
$x0 = 484; $x1 = 538; $y0 = 297; $y1 = 340
for ($y = $y0; $y -le $y1; $y++) { for ($x = $x0; $x -le $x1; $x++) { $bmp.SetPixel($x, $y, $bg) } }
$tmp = $path + '.tmp'
$bmp.Save($tmp, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Move-Item -Force $tmp $path
"painted $path"

# --- Gear-selector emblem (the embossed crossed-flags and bowtie relief on the centre-console
# "chevron" panel, Interior_baseColor.png, UV region x 285-470, y 92-262 of 512). The relief is
# low-contrast (luma 33-49), so every dark pixel in the panel is set to the flat panel grey; the
# labels (text box, "IA", the three dots) and the neighbouring grey button are left alone.
$ipath = Join-Path (Get-Location) 'data\art\traffic\c8-790c40cc\textures\Interior_baseColor.png'
$s2 = [System.Drawing.Bitmap]::FromFile($ipath)
$im = New-Object System.Drawing.Bitmap($s2.Width, $s2.Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g2 = [System.Drawing.Graphics]::FromImage($im); $g2.DrawImage($s2, 0, 0, $s2.Width, $s2.Height); $g2.Dispose(); $s2.Dispose()
$flat = [System.Drawing.Color]::FromArgb(255, 41, 41, 41)
function InBox($x, $y, $x0, $y0, $x1, $y1) { return ($x -ge $x0 -and $x -le $x1 -and $y -ge $y0 -and $y -le $y1) }
function InDot($x, $y, $cx, $cy, $r) { return ((($x - $cx) * ($x - $cx) + ($y - $cy) * ($y - $cy)) -le ($r * $r)) }
for ($y = 90; $y -le 262; $y++) {
  for ($x = 285; $x -le 470; $x++) {
    if ($x -gt 440 -and $y -lt 190) { continue }
    if (InBox $x $y 270 84 345 150) { continue }
    if (InBox $x $y 338 224 392 270) { continue }
    if (InBox $x $y 376 203 396 222) { continue }
    if (InDot $x $y 365 215 10) { continue }
    if (InDot $x $y 401 236 10) { continue }
    if (InDot $x $y 401 256 10) { continue }
    $c = $im.GetPixel($x, $y)
    $l = ($c.R + $c.G + $c.B) / 3
    if ($l -le 80) { $im.SetPixel($x, $y, $flat) }
  }
}
$itmp = $ipath + '.tmp'
$im.Save($itmp, [System.Drawing.Imaging.ImageFormat]::Png); $im.Dispose()
Move-Item -Force $itmp $ipath
"painted $ipath"

# --- Speaker-trim strip (same atlas): a Corvette stingray silhouette sits above a "BOSE ...
# PERFORMANCE SERIES" lettering strip. Both are third-party marks: fill the silhouette with the
# dark panel colour and the lettering with the strip's own grey. Idempotent.
$jp = Join-Path (Get-Location) 'data\art\traffic\c8-790c40cc\textures\Interior_baseColor.png'
$s3 = [System.Drawing.Bitmap]::FromFile($jp)
$jm = New-Object System.Drawing.Bitmap($s3.Width, $s3.Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g3 = [System.Drawing.Graphics]::FromImage($jm); $g3.DrawImage($s3, 0, 0, $s3.Width, $s3.Height); $g3.Dispose(); $s3.Dispose()
$dark = [System.Drawing.Color]::FromArgb(255, 41, 36, 41)
$grey = [System.Drawing.Color]::FromArgb(255, 132, 134, 132)
for ($y = 279; $y -le 303; $y++) { for ($x = 212; $x -le 300; $x++) { $jm.SetPixel($x, $y, $dark) } }
for ($y = 304; $y -le 333; $y++) { for ($x = 86; $x -le 417; $x++) { $jm.SetPixel($x, $y, $grey) } }
$jtmp = $jp + '.tmp'
$jm.Save($jtmp, [System.Drawing.Imaging.ImageFormat]::Png); $jm.Dispose()
Move-Item -Force $jtmp $jp
"painted $jp"

# --- Overhead-console button row (same atlas): the blue circle labelled "ON" is the OnStar
# button mark; fill it with the row's own dark. Idempotent.
$kp = Join-Path (Get-Location) 'data\art\traffic\c8-790c40cc\textures\Interior_baseColor.png'
$s4 = [System.Drawing.Bitmap]::FromFile($kp)
$km = New-Object System.Drawing.Bitmap($s4.Width, $s4.Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g4 = [System.Drawing.Graphics]::FromImage($km); $g4.DrawImage($s4, 0, 0, $s4.Width, $s4.Height); $g4.Dispose(); $s4.Dispose()
$row = [System.Drawing.Color]::FromArgb(255, 41, 40, 41)
for ($y = 350; $y -le 375; $y++) { for ($x = 347; $x -le 369; $x++) { $km.SetPixel($x, $y, $row) } }
$ktmp = $kp + '.tmp'
$km.Save($ktmp, [System.Drawing.Imaging.ImageFormat]::Png); $km.Dispose()
Move-Item -Force $ktmp $kp
"painted $kp"

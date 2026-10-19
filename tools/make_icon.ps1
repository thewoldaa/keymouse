#requires -Version 5.1
<#
.SYNOPSIS
    Generates resources/keymouse.ico.

.DESCRIPTION
    The application icon is generated rather than committed as an opaque binary,
    so a palette change can be applied by editing one file and re-running this
    script. Windows PowerShell only: it uses System.Drawing, which ships with
    the .NET Framework and needs no extra downloads.

    Sizes below 128 use a 32bpp DIB frame, larger sizes use a PNG frame. That is
    the same layout modern icon tools emit: PNG compresses the big frames,
    DIB keeps the small ones readable on older shell code paths.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/make_icon.ps1
#>
[CmdletBinding()]
param(
    [string]$Output
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Output) {
    $Output = Join-Path $repoRoot 'resources\keymouse.ico'
}

# Palette, kept in step with src/ui/theme.h.
$Background = [System.Drawing.Color]::FromArgb(255, 0x15, 0x18, 0x1D)
$Accent = [System.Drawing.Color]::FromArgb(255, 0x4C, 0xC2, 0xA6)
$AccentDim = [System.Drawing.Color]::FromArgb(255, 0x2F, 0x8A, 0x76)
$Slot = [System.Drawing.Color]::FromArgb(255, 0x11, 0x2A, 0x25)

function New-RoundedPath {
    param([double]$X, [double]$Y, [double]$Width, [double]$Height, [double]$Radius)

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = [Math]::Min([Math]::Min($Radius * 2, $Width), $Height)

    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()

    return $path
}

function New-IconBitmap {
    param([int]$Size)

    $bitmap = New-Object System.Drawing.Bitmap(
        $Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $s = [double]$Size

    $backgroundPath = New-RoundedPath ($s * 0.02) ($s * 0.02) ($s * 0.96) ($s * 0.96) ($s * 0.22)
    $backgroundBrush = New-Object System.Drawing.SolidBrush($Background)
    $graphics.FillPath($backgroundBrush, $backgroundPath)

    # The mouse occupies the upper part; the keys row only exists once there is
    # room for it without turning into mush.
    $keysHeight = if ($Size -ge 32) { $s * 0.13 } else { 0.0 }
    $mouseWidth = $s * 0.40
    $mouseHeight = $s * 0.60 - $keysHeight
    $mouseX = ($s - $mouseWidth) / 2.0
    $mouseY = $s * 0.36 - ($mouseHeight / 2.0) - ($keysHeight * 0.2)
    if ($Size -lt 32) {
        $mouseHeight = $s * 0.60
        $mouseY = ($s - $mouseHeight) / 2.0
    }

    $bodyPath = New-RoundedPath $mouseX $mouseY $mouseWidth $mouseHeight ($mouseWidth * 0.46)
    $accentBrush = New-Object System.Drawing.SolidBrush($Accent)
    $graphics.FillPath($accentBrush, $bodyPath)

    # Wheel slot: the one detail that separates a mouse from a pill.
    $wheelWidth = [Math]::Max(1.0, $s * 0.045)
    $wheelHeight = [Math]::Max(2.0, $s * 0.14)
    $wheelBrush = New-Object System.Drawing.SolidBrush($Slot)
    $graphics.FillRectangle($wheelBrush, ($s - $wheelWidth) / 2.0,
                            $mouseY + $mouseHeight * 0.15, $wheelWidth, $wheelHeight)

    if ($keysHeight -gt 0) {
        $keyWidth = $s * 0.19
        $keyGap = $s * 0.055
        $totalWidth = ($keyWidth * 3) + ($keyGap * 2)
        $keyX = ($s - $totalWidth) / 2.0
        $keyY = $s * 0.80
        $keyBrush = New-Object System.Drawing.SolidBrush($AccentDim)

        for ($i = 0; $i -lt 3; $i++) {
            $x = $keyX + ($i * ($keyWidth + $keyGap))
            $keyPath = New-RoundedPath $x $keyY $keyWidth $keysHeight ($keysHeight * 0.35)
            $graphics.FillPath($keyBrush, $keyPath)
            $keyPath.Dispose()
        }

        $keyBrush.Dispose()
    }

    $accentBrush.Dispose()
    $wheelBrush.Dispose()
    $backgroundBrush.Dispose()
    $backgroundPath.Dispose()
    $bodyPath.Dispose()
    $graphics.Dispose()

    return $bitmap
}

function Get-PngBytes {
    param([System.Drawing.Bitmap]$Bitmap)

    $stream = New-Object System.IO.MemoryStream
    $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $stream.ToArray()
    $stream.Dispose()
    # The leading comma keeps the array intact: returning it bare lets
    # PowerShell unroll it to Object[], which BinaryWriter then coerces to a
    # single byte instead of writing the whole frame.
    return ,$bytes
}

function Get-DibBytes {
    param([System.Drawing.Bitmap]$Bitmap)

    $width = $Bitmap.Width
    $height = $Bitmap.Height
    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($stream)

    # BITMAPINFOHEADER. biHeight is doubled because the DIB holds the colour
    # bitmap followed by the (unused) AND mask.
    $writer.Write([UInt32]40)
    $writer.Write([Int32]$width)
    $writer.Write([Int32]($height * 2))
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]0)
    $writer.Write([UInt32]($width * $height * 4))
    $writer.Write([Int32]0)
    $writer.Write([Int32]0)
    $writer.Write([UInt32]0)
    $writer.Write([UInt32]0)

    for ($y = $height - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $width; $x++) {
            $pixel = $Bitmap.GetPixel($x, $y)
            $writer.Write([byte]$pixel.B)
            $writer.Write([byte]$pixel.G)
            $writer.Write([byte]$pixel.R)
            $writer.Write([byte]$pixel.A)
        }
    }

    $maskRowBytes = [Math]::Floor(($width + 31) / 32) * 4
    $writer.Write((New-Object byte[] ($maskRowBytes * $height)))
    $writer.Flush()

    $bytes = $stream.ToArray()
    $writer.Dispose()
    $stream.Dispose()
    # See Get-PngBytes: the comma preserves the array type through the return.
    return ,$bytes
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$frames = @()

foreach ($size in $sizes) {
    $bitmap = New-IconBitmap $size
    [byte[]]$data = if ($size -ge 128) { Get-PngBytes $bitmap } else { Get-DibBytes $bitmap }
    $frames += [pscustomobject]@{ Size = $size; Data = $data }
    $bitmap.Dispose()
}

$directory = Split-Path -Parent $Output
if (-not (Test-Path $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($stream)

# ICONDIR
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$frames.Count)

# ICONDIRENTRY for each frame.
$offset = 6 + (16 * $frames.Count)
foreach ($frame in $frames) {
    # 256 is encoded as 0 in a single byte.
    $dimension = if ($frame.Size -ge 256) { 0 } else { $frame.Size }
    $writer.Write([byte]$dimension)
    $writer.Write([byte]$dimension)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$frame.Data.Length)
    $writer.Write([UInt32]$offset)
    $offset += $frame.Data.Length
}

foreach ($frame in $frames) {
    $writer.Write($frame.Data)
}

$writer.Flush()
[System.IO.File]::WriteAllBytes($Output, $stream.ToArray())
$writer.Dispose()
$stream.Dispose()

Write-Host ("Wrote {0} ({1} bytes, sizes: {2})" -f `
    $Output, (Get-Item $Output).Length, ($sizes -join ', '))

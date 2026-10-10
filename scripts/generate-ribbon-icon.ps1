param([string]$OutputPath = (Join-Path $PSScriptRoot '..\src\assets\ribbon.ico'))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$dir=Split-Path -Parent $OutputPath
if(!(Test-Path $dir)){ New-Item -ItemType Directory -Path $dir -Force | Out-Null }
function Fill-Shape {
 param($gfx,$brush,[single[]]$coordinates)
 $pts=New-Object 'System.Drawing.PointF[]' ($coordinates.Length/2)
 for($i=0;$i -lt $pts.Length;$i++){
   $pts[$i]=New-Object System.Drawing.PointF([single]$coordinates[$i*2],[single]$coordinates[$i*2+1])
 }
 $gfx.FillPolygon($brush,$pts)
}
$frames=@()
foreach($size in @(16,24,32,48,256)){
 $bmp=New-Object System.Drawing.Bitmap($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
 $gfx=[System.Drawing.Graphics]::FromImage($bmp)
 $gfx.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
 $gfx.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
 $gfx.Clear([System.Drawing.Color]::Transparent)
 $gfx.ScaleTransform([single]($size/256.0),[single]($size/256.0))
 $pink=New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#F58EB8'))
 $shade=New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#BE628D'))
 $light=New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#FFBFD8'))
 Fill-Shape $gfx $shade ([single[]]@(112,137,65,227,93,214,111,244,139,159))
 Fill-Shape $gfx $shade ([single[]]@(145,137,191,227,164,214,145,244,117,159))
 Fill-Shape $gfx $pink ([single[]]@(121,121,87,87,43,79,20,104,30,144,74,162,122,143))
 Fill-Shape $gfx $pink ([single[]]@(135,121,169,87,213,79,236,104,226,144,182,162,134,143))
 Fill-Shape $gfx $light ([single[]]@(112,125,84,105,48,110,84,132))
 Fill-Shape $gfx $light ([single[]]@(144,125,172,105,208,110,172,132))
 Fill-Shape $gfx $shade ([single[]]@(114,141,83,157,46,141,93,133))
 Fill-Shape $gfx $shade ([single[]]@(142,141,173,157,210,141,163,133))
 $gfx.FillEllipse($shade,108,107,40,46)
 $gfx.FillEllipse($pink,111,110,34,40)
 $gfx.FillEllipse($light,116,115,22,11)
 $stream=New-Object System.IO.MemoryStream
 $bmp.Save($stream,[System.Drawing.Imaging.ImageFormat]::Png)
 $frames+=,[PSCustomObject]@{Size=$size; Bytes=$stream.ToArray()}
 $stream.Dispose();$light.Dispose();$shade.Dispose();$pink.Dispose();$gfx.Dispose();$bmp.Dispose()
}
$out=New-Object System.IO.MemoryStream
$w=New-Object System.IO.BinaryWriter($out)
$w.Write([UInt16]0);$w.Write([UInt16]1);$w.Write([UInt16]$frames.Count)
$offset=6+16*$frames.Count
foreach($f in $frames){
 $w.Write([byte]($f.Size -band 255));$w.Write([byte]($f.Size -band 255))
 $w.Write([byte]0);$w.Write([byte]0);$w.Write([UInt16]1);$w.Write([UInt16]32)
 $w.Write([UInt32]$f.Bytes.Length);$w.Write([UInt32]$offset)
 $offset+=$f.Bytes.Length
}
foreach($f in $frames){$w.Write([byte[]]$f.Bytes)}
[System.IO.File]::WriteAllBytes([System.IO.Path]::GetFullPath($OutputPath),$out.ToArray())
$w.Dispose();$out.Dispose()
Write-Output "Ribbon icon generated: $OutputPath (16,24,32,48,256 px)"

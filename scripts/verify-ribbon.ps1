param([string]$IconPath = (Join-Path $PSScriptRoot '..\src\assets\ribbon.ico'), [string]$ExecutablePath = (Join-Path $PSScriptRoot '..\bin\24510_0rb.exe'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$bytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($IconPath))
if ($bytes.Length -lt 6) { throw 'ICO is shorter than its header.' }
$reserved = [BitConverter]::ToUInt16($bytes, 0); $type = [BitConverter]::ToUInt16($bytes, 2); $count = [BitConverter]::ToUInt16($bytes, 4)
if ($reserved -ne 0 -or $type -ne 1 -or $count -ne 5) { throw "Invalid ICO header: reserved=$reserved type=$type count=$count" }
$expected = @(16, 24, 32, 48, 256); $actual = @()
for ($i = 0; $i -lt $count; $i++) {
    $entry = 6 + 16 * $i; $width = [int]$bytes[$entry]; $height = [int]$bytes[$entry + 1]
    if ($width -eq 0) { $width = 256 }; if ($height -eq 0) { $height = 256 }
    $length = [BitConverter]::ToUInt32($bytes, $entry + 8); $offset = [BitConverter]::ToUInt32($bytes, $entry + 12)
    if ($offset + $length -gt $bytes.Length -or $length -lt 24) { throw "Invalid image bounds in ICO entry $i." }
    $signature = [byte[]](0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a)
    for ($j = 0; $j -lt 8; $j++) { if ($bytes[$offset + $j] -ne $signature[$j]) { throw "ICO entry $i is not PNG-backed." } }
    $actual += $width; Write-Output "ICO frame: ${width}x${height}, PNG-backed, $length bytes"
}
if (($actual -join ',') -ne ($expected -join ',')) { throw "Unexpected ICO frame sizes: $($actual -join ',')" }
if (!(Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) { throw "Executable missing: $ExecutablePath" }
$api = @'
using System;
using System.Runtime.InteropServices;
public static class OrbIconResource {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
 [DllImport("kernel32.dll", SetLastError=true)]
 public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
 [DllImport("kernel32.dll", SetLastError=true)]
 public static extern uint SizeofResource(IntPtr module, IntPtr resource);
 [DllImport("kernel32.dll", SetLastError=true)]
 public static extern bool FreeLibrary(IntPtr module);
}
'@
Add-Type -TypeDefinition $api -ErrorAction Stop
$module = [OrbIconResource]::LoadLibraryEx([IO.Path]::GetFullPath($ExecutablePath),[IntPtr]::Zero,[uint32]2)
if ($module -eq [IntPtr]::Zero) { throw "Cannot load EXE as data image: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
try {
 $group = [OrbIconResource]::FindResource($module,[IntPtr]302,[IntPtr]14)
 if($group -eq [IntPtr]::Zero) { throw "Missing RT_GROUP_ICON IDI_ORB=302: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
 $size = [OrbIconResource]::SizeofResource($module,$group)
 if($size -lt 20) { throw "Icon-group resource too short: $size" }
 Write-Output "PE group icon: ID 302 RT_GROUP_ICON size=$size bytes"
}
finally { [void][OrbIconResource]::FreeLibrary($module) }
Write-Output "RIBBON_VERIFY PASS"

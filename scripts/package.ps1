param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$Development
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $version=[IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim()
    if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'VERSION must be a semantic version.'}
    $status=@(& git status --porcelain=v1); if($LASTEXITCODE){throw 'Cannot inspect Git source state.'}
    $dirty=$status.Count -ne 0
    if($dirty -and -not $Development){throw 'Release packaging requires clean source. Use -Development only for a private review package.'}
    $revision=(& git rev-parse HEAD).Trim(); if($LASTEXITCODE){throw 'Cannot read source revision.'}
    $gcc=$env:ORB_GCC
    if(-not $gcc -and $env:MSYS2_LOCATION){$gcc=Join-Path $env:MSYS2_LOCATION 'ucrt64\bin\gcc.exe'}
    if(-not $gcc){foreach($candidate in @('C:\msys64\ucrt64\bin\gcc.exe','C:\tools\msys64\ucrt64\bin\gcc.exe')){if(Test-Path -LiteralPath $candidate){$gcc=$candidate;break}}}
    if(-not $gcc){$gcc=(Get-Command gcc.exe -ErrorAction Stop).Source}
    $gcc=(Resolve-Path -LiteralPath $gcc).Path; $compilerDirectory=Split-Path $gcc -Parent
    $savedPath=$env:PATH; $env:PATH=$compilerDirectory+';'+$env:PATH
    try {
        $target=(& $gcc -dumpmachine).Trim(); if($LASTEXITCODE -or $target -ne 'x86_64-w64-mingw32'){throw 'x64 UCRT64 GCC required.'}
        $compiler=(@(& $gcc --version))[0]; if($LASTEXITCODE){throw 'Compiler version unavailable.'}
        $objdump=Join-Path $compilerDirectory 'objdump.exe'; $binutils=(@(& $objdump --version))[0]; if($LASTEXITCODE){throw 'Binutils version unavailable.'}
        $msys=Split-Path (Split-Path $compilerDirectory -Parent) -Parent
        $pacman=Join-Path $msys 'usr\bin\pacman.exe'
        $packages=@(& $pacman -Q mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-binutils mingw-w64-ucrt-x86_64-crt mingw-w64-ucrt-x86_64-headers)
        if($LASTEXITCODE){throw 'Actual MSYS2 compiler/SDK package provenance is required.'}
    } finally {$env:PATH=$savedPath}
    $build=(Resolve-Path -LiteralPath $BuildDirectory).Path
    $builtRevision=[IO.File]::ReadAllText((Join-Path $build 'build-source-revision.txt')).Trim()
    if($builtRevision -ne $revision){throw 'Binary build source revision differs from checkout.'}
    $builtCompiler=([IO.File]::ReadAllLines((Join-Path $build 'compiler-version.txt')))[0]
    $builtBinutils=([IO.File]::ReadAllLines((Join-Path $build 'binutils-version.txt')))[0]
    $builtPackages=[IO.File]::ReadAllLines((Join-Path $build 'compiler-packages.txt'))
    if($builtCompiler -ne $compiler -or $builtBinutils -ne $binutils -or (($builtPackages -join "`n") -ne ($packages -join "`n"))){throw 'Packaging toolchain differs from recorded build toolchain.'}
    $outputFull=[IO.Path]::GetFullPath($OutputDirectory)
    if($outputFull.TrimEnd('\') -eq $root.TrimEnd('\') -or $outputFull.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Package output must be outside the source checkout.'}
    $null=New-Item -ItemType Directory -Force -Path $OutputDirectory
    $output=(Resolve-Path -LiteralPath $OutputDirectory).Path
    $files=[ordered]@{
        '24510_0rb.exe'=(Join-Path $build '24510_0rb.exe'); 'orb-cdp.exe'=(Join-Path $build 'orb-cdp.exe')
        'payloads/hello.js'=(Join-Path $root 'payloads\hello.js'); 'payloads/banner.js'=(Join-Path $root 'payloads\banner.js')
        'README.md'=(Join-Path $root 'README.md'); 'LICENSE'=(Join-Path $root 'LICENSE')
        'BUILDING.md'=(Join-Path $root 'BUILDING.md'); 'SECURITY.md'=(Join-Path $root 'SECURITY.md')
        'CHANGELOG.md'=(Join-Path $root 'CHANGELOG.md'); 'VERSION'=(Join-Path $root 'VERSION')
        'docs/architecture.md'=(Join-Path $root 'docs\architecture.md'); 'docs/test-results.md'=(Join-Path $root 'docs\test-results.md')
        'docs/images/app.png'=(Join-Path $root 'docs\images\app.png'); 'docs/images/restart.png'=(Join-Path $root 'docs\images\restart.png')
    }
    foreach($path in $files.Values){if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw 'A whitelisted asset is missing.'}}
    $public=@(& git -c core.quotepath=false ls-files --cached --others --exclude-standard); if($LASTEXITCODE){throw 'Cannot inventory source files.'}
    $public=$public | Select-Object -Unique
    $sourceNames=[string[]]$public; [Array]::Sort($sourceNames,[StringComparer]::Ordinal)
    $snapshot=[Text.StringBuilder]::new()
    foreach($name in $sourceNames){
        $path=Join-Path $root $name
        if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw 'Source inventory contains a missing file.'}
        $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        $null=$snapshot.Append($name.Replace('\','/')).Append(' ').Append($hash).Append("`n")
    }
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$snapshotHash=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($snapshot.ToString())))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
    $assetHashes=[ordered]@{}
    $names=[string[]]$files.Keys; [Array]::Sort($names,[StringComparer]::Ordinal)
    foreach($name in $names){$assetHashes[$name]=(Get-FileHash -LiteralPath $files[$name] -Algorithm SHA256).Hash.ToLowerInvariant()}
    $manifest=[ordered]@{
        schema=1; project='24510_0rb'; version=$version; target='windows-x64'
        source_revision=$revision; source_dirty=$dirty; development_package=[bool]$Development
        source_snapshot_sha256=$snapshotHash; compiler=$compiler; compiler_target=$target; binutils=$binutils
        msys2_packages=$packages; powershell=$PSVersionTable.PSVersion.ToString(); dotnet=[Environment]::Version.ToString()
        compile_flags='C11 -Wall -Wextra -Wpedantic -Werror -O2; source-prefix-map; zero-PE-timestamp'
        zip_timestamp='2000-01-01T00:00:00Z'; files=$assetHashes
    }
    $manifestBytes=[Text.UTF8Encoding]::new($false).GetBytes(($manifest|ConvertTo-Json -Depth 8).Replace("`r`n","`n")+"`n")
    $all=[string[]](@($files.Keys)+@('build-manifest.json')); [Array]::Sort($all,[StringComparer]::Ordinal)
    $zipName="24510_0rb-v$version-windows-x64.zip"; $zipPath=Join-Path $output $zipName
    if(Test-Path -LiteralPath $zipPath){throw 'Output ZIP already exists; choose a new output directory.'}
    $stream=[IO.File]::Open($zipPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try{
        $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true,[Text.Encoding]::UTF8)
        try{
            foreach($name in $all){
                $entry=$archive.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime=[DateTimeOffset]::new(2000,1,1,0,0,0,[TimeSpan]::Zero); $entry.ExternalAttributes=0
                $destination=$entry.Open()
                try{if($name -eq 'build-manifest.json'){$destination.Write($manifestBytes,0,$manifestBytes.Length)}else{$inputFile=[IO.File]::OpenRead($files[$name]);try{$inputFile.CopyTo($destination)}finally{$inputFile.Dispose()}}}finally{$destination.Dispose()}
            }
        }finally{$archive.Dispose()}
    }finally{$stream.Dispose()}
    $zipHash=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'),"$zipHash  $zipName`n",[Text.UTF8Encoding]::new($false))
    Write-Output "PACKAGE development=$([bool]$Development) dirty=$dirty version=$version sha256=$zipHash"
}finally{Pop-Location}

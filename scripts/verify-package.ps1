param(
    [Parameter(Mandatory=$true)][string]$ArtifactDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$ExpectedSourceSha,
    [Parameter(Mandatory=$true)][ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$ExpectedVersion
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipName="24510_0rb-v$ExpectedVersion-windows-x64.zip"
$actual=@(Get-ChildItem -LiteralPath $ArtifactDirectory -Force)
if($actual.Count -ne 2 -or @($actual|Where-Object{$_.PSIsContainer}).Count -or
   (Compare-Object @($zipName,'SHA256SUMS.txt') @($actual.Name))){throw 'Artifact must contain exactly the expected ZIP and SHA256SUMS.txt.'}
$zip=Join-Path $ArtifactDirectory $zipName
$hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$sum=[IO.File]::ReadAllText((Join-Path $ArtifactDirectory 'SHA256SUMS.txt'))
if($sum -ne "$hash  $zipName`n"){throw 'ZIP checksum file mismatch or unexpected format.'}
$expected=@('24510_0rb.exe','BUILDING.md','CHANGELOG.md','LICENSE','README.md','SECURITY.md','VERSION','build-manifest.json','docs/architecture.md','docs/images/app.png','docs/images/restart.png','docs/test-results.md','orb-cdp.exe','payloads/banner.js','payloads/hello.js')
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try {
    $names=@($archive.Entries.FullName)
    if($names.Count -ne 15 -or (Compare-Object $expected $names)){throw 'Archive whitelist mismatch.'}
    $sorted=[string[]]$names.Clone(); [Array]::Sort($sorted,[StringComparer]::Ordinal)
    if(($sorted -join '|') -ne ($names -join '|')){throw 'Archive entry order mismatch.'}
    foreach($entry in $archive.Entries){
        if($entry.LastWriteTime.DateTime -ne [DateTime]::new(2000,1,1,0,0,0) -or $entry.ExternalAttributes -ne 0){throw 'Archive metadata mismatch.'}
    }
    function Read-Entry([string]$Name){
        $reader=[IO.StreamReader]::new($archive.GetEntry($Name).Open(),[Text.Encoding]::UTF8)
        try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
    }
    $manifest=(Read-Entry 'build-manifest.json')|ConvertFrom-Json
    if($manifest.schema -ne 1 -or $manifest.project -ne '24510_0rb' -or $manifest.target -ne 'windows-x64' -or
       $manifest.source_revision -cne $ExpectedSourceSha -or $manifest.version -cne $ExpectedVersion -or
       $manifest.source_dirty -isnot [bool] -or $manifest.source_dirty -or
       $manifest.development_package -isnot [bool] -or $manifest.development_package){throw 'Package clean-source provenance mismatch.'}
    if((Read-Entry 'VERSION').Trim() -cne $ExpectedVersion){throw 'Packaged VERSION mismatch.'}
    if($manifest.source_snapshot_sha256 -notmatch '^[0-9a-f]{64}$' -or
       $manifest.compiler_target -cne 'x86_64-w64-mingw32' -or
       -not $manifest.compiler -or -not $manifest.binutils -or @($manifest.msys2_packages).Count -ne 4 -or
       -not $manifest.powershell -or -not $manifest.dotnet){throw 'Missing build/toolchain provenance.'}
    $assets=@($names|Where-Object{$_ -ne 'build-manifest.json'});$keys=@($manifest.files.PSObject.Properties.Name)
    if($keys.Count -ne 14 -or (Compare-Object $assets $keys)){throw 'Manifest asset coverage mismatch.'}
    foreach($name in $assets){
        $stream=$archive.GetEntry($name).Open();$sha=[Security.Cryptography.SHA256]::Create()
        try{$internal=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose();$sha.Dispose()}
        if($internal -cne $manifest.files.$name){throw "Manifest asset checksum mismatch: $name"}
    }
    $links=@([regex]::Matches((Read-Entry 'README.md'),'\]\(([^):]+)\)')|ForEach-Object{$_.Groups[1].Value})
    if(@($links|Where-Object{$_ -notin $names}).Count){throw 'Packaged README has missing relative assets.'}
    Write-Output "VERIFIED version=$ExpectedVersion source=$ExpectedSourceSha assets=14 zip_sha256=$hash"
} finally {$archive.Dispose()}

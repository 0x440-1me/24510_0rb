param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$ExpectedSourceSha,
    [AllowEmptyString()][string]$ReleaseTag='',
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
if($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows' -or -not [Environment]::Is64BitProcess){throw 'ci.ps1 requires the hosted Windows x64 Actions environment.'}
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if($env:ORB_OUT_DIR){throw 'Hosted CI requires default bin output; ORB_OUT_DIR must be unset.'}
    if(-not $env:MSYS2_LOCATION){throw 'MSYS2 setup location is required.'}
    $null=New-Item -ItemType Directory -Force -Path $EvidenceDirectory
    if(@(Get-Process Discord,DiscordPTB,DiscordCanary -ErrorAction SilentlyContinue).Count){throw 'CI fixtures require no running Discord client.'}
    $revision=(& git rev-parse HEAD).Trim();if($LASTEXITCODE -or $revision -cne $ExpectedSourceSha){throw 'Checked out source differs from event SHA.'}
    $status=@(& git status --porcelain=v1)
    $status|Set-Content (Join-Path $EvidenceDirectory 'source-status.txt')
    if($LASTEXITCODE -or $status.Count){Write-Output ($status -join "`n");throw 'CI requires a clean source checkout.'}
    $version=[IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim()
    if($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'){throw 'VERSION must be a supported semantic version.'}
    if($ReleaseTag){
        if($ReleaseTag -notmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or $ReleaseTag -cne "v$version"){throw 'Release tag must exactly match VERSION.'}
        & git fetch --no-tags origin main;if($LASTEXITCODE){throw 'Cannot refresh main ancestry.'}
        & git merge-base --is-ancestor $revision FETCH_HEAD;if($LASTEXITCODE){throw 'Tagged commit is not in current origin/main history.'}
        $tagCommit=(& git rev-parse "$ReleaseTag^{commit}").Trim();if($LASTEXITCODE -or $tagCommit -cne $revision){throw 'Tag does not resolve to the checked out source.'}
    }
    & cmd.exe /d /c scripts\build.bat *> (Join-Path $EvidenceDirectory 'build.log')
    if($LASTEXITCODE){throw 'Native build failed; see build.log.'}
    & .\scripts\verify-ribbon.ps1 *> (Join-Path $EvidenceDirectory 'ribbon.log')
    if($LASTEXITCODE){throw 'Ribbon resource verification failed; see ribbon.log.'}
    & .\bin\process-test.exe *> (Join-Path $EvidenceDirectory 'process.log')
    if($LASTEXITCODE){throw 'Native mocked process tests failed.'}
    & .\bin\payload-test.exe *> (Join-Path $EvidenceDirectory 'payload.log')
    if($LASTEXITCODE){throw 'Payload consent unit tests failed.'}
    & .\bin\orb-state-test.exe *> (Join-Path $EvidenceDirectory 'orb-state.log')
    if($LASTEXITCODE){throw 'V1 state-machine unit tests failed.'}
    & .\tests\v03.ps1 -EvidenceDirectory (Join-Path $EvidenceDirectory 'v03') *> (Join-Path $EvidenceDirectory 'v03.log')
    & .\tests\foundation.ps1 -EvidenceDirectory (Join-Path $EvidenceDirectory 'foundation') *> (Join-Path $EvidenceDirectory 'foundation.log')
    & .\tests\gui-v1-basic.ps1 -EvidenceDirectory (Join-Path $EvidenceDirectory 'gui-v1') *> (Join-Path $EvidenceDirectory 'gui-v1.log')
    if($LASTEXITCODE){throw 'V1 mock GUI suite failed; see gui-v1.log.'}
    $gcc=Join-Path $env:MSYS2_LOCATION 'ucrt64\bin\gcc.exe'
    if($env:ORB_GCC){$gcc=$env:ORB_GCC}
    $objdump=Join-Path (Split-Path $gcc -Parent) 'objdump.exe'
    $imports=@()
    foreach($image in @('24510_0rb.exe','orb-cdp.exe')){
        $dump=@(& $objdump -p (Join-Path $root "bin\$image"));if($LASTEXITCODE){throw 'Cannot inspect production imports.'}
        $dump|Set-Content (Join-Path $EvidenceDirectory ($image+'-imports.txt'))
        $dlls=@($dump|Select-String 'DLL Name:\s*(\S+)'|ForEach-Object{$_.Matches[0].Groups[1].Value})
        if(-not $dlls.Count -or @($dlls|Where-Object{$_ -notmatch '^(?i:bcrypt|COMCTL32|comdlg32|GDI32|IPHLPAPI|KERNEL32|SHELL32|USER32|WS2_32|WINHTTP)\.dll$|^api-ms-win-crt-[a-z0-9-]+\.dll$'}).Count){throw 'Unexpected production runtime dependency.'}
        $imports+=[pscustomobject]@{image=$image;imports=$dlls;passed=$true}
    }
    $imports|ConvertTo-Json -Depth 4|Set-Content (Join-Path $EvidenceDirectory 'imports.json')
    & .\scripts\package.ps1 -BuildDirectory (Join-Path $root 'bin') -OutputDirectory $OutputDirectory *> (Join-Path $EvidenceDirectory 'package-a.log')
    & .\scripts\verify-package.ps1 -ArtifactDirectory $OutputDirectory -ExpectedSourceSha $revision -ExpectedVersion $version *> (Join-Path $EvidenceDirectory 'verify-a.log')
    $repeat=Join-Path $env:RUNNER_TEMP 'orb-package-b'
    & .\scripts\package.ps1 -BuildDirectory (Join-Path $root 'bin') -OutputDirectory $repeat *> (Join-Path $EvidenceDirectory 'package-b.log')
    & .\scripts\verify-package.ps1 -ArtifactDirectory $repeat -ExpectedSourceSha $revision -ExpectedVersion $version *> (Join-Path $EvidenceDirectory 'verify-b.log')
    $zipName="24510_0rb-v$version-windows-x64.zip"
    $first=(Get-FileHash (Join-Path $OutputDirectory $zipName) -Algorithm SHA256).Hash
    $second=(Get-FileHash (Join-Path $repeat $zipName) -Algorithm SHA256).Hash
    if($first -cne $second){throw 'Repeat package SHA differs.'}
    "REPEAT_ZIP sha256=$first"|Set-Content (Join-Path $EvidenceDirectory 'repeatability.txt')
    "source_sha=$revision`nversion=$version"|Add-Content -LiteralPath $env:GITHUB_OUTPUT
} finally {Pop-Location}

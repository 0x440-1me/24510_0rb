param(
    [Parameter(Mandatory=$true)][ValidatePattern('^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$ReleaseTag,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$ExpectedSourceSha,
    [Parameter(Mandatory=$true)][string]$ExpectedVersion,
    [Parameter(Mandatory=$true)][string]$ArtifactDirectory
)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true' -or $env:GITHUB_EVENT_NAME -ne 'push' -or $env:GITHUB_REF_TYPE -ne 'tag'){throw 'Publishing requires an Actions tag push.'}
if($ReleaseTag -cne "v$ExpectedVersion" -or $env:GITHUB_REF_NAME -cne $ReleaseTag){throw 'Release tag/version mismatch.'}
if($env:GITHUB_REPOSITORY -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Invalid repository identity.'}
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $head=(& git rev-parse HEAD).Trim();if($LASTEXITCODE -or $head -cne $ExpectedSourceSha -or $head -cne $env:GITHUB_SHA){throw 'Publisher source SHA mismatch.'}
    $tagCommit=(& git rev-parse "$ReleaseTag^{commit}").Trim();if($LASTEXITCODE -or $tagCommit -cne $head){throw 'Tag source mismatch.'}
    $status=@(& git status --porcelain=v1);if($LASTEXITCODE -or $status.Count){throw 'Publisher source is dirty.'}
    if([IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim() -cne $ExpectedVersion){throw 'Publisher VERSION mismatch.'}
    & git fetch --no-tags origin main;if($LASTEXITCODE){throw 'Cannot refresh main ancestry.'}
    & git merge-base --is-ancestor $head FETCH_HEAD;if($LASTEXITCODE){throw 'Tag is not in current main history.'}
    & .\scripts\verify-package.ps1 -ArtifactDirectory $ArtifactDirectory -ExpectedSourceSha $head -ExpectedVersion $ExpectedVersion
    $existing=@(& gh api "repos/$env:GITHUB_REPOSITORY/releases" --paginate --jq '.[].tag_name')
    if($LASTEXITCODE){throw 'Cannot inspect existing releases.'}
    if($ReleaseTag -in $existing){throw 'Release already exists; overwrite is forbidden.'}
    $notes=Join-Path $env:RUNNER_TEMP 'orb-release-notes.md'
    $controlled=[IO.File]::ReadAllText((Join-Path $root 'docs\release-notes.md'))
    [IO.File]::WriteAllText($notes,"# $ReleaseTag`n`nTagged source: $head`n`n"+$controlled,[Text.UTF8Encoding]::new($false))
    $zip=Join-Path $ArtifactDirectory "24510_0rb-$ReleaseTag-windows-x64.zip"
    $sum=Join-Path $ArtifactDirectory 'SHA256SUMS.txt'
    & gh release create $ReleaseTag $zip $sum --repo $env:GITHUB_REPOSITORY --verify-tag --title "$ReleaseTag Windows x64" --notes-file $notes
    if($LASTEXITCODE){throw 'Release creation/upload failed; no overwrite retry is allowed.'}
} finally {Pop-Location}

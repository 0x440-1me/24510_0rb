param([string]$EvidenceDirectory=(Join-Path $PSScriptRoot '..\bin\evidence-v1-basic'))
$ErrorActionPreference='Stop'

$root=Split-Path $PSScriptRoot -Parent
$harness=Join-Path $root 'bin\gui-harness.exe'
$mock=Join-Path $root 'bin\mock\24510_0rb-ui-mock.exe'

if (-not (Test-Path -LiteralPath $harness -PathType Leaf)) {
    throw "Harness not found: $harness"
}
if (-not (Test-Path -LiteralPath $mock -PathType Leaf)) {
    throw "Mock not found: $mock"
}
if ([IO.Path]::GetFileName($mock) -ne '24510_0rb-ui-mock.exe') {
    throw "Refusing to launch unexpected executable: $mock"
}

New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$EvidenceDirectory=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$payload=Join-Path $root 'payloads\hello.js'
$modes=@('smoke','v1-startup-cancel','settings','minimum','dpi','v1-visual-ready','v1-visual-running','v1-visual-completed')
$results=New-Object System.Collections.Generic.List[object]

foreach ($mode in $modes) {
    $screenshot=Join-Path $EvidenceDirectory ($mode+'.bmp')
    $log=Join-Path $EvidenceDirectory ($mode+'.log')
    $output=@(& $harness $mock $mode '19234' $payload 'unused' $screenshot 2>&1)
    $exitCode=$LASTEXITCODE
    $output | Out-File -LiteralPath $log -Encoding utf8
    $text=$output -join "`n"
    $summary=[regex]::Match($text,'SUMMARY checks=(\d+) failures=(\d+)')
    $checks=0
    $failed=1
    if ($summary.Success) {
        $checks=[int]$summary.Groups[1].Value
        $failed=[int]$summary.Groups[2].Value
    }
    $present=Test-Path -LiteralPath $screenshot -PathType Leaf
    if ($exitCode -ne 0 -or -not $summary.Success -or -not $present) {
        if ($failed -eq 0) { $failed=1 }
    }
    $results.Add([pscustomobject]@{name=$mode; exit=$exitCode; checks=$checks; failures=$failed; screenshot_present=$present})
}

$results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'summary.json') -Encoding utf8
$passed=@($results | Where-Object { $_.exit -eq 0 -and $_.failures -eq 0 -and $_.screenshot_present }).Count
$failed=$modes.Count-$passed
Write-Output "GUI_V1_BASIC total=$($modes.Count) passed=$passed failed=$failed"
if ($failed -gt 0) { exit 1 }
exit 0

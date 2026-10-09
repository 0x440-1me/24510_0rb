param([string]$EvidenceDirectory = (Join-Path $env:TEMP ('orb-ui-' + [guid]::NewGuid().ToString())))
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$null = New-Item -ItemType Directory -Force -Path $EvidenceDirectory
$harness = Join-Path $root 'bin\gui-harness.exe'
$app = Join-Path $root 'bin\24510_0rb-ui-mock.exe'
$fixture = Join-Path $PSScriptRoot 'mock_cdp.ps1'
$hello = Join-Path $root 'payloads\hello.js'
$unicode = Join-Path $EvidenceDirectory ('external ' + [char]0xD83D + [char]0xDE80 + '.js')
[IO.File]::WriteAllBytes($unicode, [IO.File]::ReadAllBytes($hello))
$preferences = Join-Path $env:LOCALAPPDATA '24510_0rb-ui-test\settings.ini'
$original = if (Test-Path -LiteralPath $preferences) { [IO.File]::ReadAllBytes($preferences) } else { $null }
$before = @(Get-Process Discord,DiscordPTB,DiscordCanary -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id | Sort-Object)
$results = [Collections.Generic.List[object]]::new()
function Invoke-UiCase([string]$Name,[string]$Action,[string]$Mode='', [string]$Payload=$hello,[string]$Expected='MOCK_EVAL_OK',[string]$Port='19234',[string]$Capture='') {
    $job = $null
    try {
        if ($Mode) {
            $job = Start-Job -ScriptBlock { param($File,$Kind) & $File -Mode $Kind -Port 19234 } -ArgumentList $fixture,$Mode
            $ready = $false
            for ($i=0; $i -lt 80; $i++) { if ((Receive-Job $job -Keep) -match 'MOCK_READY') { $ready=$true; break }; Start-Sleep -Milliseconds 100 }
            if (-not $ready) { throw 'Fixture did not become ready.' }
        }
        $arguments = @($app,$Action,$Port,$Payload,$Expected)
        if ($Capture) { $arguments += (Join-Path $EvidenceDirectory $Capture) }
        & $harness @arguments *> (Join-Path $EvidenceDirectory ($Name+'.log'))
        $exitCode=$LASTEXITCODE
        $results.Add([pscustomobject]@{name=$Name;passed=($exitCode -eq 0);exit=$exitCode})
        Write-Output "$Name exit=$exitCode"
        if ($job) {
            $fixtureOutput=@(Receive-Job $job -Keep); $fixtureOutput | Set-Content -LiteralPath (Join-Path $EvidenceDirectory ($Name+'-fixture.log')) -Encoding utf8
            if ($Action -eq 'smoke') { $silent = -not ($fixtureOutput -match 'MOCK_HTTP_REQUEST|MOCK_HANDSHAKE'); $results.Add([pscustomobject]@{name='startup-no-fixture-I/O';passed=$silent;exit=$(if($silent){0}else{1})}) }
        }
    } finally { if ($job) { Stop-Job $job; Remove-Job $job } }
}
try {
    Invoke-UiCase 'startup' 'smoke' 'check' -Capture 'startup.bmp'
    Invoke-UiCase 'settings' 'settings' -Capture 'settings.bmp'
    Invoke-UiCase 'minimum' 'minimum' -Capture 'minimum.bmp'
    Invoke-UiCase 'dpi' 'dpi' -Capture 'dpi.bmp'
    Invoke-UiCase 'mock-affirm' 'mock-restart'
    foreach ($stage in 2,4,7) { $env:ORB_UI_MOCK_FAIL_STAGE="$stage"; Invoke-UiCase "mock-failure-$stage" 'mock-error' -Expected "MOCK failure at stage $stage" }
    Remove-Item Env:\ORB_UI_MOCK_FAIL_STAGE
    $env:ORB_UI_MOCK_HOLD_STAGE='1'; Invoke-UiCase 'mock-cancel-before-shutdown' 'mock-stop' -Port '1000' -Expected 'cancelled before shutdown'
    $env:ORB_UI_MOCK_HOLD_STAGE='4'; Invoke-UiCase 'mock-cancel-after-shutdown' 'mock-stop' -Port '1000' -Expected 'may be stopped'
    $env:ORB_UI_MOCK_HOLD_STAGE='7'; Invoke-UiCase 'mock-cancel-after-launch' 'mock-stop' -Port '1000' -Expected 'Discord was launched. Check the process state above.'
    Remove-Item Env:\ORB_UI_MOCK_HOLD_STAGE
    Invoke-UiCase 'mock-connect' 'connect' 'connect' -Expected 'handshake verified'
    Invoke-UiCase 'mock-hello' 'run' 'success'
    Invoke-UiCase 'mock-exception' 'run' 'exception' -Expected 'JavaScript exception'
    Invoke-UiCase 'mock-repeat-1' 'run' 'success'
    Invoke-UiCase 'mock-repeat-2' 'run' 'success'
    Invoke-UiCase 'mock-repeat-3' 'run' 'success'
    Invoke-UiCase 'Unicode-picker' 'load' 'success' -Payload $unicode
    Invoke-UiCase 'Unicode-persistence' 'persist' -Payload $unicode
    $bytes=[IO.File]::ReadAllBytes($preferences); $bom=($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254)
    $results.Add([pscustomobject]@{name='Unicode-INI-BOM';passed=$bom;exit=$(if($bom){0}else{1})})
    [IO.File]::WriteAllText($preferences,"[app]`r`nport=corrupt`r`n",[Text.Encoding]::ASCII)
    Invoke-UiCase 'corrupt-preferences' 'fallback'
    Invoke-UiCase 'invalidate-verification' 'invalidate' 'success'
    Invoke-UiCase 'stalled-Stop' 'stop' 'stall'
    Invoke-UiCase 'stalled-close' 'close' 'stall'
    $after=@(Get-Process Discord,DiscordPTB,DiscordCanary -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id | Sort-Object)
    $same = ($before -join ',') -ceq ($after -join ',')
    $orphans=@(Get-Process orb-cdp,24510_0rb-ui-mock -ErrorAction SilentlyContinue)
    $results.Add([pscustomobject]@{name='Discord-PIDs-unchanged';passed=$same;exit=$(if($same){0}else{1})})
    $results.Add([pscustomobject]@{name='no-owned-GUI-helper-orphans';passed=($orphans.Count -eq 0);exit=$(if($orphans.Count -eq 0){0}else{1})})
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'gui-summary.json') -Encoding utf8
    $failed=@($results | Where-Object {-not $_.passed}).Count
    Write-Output "UI_SUMMARY total=$($results.Count) passed=$($results.Count-$failed) failed=$failed"
    if ($failed) { throw 'Native GUI suite had failures; see private evidence.' }
} finally {
    Remove-Item Env:\ORB_UI_MOCK_FAIL_STAGE -ErrorAction SilentlyContinue
    Remove-Item Env:\ORB_UI_MOCK_HOLD_STAGE -ErrorAction SilentlyContinue
    if ($null -ne $original) { [IO.File]::WriteAllBytes($preferences,$original) }
    elseif (Test-Path -LiteralPath $preferences) { Remove-Item -LiteralPath $preferences }
}

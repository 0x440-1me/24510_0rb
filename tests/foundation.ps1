param([Parameter(Mandatory=$true)][string]$EvidenceDirectory, [int]$Port=19234)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$bin=Join-Path $root 'bin'
$testBin=Join-Path $bin 'mock'
$helper=Join-Path $testBin 'orb-cdp.exe'
$driver=Join-Path $bin 'controller-test.exe'
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$EvidenceDirectory=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$summary=@()
$discordBefore=@(Get-Process Discord -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id | Sort-Object)
$cases=@(
    @{Name='mock-check';Mode='check';Action='check';Expected=0},
    @{Name='mock-list';Mode='list';Action='list';Expected=0},
    @{Name='mock-connect';Mode='connect';Action='connect';Expected=0},
    @{Name='mock-success';Mode='success';Action='eval';Expected=0},
    @{Name='mock-exception';Mode='exception';Action='eval';Expected=5}
)
foreach($scenario in @('remote','wrongport','wss','userinfo','query','fragment','badpath')) {
    $cases+=@{Name="reject-$scenario";Mode='success';Action='eval';Scenario=$scenario;Expected=4}
}
foreach($scenario in @('title','domain','origin-userinfo','http','https-port','comma','redirect','auth')) {
    $cases+=@{Name="reject-$scenario";Mode='success';Action='eval';Scenario=$scenario;Expected=3}
}
foreach($scenario in @('missing-result','empty-result','missing-type','invalid-type','command-error','garbage')) {
    $cases+=@{Name="reject-$scenario";Mode='success';Action='eval';Scenario=$scenario;Expected=4}
}
foreach($scenario in @('whitespace','undefined')) {
    $cases+=@{Name="accept-$scenario";Mode='success';Action='eval';Scenario=$scenario;Expected=0}
}
$cases+=@{Name='managed-output-cap';Mode='success';Action='managed';Scenario='large-output';Expected=0}
$cases+=@{Name='managed-timeout';Mode='stall';Action='managed';Expected=12;Timeout=1000}
$cases+=@{Name='managed-cancel';Mode='stall';Action='managed';Expected=11;Timeout=10000;Cancel=900}
foreach($case in $cases) {
    $scenario=if($case.Scenario){$case.Scenario}else{'normal'}
    $job=Start-Job -ScriptBlock {param($script,$mode,$port,$scenario) & $script -Mode $mode -Port $port -Scenario $scenario} -ArgumentList (Join-Path $PSScriptRoot 'mock_cdp.ps1'),$case.Mode,$Port,$scenario
    $tripwire=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,$Port+1)
    $tripwire.Start()
    $sentinel=$null
    try {
        $ready=$false; $deadline=[DateTime]::UtcNow.AddSeconds(15)
        do {
            $ready=@(Receive-Job $job -Keep -ErrorAction Stop) -match '^MOCK_READY'
            if(!$ready){Start-Sleep -Milliseconds 100}
        } while(!$ready -and [DateTime]::UtcNow -lt $deadline)
        if(!$ready){throw 'Mock did not start'}
        $clientLog=Join-Path $EvidenceDirectory ($case.Name+'-client.log')
        $started=[Diagnostics.Stopwatch]::StartNew()
        if($case.Name -eq 'managed-cancel') {
            $sentinel=Start-Process -FilePath $driver -ArgumentList '--sentinel' -WindowStyle Hidden -PassThru
        }
        $ErrorActionPreference='Continue' # Windows PowerShell treats native stderr as an ErrorRecord.
        switch($case.Action) {
            'check' { & $helper --check --port $Port *> $clientLog }
            'list' { & $helper --list-targets --port $Port *> $clientLog }
            'connect' { & $helper --connect --port $Port *> $clientLog }
            'eval' { & $helper --file (Join-Path $root 'payloads\hello.js') --verbose --port $Port *> $clientLog }
            'managed' {
                $timeout=if($case.Timeout){$case.Timeout}else{10000}
                $argv=@($testBin,[string]$Port,[string]$timeout,'eval',(Join-Path $root 'payloads\hello.js'))
                if($case.Cancel){$argv += [string]$case.Cancel}
                & $driver @argv *> $clientLog
            }
        }
        $code=$LASTEXITCODE; $ErrorActionPreference='Stop'; $started.Stop()
        $unexpectedConnection=$tripwire.Pending()
        if($case.Mode -eq 'stall') { Stop-Job $job } else { $null=Wait-Job $job -Timeout 10 }
        $serverLog=Join-Path $EvidenceDirectory ($case.Name+'-server.log')
        Receive-Job $job *> $serverLog
        $content=Get-Content -LiteralPath $clientLog -Raw
        $noOrphan=$true
        if($content -match 'pid=(\d+)') { $noOrphan=!(Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue) }
        $passed=$code -eq $case.Expected -and !$unexpectedConnection -and $noOrphan
        $unrelatedSurvived=(!$sentinel -or !$sentinel.HasExited)
        $passed=$passed -and $unrelatedSurvived
        if($case.Name -eq 'managed-output-cap'){ $passed=$passed -and $content.Contains('truncated=0') -and !$content.Contains(('X' * 80)) }
        if($scenario -in @('missing-result','empty-result','missing-type','invalid-type','command-error')) {
            $passed=$passed -and !$content.Contains('No matching CDP response')
        }
        if($case.Mode -eq 'stall'){ $passed=$passed -and $started.ElapsedMilliseconds -lt 5000 }
        if($case.Mode -ne 'stall'){ $passed=$passed -and $job.State -eq 'Completed' }
        $summary += [pscustomobject]@{Name=$case.Name;Exit=$code;Expected=$case.Expected;ElapsedMs=$started.ElapsedMilliseconds;JobState=[string]$job.State;NoOrphan=$noOrphan;UnexpectedConnection=$unexpectedConnection;UnrelatedSurvived=$unrelatedSurvived;Passed=$passed}
    } finally {
        $tripwire.Stop()
        if($job.State -eq 'Running'){Stop-Job $job}
        Remove-Job $job
        if($sentinel){$null=$sentinel.WaitForExit(10000);$sentinel.Dispose()}
    }
}
foreach($case in @(
    @{Name='managed-launch-error';Directory=(Join-Path $EvidenceDirectory 'absent');Payload=(Join-Path $root 'payloads\hello.js');Expected=13},
    @{Name='managed-missing-file';Directory=$testBin;Payload=(Join-Path $EvidenceDirectory 'absent.js');Expected=6},
    @{Name='managed-invalid-filename';Directory=$testBin;Payload='invalid|filename.js';Expected=6}
)) {
    & $driver $case.Directory $Port 2000 eval $case.Payload *> (Join-Path $EvidenceDirectory ($case.Name+'-client.log'))
    $code=$LASTEXITCODE
    $summary += [pscustomobject]@{Name=$case.Name;Exit=$code;Expected=$case.Expected;Passed=($code -eq $case.Expected)}
}
$discordAfter=@(Get-Process Discord -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id | Sort-Object)
$sameDiscord=($discordBefore -join ',') -eq ($discordAfter -join ',')
$summary += [pscustomobject]@{Name='discord-processes-untouched';Passed=$sameDiscord;BeforeCount=$discordBefore.Count;AfterCount=$discordAfter.Count}
foreach($directory in @($testBin,(Join-Path $EvidenceDirectory 'absent'))) {
    $name=if($directory -eq $testBin){'managed-handles-normal-exit'}else{'managed-handles-launch-failure'}
    & $driver --handles $directory *> (Join-Path $EvidenceDirectory ($name+'.log'))
    $summary += [pscustomobject]@{Name=$name;Passed=($LASTEXITCODE -eq 0)}
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'summary.json') -Encoding UTF8
$summary | Format-Table -AutoSize
if($summary.Passed -contains $false){throw 'Foundation tests failed; see evidence'}

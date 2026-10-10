param([string]$EvidenceDirectory = (Join-Path $PSScriptRoot '..\bin\evidence-v03'), [int]$Port=19235)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$null=New-Item -ItemType Directory -Force -Path $EvidenceDirectory
$EvidenceDirectory=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$helper=Join-Path $root 'bin\mock\orb-cdp.exe'
$production=Join-Path $root 'bin\orb-cdp.exe'
$payload=Join-Path $EvidenceDirectory 'synthetic.js'
$text="(() => 'SYNTHETIC_DISPATCH_ONLY')()`r`n"
[IO.File]::WriteAllText($payload,$text,[Text.UTF8Encoding]::new($false))
$hash=(Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash.ToLowerInvariant()
$results=[Collections.Generic.List[object]]::new()
function Assert([bool]$Passed,[string]$Name) {
    $results.Add([pscustomobject]@{name=$Name;passed=$Passed}); Write-Output "$Name passed=$Passed"
}
function Fixture([string]$Name,[string]$Mode,[string[]]$Arguments,[int]$Expected,[switch]$Dispatch,[string]$ExpectedText='SYNTHETIC_DISPATCH_ONLY') {
    $job=Start-Job -ScriptBlock {param($Script,$Mode,$Port,$Dispatch,$Text) & $Script -Mode $Mode -Port $Port -Scenario secrets -Dispatch:([bool]$Dispatch) -ExpectedText $Text} -ArgumentList @((Join-Path $PSScriptRoot 'mock_cdp.ps1'),$Mode,$Port,([bool]$Dispatch),$ExpectedText)
    try {
        $ready=$false
        for($i=0;$i -lt 100;$i++){if(@(Receive-Job $job -Keep) -match 'MOCK_READY'){$ready=$true;break};Start-Sleep -Milliseconds 50}
        if(!$ready){throw 'Synthetic endpoint not ready.'}
        $log=Join-Path $EvidenceDirectory "$Name-client.log"
        $ErrorActionPreference='Continue'
        & $helper @Arguments --port $Port --verbose *> $log
        $code=$LASTEXITCODE; $ErrorActionPreference='Stop'
        $null=Wait-Job $job -Timeout 8
        $server=@(Receive-Job $job -Keep)
        $server | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$Name-server.log")
        $output=[IO.File]::ReadAllText($log)
        Assert ($code -eq $Expected -and $job.State -eq 'Completed') "$Name-result"
        Assert (!$output.Contains('SYNTHETIC_SECRET')) "$Name-no-secrets"
        if($Dispatch){Assert (($server -match 'MOCK_REQUEST_OK').Count -eq 1) "$Name-one-dispatch"}
        if($Name -eq 'stop'){Assert ($output.Contains('Stop requested / state unknown.') -and !$output.Contains('stopped')) 'stop-honest-state'}
        if($Mode -eq 'connect'){Assert (!($server -match 'MOCK_REQUEST_OK')) 'connect-does-not-evaluate'}
    } finally { if($job.State -eq 'Running'){Stop-Job $job};Remove-Job $job }
}
Fixture 'dispatch' 'success' @('--file',$payload,'--dispatch','--sha256',$hash) 0 -Dispatch
Fixture 'stop' 'success' @('--stop-quest') 0 -Dispatch -ExpectedText 'window.stopQuestProgram()'
Fixture 'check-private' 'check' @('--check') 0
Fixture 'list-private' 'list' @('--list-targets') 0
Fixture 'exception-private' 'exception' @('--file',$payload) 5
Fixture 'connect-private' 'connect' @('--connect') 0
# Failing consent and production ownership must produce zero network requests.
$tripwire=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,$Port)
$tripwire.Start()
try {
    $ErrorActionPreference='Continue'
    & $helper --file $payload --dispatch --sha256 ('0'*64) --port $Port *> (Join-Path $EvidenceDirectory 'hash-mismatch.log')
    Assert ($LASTEXITCODE -eq 6 -and !$tripwire.Pending()) 'hash-mismatch-no-I-O'
    & $helper --file $payload --dispatch --port $Port *> (Join-Path $EvidenceDirectory 'missing-hash.log')
    Assert ($LASTEXITCODE -eq 2 -and !$tripwire.Pending()) 'missing-hash-no-I-O'
    foreach($action in @('connect','file','stop-quest','check')) {
        $arguments=@("--$action")
        if($action -eq 'file'){$arguments+=@($payload)}
        $arguments+=@('--port',"$Port")
        & $production @arguments *> (Join-Path $EvidenceDirectory "untrusted-$action.log")
        Assert ($LASTEXITCODE -eq 3 -and !$tripwire.Pending()) "production-$action-refuses-foreign-listener"
    }
    $ErrorActionPreference='Stop'
} finally {$tripwire.Stop()}
# Only compare original bytes; never supply the original as an execution test input.
$original=Join-Path $root 'legacy\dc_cmd_script.js'
Assert ((Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ceq 'EF4B9FD9146DDC4DA80B4037158D12E12641D91E6AEE4BABFA9B976EF96205B5') 'preserved-original-SHA256'
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'summary.json')
$failed=@($results | Where-Object {!$_.passed}).Count
Write-Output "V03_SUMMARY total=$($results.Count) passed=$($results.Count-$failed) failed=$failed"
if($failed){throw 'v0.3 synthetic suite failed.'}

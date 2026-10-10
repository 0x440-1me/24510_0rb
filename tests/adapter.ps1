param([string]$EvidenceDirectory=(Join-Path $PSScriptRoot '..\bin\evidence-adapter'))
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$null=New-Item -ItemType Directory -Force -Path $EvidenceDirectory
$EvidenceDirectory=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$adapter=[IO.File]::ReadAllText((Join-Path $root 'src\orb-status.js'))
$tests=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'adapter_tests.js'))
$html=Join-Path $EvidenceDirectory 'adapter.html'
[IO.File]::WriteAllText($html,'<!doctype html><meta charset="utf-8"><body>pending<script>async function runAdapter(scan,route=1,key=""){const __ORB_SCAN__=scan,__ORB_KEY__=key; const location={origin:"https://discord.com",pathname:route===1?"/quest-home":"/other"},document={readyState:route===0?"loading":"complete"}; return ('+$adapter+'); }'+$tests+'</script>',[Text.UTF8Encoding]::new($false))
$edge='C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if(!(Test-Path -LiteralPath $edge)){throw 'Adapter engine tests require Microsoft Edge or equivalent isolated Chromium; Edge is absent.'}
$profile=Join-Path $EvidenceDirectory ('profile-'+[guid]::NewGuid().ToString('N'))
$dom=Join-Path $EvidenceDirectory 'result.html'
$stderr=Join-Path $EvidenceDirectory 'engine.log'
$arguments=@('--headless','--disable-gpu','--disable-background-networking','--disable-component-update','--no-first-run','--no-default-browser-check','--virtual-time-budget=10000','--dump-dom',('--user-data-dir="'+$profile+'"'),([uri]::new($html).AbsoluteUri))
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $edge
$psi.Arguments = ($arguments -join ' ')
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $psi
if (-not $process.Start()) { throw 'Could not start the isolated test browser.' }
$outTask = $process.StandardOutput.ReadToEndAsync()
$errTask = $process.StandardError.ReadToEndAsync()
if (-not $process.WaitForExit(20000)) {
  try { $process.Kill() } catch {}
  throw 'Isolated adapter test browser timed out.'
}
[IO.File]::WriteAllText($dom,$outTask.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($stderr,$errTask.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
$process.Dispose()
$deadline=[DateTime]::UtcNow.AddSeconds(20)
do {
    $inputFile=[IO.File]::Open($dom,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    $reader=[IO.StreamReader]::new($inputFile)
    try {$content=$reader.ReadToEnd()} finally {$reader.Dispose()}
    if($content -match '<body>\{'){break}
    Start-Sleep -Milliseconds 100
} while([DateTime]::UtcNow -lt $deadline)
$match=[regex]::Match($content,'<body>(\{.*?\})</body>','Singleline')
if(!$match.Success){throw 'Adapter browser did not return a test summary.'}
$result=[Net.WebUtility]::HtmlDecode($match.Groups[1].Value)|ConvertFrom-Json
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'summary.json')
Write-Output "ADAPTER_SUMMARY total=$($result.total) passed=$($result.passed) failed=$($result.failed)"
if($result.failed){throw 'Read-only adapter tests failed.'}

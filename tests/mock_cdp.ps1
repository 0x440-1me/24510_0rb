param(
    [ValidateSet('success', 'exception', 'check', 'list', 'connect', 'stall')]
    [string]$Mode = 'success',
    [int]$Port = 9223,
    [string]$ExpectedText = 'JURI INJECTION SUCCESS',
    [ValidateSet('normal','remote','wrongport','wss','userinfo','query','fragment','badpath','title','domain','origin-userinfo','http','https-port','comma','missing-result','empty-result','missing-type','invalid-type','whitespace','undefined','command-error','garbage','redirect','auth','large-output')]
    [string]$Scenario = 'normal'
)

$ErrorActionPreference = 'Stop'
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse('127.0.0.1'), $Port)
$listener.Start()
Write-Output "MOCK_READY mode=$Mode port=$Port"

try {
    $httpCount = 0
    while ($true) {
        while (-not $listener.Pending()) { Start-Sleep -Milliseconds 20 }
        $client = $listener.AcceptTcpClient()
        $stream = $client.GetStream()
        try {
            $headerMemory = [System.IO.MemoryStream]::new()
            $one = [byte[]]::new(1)
            $tail = [System.Collections.Generic.Queue[byte]]::new()
            while ($true) {
                $read = $stream.Read($one, 0, 1)
                if ($read -ne 1) { throw 'Client closed before HTTP headers completed.' }
                $headerMemory.WriteByte($one[0])
                $tail.Enqueue($one[0])
                if ($tail.Count -gt 4) { $null = $tail.Dequeue() }
                if ($tail.Count -eq 4 -and [Text.Encoding]::ASCII.GetString($tail.ToArray()) -eq "`r`n`r`n") { break }
                if ($headerMemory.Length -gt 32768) { throw 'HTTP headers too large.' }
            }
            $headers = [Text.Encoding]::ASCII.GetString($headerMemory.ToArray())
            if ($headers -match '(?im)^(Origin|Authorization|Cookie):') { throw 'Unexpected origin/auth/cookie header' }
            $headerMemory.Dispose()
            $line = ($headers -split "`r`n", 2)[0]
            $path = ($line -split ' ')[1]
            Write-Output "MOCK_HTTP_REQUEST path=$path"

            if ($headers -match '(?im)^Upgrade:\s*websocket\s*$') {
                $keyMatch = [regex]::Match($headers, '(?im)^Sec-WebSocket-Key:\s*([^\r\n]+)')
                if (-not $keyMatch.Success) { throw 'Missing Sec-WebSocket-Key.' }
                $sha = [Security.Cryptography.SHA1]::Create()
                $acceptBytes = $sha.ComputeHash([Text.Encoding]::ASCII.GetBytes($keyMatch.Groups[1].Value.Trim() + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'))
                $sha.Dispose()
                $accept = [Convert]::ToBase64String($acceptBytes)
                $handshake = "HTTP/1.1 101 Switching Protocols`r`nUpgrade: websocket`r`nConnection: Upgrade`r`nSec-WebSocket-Accept: $accept`r`n`r`n"
                $handshakeBytes = [Text.Encoding]::ASCII.GetBytes($handshake)
                $stream.Write($handshakeBytes, 0, $handshakeBytes.Length)
                $stream.Flush()
                Write-Output 'MOCK_HANDSHAKE_OK no-origin-auth-cookie'
                if ($Mode -eq 'connect') { Start-Sleep -Milliseconds 200; break }

                $socket = [System.Net.WebSockets.WebSocket]::CreateFromStream($stream, $true, 'cdp', [TimeSpan]::FromSeconds(30))
                $memory = [System.IO.MemoryStream]::new()
                $buffer = [byte[]]::new(8192)
                do {
                    $received = $socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), [Threading.CancellationToken]::None).GetAwaiter().GetResult()
                    if ($received.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { break }
                    $memory.Write($buffer, 0, $received.Count)
                } while (-not $received.EndOfMessage)
                $requestText = [Text.Encoding]::UTF8.GetString($memory.ToArray())
                $memory.Dispose()
                $request = $requestText | ConvertFrom-Json
                if ($request.method -ne 'Runtime.evaluate' -or $request.id -ne 1 -or
                -not $request.params.expression.Contains($ExpectedText)) {
                    throw "Unexpected CDP request: $requestText"
                }
                Write-Output "MOCK_REQUEST_OK method=$($request.method) id=$($request.id) expectedText=$ExpectedText expressionBytes=$([Text.Encoding]::UTF8.GetByteCount($request.params.expression))"
                if ($Mode -eq 'stall') { Start-Sleep -Seconds 30; break }

                $eventBytes = [Text.Encoding]::UTF8.GetBytes('{"method":"Runtime.consoleAPICalled","params":{"type":"log"}}')
                $null = $socket.SendAsync([ArraySegment[byte]]::new($eventBytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
                if ($Mode -eq 'exception') {
                    $responseText = '{"id":1,"result":{"result":{"type":"object","subtype":"error","description":"Error: mock exception"},"exceptionDetails":{"text":"Uncaught","exception":{"description":"Error: mock exception"}}}}'
                } else {
                    $responseText = '{"id":1,"result":{"result":{"type":"string","value":"MOCK_EVAL_OK"}}}'
                }
                if ($Scenario -eq 'missing-result') { $responseText = '{"id":1}' }
                if ($Scenario -eq 'empty-result') { $responseText = '{"id":1,"result":{}}' }
                if ($Scenario -eq 'missing-type') { $responseText = '{"id":1,"result":{"result":{}}}' }
                if ($Scenario -eq 'invalid-type') { $responseText = '{"id":1,"result":{"result":{"type":"invalid"}}}' }
                if ($Scenario -eq 'undefined') { $responseText = '{"id":1,"result":{"result":{"type":"undefined"}}}' }
                if ($Scenario -eq 'command-error') { $responseText = '{"id":1,"error":{"code":-32601,"message":"Fixture command error"}}' }
                if ($Scenario -eq 'whitespace') { $responseText = " `n`t" + $responseText + "`n " }
                if ($Scenario -eq 'garbage') { $responseText += 'garbage' }
                if ($Scenario -eq 'large-output') { $responseText = '{"id":1,"result":{"result":{"type":"string","value":"' + ('X' * 200000) + '"}}}' }
                $responseBytes = [Text.Encoding]::UTF8.GetBytes($responseText)
                $split = [int][Math]::Floor($responseBytes.Length / 2)
                $null = $socket.SendAsync([ArraySegment[byte]]::new($responseBytes, 0, $split), [System.Net.WebSockets.WebSocketMessageType]::Text, $false, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
                $null = $socket.SendAsync([ArraySegment[byte]]::new($responseBytes, $split, $responseBytes.Length - $split), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
                $socket.Dispose()
                break
            }

            if ($path -eq '/json/version') {
                $body = '{"Browser":"MockCDP/1.0","Protocol-Version":"1.3"}'
            } elseif ($path -eq '/json/list') {
                $body = '[{"description":"","devtoolsFrontendUrl":"","id":"mock-page","title":"Discord","type":"page","url":"https://discord.com/app","webSocketDebuggerUrl":"ws://127.0.0.1:' + $Port + '/devtools/page/mock"}]'
                $body = switch ($Scenario) {
                    'remote' { $body.Replace('127.0.0.1','192.0.2.1') }
                    'wrongport' { $body }
                    'wss' { $body.Replace('ws://','wss://') }
                    'userinfo' { $body.Replace('ws://127','ws://user@127') }
                    'query' { $body.Replace('/page/mock','/page/mock?q=1') }
                    'fragment' { $body.Replace('/page/mock','/page/mock#x') }
                    'badpath' { $body.Replace('/devtools/page/mock','/unexpected/mock') }
                    'title' { $body.Replace('https://discord.com/app','https://example.com/app') }
                    'domain' { $body.Replace('https://discord.com/app','https://discord.com.evil.test/app') }
                    'origin-userinfo' { $body.Replace('https://discord.com/app','https://discord.com@evil.test/app') }
                    'http' { $body.Replace('https://discord.com/app','http://discord.com/app') }
                    'https-port' { $body.Replace('https://discord.com/app','https://discord.com:444/app') }
                    'comma' { $body.Substring(0,$body.Length-1) + ',]' }
                    default { $body }
                }
                if ($Scenario -eq 'wrongport') { $body = $body.Replace(":$Port/",":$($Port+1)/") }
            } else {
                $client.Close()
                continue
            }
            $bodyBytes = [Text.Encoding]::UTF8.GetBytes($body)
            $status = if ($Scenario -eq 'redirect') {'302 Found'} elseif ($Scenario -eq 'auth') {'401 Unauthorized'} else {'200 OK'}
            $extra = if ($Scenario -eq 'redirect') {"Location: http://127.0.0.1:$($Port+1)/unexpected`r`n"} elseif ($Scenario -eq 'auth') {"WWW-Authenticate: Basic realm=`"fixture`"`r`n"} else {"Set-Cookie: fixture=must-not-send`r`n"}
            $responseHead = "HTTP/1.1 $status`r`n${extra}Content-Type: application/json; charset=utf-8`r`nContent-Length: $($bodyBytes.Length)`r`nConnection: close`r`n`r`n"
            $responseHeadBytes = [Text.Encoding]::ASCII.GetBytes($responseHead)
            $stream.Write($responseHeadBytes, 0, $responseHeadBytes.Length)
            $stream.Write($bodyBytes, 0, $bodyBytes.Length)
            $stream.Flush()
            $httpCount++
            if ($Scenario -in @('remote','wrongport','wss','userinfo','query','fragment','badpath','title','domain','origin-userinfo','http','https-port','comma','redirect','auth')) { break }
            if (($Mode -eq 'list' -and $httpCount -ge 1) -or ($Mode -eq 'check' -and $httpCount -ge 2)) { break }
        } finally {
            $client.Close()
        }
        if ($headers -match '(?im)^Upgrade:\s*websocket\s*$') { break }
    }
} finally {
    $listener.Stop()
}

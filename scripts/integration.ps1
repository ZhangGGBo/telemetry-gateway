param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (-not $SkipBuild) { & "$PSScriptRoot/build.ps1" }
$config = Join-Path $root 'build/integration.json'
$db = Join-Path $root ('build/integration-' + [guid]::NewGuid().ToString() + '.db')
@{host='127.0.0.1';tcp_port=19090;http_port=18080;database=$db;timeout_seconds=2;temperature_limit=80;pressure_limit=10} | ConvertTo-Json | Set-Content -Encoding ascii $config
$server = Start-Process -FilePath "$root/build/telemetry_server.exe" -ArgumentList 'build/integration.json' -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/build/integration.out" -RedirectStandardError "$root/build/integration.err"
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function Connect-Device { $c = [Net.Sockets.TcpClient]::new('127.0.0.1',19090); $c.ReceiveTimeout=5000; return $c }
function Send-Line($stream, $line) { $bytes=[Text.Encoding]::UTF8.GetBytes($line);$stream.Write($bytes,0,$bytes.Length) }
try {
    $ready=$false
    for($i=0;$i -lt 40;$i++) { try { $null=Invoke-RestMethod http://127.0.0.1:18080/health;$ready=$true;break } catch {Start-Sleep -Milliseconds 250} }
    Assert $ready 'Server startup failed'
    $client=Connect-Device; $stream=$client.GetStream(); $reader=[IO.StreamReader]::new($stream)
    # One message split across writes, followed by a second message in the same write.
    Send-Line $stream '{"device":"test-1","temperature":'
    Start-Sleep -Milliseconds 50
    Send-Line $stream "85,`"pressure`":3,`"sequence`":0}`n{`"device`":`"test-1`",`"temperature`":30,`"pressure`":2,`"sequence`":1}`n"
    Assert (($reader.ReadLine()|ConvertFrom-Json).ok) 'Fragmented frame failed'
    Assert (($reader.ReadLine()|ConvertFrom-Json).ok) 'Coalesced frame failed'
    Send-Line $stream "{}`n"
    Assert (-not ($reader.ReadLine()|ConvertFrom-Json).ok) 'Malformed telemetry accepted'
    $client.Dispose()
    $history=Invoke-RestMethod 'http://127.0.0.1:18080/api/history?device=test-1&limit=50'
    Assert ($history.Count -eq 2) 'History count'
    Assert ($history[1].alarm) 'Threshold alarm'
    Assert (-not $history[0].alarm) 'Latest normal reading'
    $devices=Invoke-RestMethod http://127.0.0.1:18080/api/devices
    Assert $devices[0].online 'Online state'
    Start-Sleep -Seconds 3
    $devices=Invoke-RestMethod http://127.0.0.1:18080/api/devices
    Assert (-not $devices[0].online) 'Offline timeout'
    $client=Connect-Device; $stream=$client.GetStream();Send-Line $stream (('x'*4097)+"`n");Start-Sleep -Milliseconds 200
    $closed=$false;try {$closed=($stream.ReadByte() -eq -1)} catch {$closed=$true};$client.Dispose();Assert $closed 'Oversized frame not closed'
    $null=Invoke-RestMethod http://127.0.0.1:18080/health
    $clients=@(); $readers=@()
    try {
        for($n=0;$n -lt 8;$n++) {
            $c=Connect-Device; $clients+=,$c; $st=$c.GetStream();$readers+=,[IO.StreamReader]::new($st)
            $message=@{device="concurrent-$n";temperature=35;pressure=3;sequence=0}|ConvertTo-Json -Compress
            Send-Line $st ($message+"`n")
        }
        foreach($rd in $readers){Assert (($rd.ReadLine()|ConvertFrom-Json).ok) 'Concurrent device write'}
    }finally{foreach($c in $clients){$c.Dispose()}}
    & ./build/device_simulator.exe 127.0.0.1 19090 simulator-test 2
    Assert ($LASTEXITCODE -eq 0) 'Simulator failed'
    $history=Invoke-RestMethod 'http://127.0.0.1:18080/api/history?device=simulator-test&limit=50'
    Assert ($history.Count -eq 2) 'Simulator persisted data'
    Write-Host 'Integration passed: framing, validation, history, alarms, offline detection, oversized frames, eight concurrent devices, C++ simulator.'
} finally { if(-not $server.HasExited) {Stop-Process -Id $server.Id}; $server.WaitForExit() }

$client = New-Object System.Net.Sockets.TcpClient
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    $client.Connect("34.30.189.147", 8080)
    $stopwatch.Stop()
    Write-Host "TCP Connect Success!" -ForegroundColor Green
    Write-Host "RTT Latency: $($stopwatch.Elapsed.TotalMilliseconds.ToString('F2')) ms" -ForegroundColor Cyan
} catch {
    $stopwatch.Stop()
    Write-Host "Connection Failed" -ForegroundColor Red
} finally {
    $client.Close()
}
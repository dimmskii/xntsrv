# Checks whether the XNTsrv TCP service is relaying network connections to
# 127.0.0.1:6000 (X), or with -Port 4713 to the audio server.
#
# A stand-in for the server holds the port on every address with address
# reuse, as the stock server does, and tells each client where its connection
# came from.  Connecting to this computer's network address then answers
# "from=127.0.0.1" when the service took the connection and relayed it, and
# "from=<network address>" when it went straight to the stand-in.
#
# Everything runs on one thread: a TCP connection completes before the
# listener accepts it, so the script can connect first and accept afterwards.
param([Parameter(Mandatory)] [ValidateSet('relayed', 'direct')] [string] $Expect, [int] $Port = 6000)

$ip = (Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
    Select-Object -First 1).IPAddress
if (-not $ip) { Write-Output 'no network address on this machine'; exit 1 }

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Any, $Port)
$listener.Server.SetSocketOption([Net.Sockets.SocketOptionLevel]::Socket,
    [Net.Sockets.SocketOptionName]::ReuseAddress, $true)
$listener.Start()

$want = if ($Expect -eq 'relayed') { 'from=127.0.0.1' } else { "from=$ip" }
$reply = ''
# The service looks at addresses and settings every few seconds, so allow for that.
foreach ($attempt in 1..15) {
    $client = $null
    try {
        $client = [Net.Sockets.TcpClient]::new($ip, $Port)
        $client.ReceiveTimeout = 3000

        # Wait for the connection (ours, or the service's on our behalf) to arrive.
        $deadline = (Get-Date).AddSeconds(3)
        while (-not $listener.Pending() -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
        if ($listener.Pending()) {
            $accepted = $listener.AcceptTcpClient()
            $writer = New-Object IO.StreamWriter($accepted.GetStream())
            $writer.WriteLine('from=' + $accepted.Client.RemoteEndPoint.Address)
            $writer.Flush()
            $accepted.Close()
        }
        $reply = (New-Object IO.StreamReader($client.GetStream())).ReadLine()
    } catch {
        $reply = "error: $($_.Exception.Message)"
    } finally {
        if ($client) { $client.Close() }
    }
    if ($reply -eq $want) { break }
    Start-Sleep -Seconds 1
}

$listener.Stop()
if ($reply -eq $want) {
    Write-Output "$Expect ($reply via ${ip}:$Port)"
    exit 0
}
Write-Output "expected $want via $ip, got: $reply"
exit 1

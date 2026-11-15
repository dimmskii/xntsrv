# Checks whether the XNTSrv TCP service is relaying network connections to
# 127.0.0.1:6000.
#
# A stand-in for the X server holds port 6000 on every address with address
# reuse, as the stock server does, and tells each client where its connection
# came from.  Connecting to this computer's network address then answers
# "from=127.0.0.1" when the service took the connection and relayed it, and
# "from=<network address>" when it went straight to the stand-in.
param([Parameter(Mandatory)] [ValidateSet('relayed', 'direct')] [string] $Expect)

$ip = (Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
    Select-Object -First 1).IPAddress
if (-not $ip) { Write-Output 'no network address on this machine'; exit 1 }

$job = Start-Job {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Any, 6000)
    $listener.Server.SetSocketOption([Net.Sockets.SocketOptionLevel]::Socket,
        [Net.Sockets.SocketOptionName]::ReuseAddress, $true)
    $listener.Start()
    while ($true) {
        $client = $listener.AcceptTcpClient()
        $writer = New-Object IO.StreamWriter($client.GetStream())
        $writer.WriteLine('from=' + $client.Client.RemoteEndPoint.Address)
        $writer.Flush()
        $client.Close()
    }
}

$want = if ($Expect -eq 'relayed') { 'from=127.0.0.1' } else { "from=$ip" }
$reply = ''
# The service looks at addresses and settings every few seconds, so allow for that.
foreach ($attempt in 1..20) {
    try {
        $client = [Net.Sockets.TcpClient]::new($ip, 6000)
        $client.ReceiveTimeout = 3000
        $reply = (New-Object IO.StreamReader($client.GetStream())).ReadLine()
        $client.Close()
    } catch {
        $reply = "error: $($_.Exception.Message)"
    }
    if ($reply -eq $want) { break }
    Start-Sleep -Seconds 1
}

Stop-Job $job
Remove-Job $job -Force
if ($reply -eq $want) {
    Write-Output "$Expect ($reply via $ip)"
    exit 0
}
Write-Output "expected $want via $ip, got: $reply"
exit 1

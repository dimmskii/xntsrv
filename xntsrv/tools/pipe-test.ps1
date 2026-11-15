# Reads the one-line reply of the XNTSrv service from its named pipe.
# Exits 1 when the pipe is not there.
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'XNTSrv', [System.IO.Pipes.PipeDirection]::In)
try {
    $pipe.Connect(3000)
} catch {
    Write-Output "no pipe: $($_.Exception.Message)"
    exit 1
}
$reply = (New-Object System.IO.StreamReader($pipe)).ReadLine()
$pipe.Dispose()
Write-Output "reply=[$reply]"
exit 0

#!/bin/sh
# End-to-end test: client -> Unix socket -> xntsrv-bridge -> target echo server.
# Usage: test.sh PATH_TO_BRIDGE [vsock]
set -eu
bridge=$1
kind=${2:-unix}
tmp=$(mktemp -d)
trap 'kill $spid $bpid 2>/dev/null || true; rm -rf "$tmp"' EXIT

if [ "$kind" = vsock ]; then
    target=vsock:1:106000   # CID 1 is loopback; needs the vsock_loopback module
    family="socket.AF_VSOCK"; addr="(1, 106000)"
else
    target=unix:$tmp/target
    family="socket.AF_UNIX"; addr="'$tmp/target'"
fi

# Echo server that upper-cases what it receives until the client half-closes.
python3 -c "
import socket
s = socket.socket($family, socket.SOCK_STREAM)
s.bind($addr); s.listen(4)
while True:
    c, _ = s.accept()
    while (d := c.recv(65536)):
        c.sendall(d.upper())
    c.close()
" &
spid=$!
sleep 0.5

"$bridge" "$tmp/sub/X0=$target" &
bpid=$!
sleep 0.5

out=$(python3 -c "
import socket
c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
c.connect('$tmp/sub/X0')
payload = b'xntsrv' * 50000
c.sendall(payload); c.shutdown(socket.SHUT_WR)
got = b''
while (d := c.recv(65536)):
    got += d
print('ok' if got == payload.upper() else 'mismatch %d/%d' % (len(got), len(payload)))
")
echo "$kind: $out"
[ "$out" = ok ]

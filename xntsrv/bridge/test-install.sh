#!/bin/sh
# System test for xntsrv-install.sh, xntsrv-update.sh and xntsrv-uninstall.sh.
# It installs a real service on the machine it runs on, so it is for CI: it
# needs passwordless sudo and a running systemd.
set -eu

here=$(cd "$(dirname "$0")" && pwd)

check() {
    desc=$1
    shift
    if "$@"; then
        echo "ok    $desc"
    else
        echo "FAIL  $desc" >&2
        exit 1
    fi
}

# Run a script the way the README shows it: piped into a root shell.  The
# settings make it take the other files from this checkout and skip the WSL
# check; they are passed explicitly because sudo does not keep the environment.
piped() { sudo env XNTSRV_BASE_URL="file://$here" XNTSRV_FORCE=1 sh <"$here/$1"; }

installed() {
    check "$1: service active" systemctl is-active --quiet xntsrv-bridge
    check "$1: service enabled" systemctl is-enabled --quiet xntsrv-bridge
    check "$1: binary installed" test -x /usr/local/bin/xntsrv-bridge
    check "$1: socket exists" test -S /tmp/.X11-unix/X0
    check "$1: socket accepts a connection" python3 -c \
        "import socket; socket.socket(socket.AF_UNIX).connect('/tmp/.X11-unix/X0')"
    check "$1: profile sets DISPLAY" sh -c 'unset DISPLAY; . /etc/profile.d/xntsrv.sh; [ "$DISPLAY" = :0 ]'
    check "$1: profile keeps an existing DISPLAY" sh -c 'DISPLAY=h:5; . /etc/profile.d/xntsrv.sh; [ "$DISPLAY" = h:5 ]'
}

piped xntsrv-install.sh
installed install
piped xntsrv-install.sh
installed "install again"
piped xntsrv-update.sh
installed update

# A mapping set in the config file replaces the default one.
echo 'XNTSRV_BRIDGE_ARGS=/tmp/.X11-unix/X7=vsock:2:106000' | sudo tee /etc/default/xntsrv-bridge >/dev/null
sudo systemctl restart xntsrv-bridge
sleep 1
check 'config: service active' systemctl is-active --quiet xntsrv-bridge
check 'config: uses the configured socket' test -S /tmp/.X11-unix/X7
check 'config: default socket not created' sh -c '! [ -e /tmp/.X11-unix/X0 ]'
sudo rm -f /etc/default/xntsrv-bridge
sudo systemctl restart xntsrv-bridge
sleep 1
check 'config removed: back to the default socket' test -S /tmp/.X11-unix/X0

piped xntsrv-uninstall.sh
check 'uninstall: service stopped' sh -c '! systemctl is-active --quiet xntsrv-bridge'
check 'uninstall: files removed' sh -c \
    '! [ -e /usr/local/bin/xntsrv-bridge ] && ! [ -e /etc/systemd/system/xntsrv-bridge.service ] && ! [ -e /etc/profile.d/xntsrv.sh ]'
check 'uninstall: socket removed' sh -c '! [ -e /tmp/.X11-unix/X0 ]'
check 'uninstall again: nothing to do' sh -c "sudo sh <'$here/xntsrv-uninstall.sh' | grep -q 'nothing was installed'"

# Refuses to run on a machine that is not WSL unless told otherwise.
check 'refuses outside WSL' sh -c "! sudo sh <'$here/xntsrv-install.sh' 2>/dev/null"
check 'refuses without root' sh -c "! sh <'$here/xntsrv-install.sh' 2>/dev/null"

echo 'all install script checks passed'

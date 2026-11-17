#!/bin/bash
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
# Extra NAME=VALUE arguments are added to them.
piped() { script=$1; shift; sudo env XNTSRV_BASE_URL="file://$here" XNTSRV_FORCE=1 "$@" bash <"$here/$script"; }

connects() { python3 -c "import socket; socket.socket(socket.AF_UNIX).connect('$1')"; }

installed() {
    check "$1: service active" systemctl is-active --quiet xntsrv-bridge
    check "$1: service enabled" systemctl is-enabled --quiet xntsrv-bridge
    check "$1: binary installed" test -x /usr/local/lib/xntsrv/xntsrv-bridge
    check "$1: socket exists" test -S /tmp/.X11-unix/X0
    check "$1: socket accepts a connection" connects /tmp/.X11-unix/X0
    check "$1: profile sets DISPLAY" sh -c 'unset DISPLAY; . /etc/profile.d/xntsrv.sh; [ "$DISPLAY" = :0 ]'
    check "$1: profile keeps an existing DISPLAY" sh -c 'DISPLAY=h:5; . /etc/profile.d/xntsrv.sh; [ "$DISPLAY" = h:5 ]'
}

audio_installed() {
    check "$1: audio socket accepts a connection" connects /run/xntsrv/pulse/native
    check "$1: profile sets PULSE_SERVER" env -i XDG_RUNTIME_DIR=/nonexistent sh -c \
        '. /etc/profile.d/xntsrv.sh; [ "$PULSE_SERVER" = unix:/run/xntsrv/pulse/native ]'
    check "$1: profile keeps an existing PULSE_SERVER" env -i PULSE_SERVER=tcp:h sh -c \
        '. /etc/profile.d/xntsrv.sh; [ "$PULSE_SERVER" = tcp:h ]'
    check "$1: ALSA plugin installed" sh -c 'ls /usr/lib/*/alsa-lib/libasound_module_pcm_pulse.so >/dev/null'
    check "$1: ALSA programs sent to PulseAudio" grep -q 'type pulse' /etc/alsa/conf.d/99-xntsrv-pulse.conf
    check "$1: PulseAudio client tools installed" command -v pactl
}

piped xntsrv-install.sh
installed install
audio_installed install
piped xntsrv-install.sh
installed "install again"
audio_installed "install again"
piped xntsrv-update.sh
installed update
audio_installed update

# Without audio: X only.
piped xntsrv-install.sh XNTSRV_AUDIO=no
installed "no audio"
check 'no audio: no audio socket' sh -c '! [ -e /run/xntsrv/pulse/native ]'
check 'no audio: profile leaves PULSE_SERVER unset' env -i sh -c '. /etc/profile.d/xntsrv.sh; [ -z "${PULSE_SERVER:-}" ]'
piped xntsrv-install.sh
audio_installed "audio again"

# A mapping set in the config file replaces the default ones.
sudo mkdir -p /etc/xntsrv
echo 'XNTSRV_BRIDGE_ARGS=/tmp/.X11-unix/X7=vsock:2:106000' | sudo tee /etc/xntsrv/bridge.conf >/dev/null
sudo systemctl restart xntsrv-bridge
sleep 1
check 'config: service active' systemctl is-active --quiet xntsrv-bridge
check 'config: uses the configured socket' test -S /tmp/.X11-unix/X7
check 'config: default socket not created' sh -c '! [ -e /tmp/.X11-unix/X0 ]'
sudo rm -rf /etc/xntsrv
sudo systemctl restart xntsrv-bridge
sleep 1
check 'config removed: back to the default socket' test -S /tmp/.X11-unix/X0

piped xntsrv-uninstall.sh
check 'uninstall: service stopped' sh -c '! systemctl is-active --quiet xntsrv-bridge'
check 'uninstall: files removed' sh -c \
    '! [ -e /usr/local/lib/xntsrv ] && ! [ -e /etc/systemd/system/xntsrv-bridge.service ] && ! [ -e /etc/profile.d/xntsrv.sh ] && ! [ -e /etc/alsa/conf.d/99-xntsrv-pulse.conf ]'
check 'uninstall: socket removed' sh -c '! [ -e /tmp/.X11-unix/X0 ]'
check 'uninstall: audio socket removed' sh -c '! [ -e /run/xntsrv/pulse/native ]'
check 'uninstall again: nothing to do' sh -c "sudo bash <'$here/xntsrv-uninstall.sh' | grep -q 'nothing was installed'"

# Refuses to run on a machine that is not WSL unless told otherwise.
check 'refuses outside WSL' sh -c "! sudo bash <'$here/xntsrv-install.sh' 2>/dev/null"
check 'refuses without root' sh -c "! bash <'$here/xntsrv-install.sh' 2>/dev/null"

echo 'all install script checks passed'

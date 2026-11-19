#!/bin/bash
# Removes everything xntsrv-install.sh put on this machine.
#
#   curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-uninstall.sh | sudo bash
#
# The distribution packages it installed for audio (the ALSA plugin for
# PulseAudio, PulseAudio's client tools) are left installed.
#
# SPDX-License-Identifier: MIT
set -eu

LIBDIR=/usr/local/lib/xntsrv
UNIT=/etc/systemd/system/xntsrv-bridge.service
PROFILE=/etc/profile.d/xntsrv.sh
FONTS_CONF=/etc/fonts/conf.d/99-xntsrv-windows-fonts.conf
ALSA_CONF=/etc/alsa/conf.d/99-xntsrv-pulse.conf
SOCKET=/tmp/.X11-unix/X0
LOGIN_PULSE_DIR=/tmp/.xntsrv-pulse

say() { printf 'xntsrv-uninstall: %s\n' "$*"; }
die() { printf 'xntsrv-uninstall: %s\n' "$*" >&2; exit 1; }

main() {
    [ "$(id -u)" = 0 ] || die "this needs root; run it with sudo"

    if [ -d /run/systemd/system ]; then
        systemctl disable --now xntsrv-bridge.service >/dev/null 2>&1 || true
    fi
    # The bridge removes its own sockets when it is stopped.
    pkill -x xntsrv-bridge 2>/dev/null || true

    removed=0
    for file in "$UNIT" "$LIBDIR" "$PROFILE" "$FONTS_CONF" "$ALSA_CONF"; do
        if [ -e "$file" ]; then
            rm -rf "$file"
            removed=$((removed + 1))
        fi
    done
    if [ -d /run/systemd/system ]; then
        systemctl daemon-reload
    fi
    # Left behind only if the bridge was killed rather than stopped.
    if grep -qi microsoft /proc/version 2>/dev/null && [ -S "$SOCKET" ]; then
        rm -f "$SOCKET"
    fi
    rm -rf "$LOGIN_PULSE_DIR"

    if [ "$removed" -eq 0 ]; then
        say "nothing was installed"
    else
        say "removed. DISPLAY, XAUTHORITY and PULSE_SERVER stay set in shells that are already open."
    fi
    if [ -e /etc/xntsrv ]; then
        say "left in place, since you created it: /etc/xntsrv"
    fi
}

main "$@"

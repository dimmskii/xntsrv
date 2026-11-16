#!/bin/sh
# Installs the XNTsrv bridge inside a WSL distribution or a VMware Linux guest.
#
#   curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-install.sh | sudo sh
#
# It installs:
#   /usr/local/bin/xntsrv-bridge              the forwarder
#   /etc/systemd/system/xntsrv-bridge.service a service that keeps it running
#   /etc/profile.d/xntsrv.sh                  DISPLAY and XAUTHORITY for login shells
#
# On a distribution without systemd there is no service; the profile script
# starts the bridge at login instead.  xntsrv-uninstall.sh removes all of it.
#
# Two optional files, which this script never creates or removes:
#   /etc/default/xntsrv-bridge  may set XNTSRV_BRIDGE_ARGS to replace the
#                               bridge's default mapping, for example
#                               XNTSRV_BRIDGE_ARGS=/tmp/.X11-unix/X1=vsock:2:106000
#   /etc/xntsrv/Xauthority      the X authentication cookie, where it cannot be
#                               read straight from Windows (VMware guests)
#
# Environment:
#   XNTSRV_BASE_URL  where to fetch the other files from (default: this repository)
#   XNTSRV_FORCE=1   install even when this is neither WSL 2 nor a VMware guest
#
# SPDX-License-Identifier: MIT
set -eu

BASE_URL=${XNTSRV_BASE_URL:-https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge}
BIN=/usr/local/bin/xntsrv-bridge
UNIT=/etc/systemd/system/xntsrv-bridge.service
PROFILE=/etc/profile.d/xntsrv.sh
# The Windows installer ships a ready-made static binary.
WINDOWS_COPY="/mnt/c/Program Files/XNTsrv/guest/xntsrv-bridge"
SOCKET=/tmp/.X11-unix/X0
CONFIG=/etc/default/xntsrv-bridge
LOCAL_COOKIE=/etc/xntsrv/Xauthority

say() { printf 'xntsrv-install: %s\n' "$*"; }
die() { printf 'xntsrv-install: %s\n' "$*" >&2; exit 1; }

fetch() { # fetch URL DEST
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$2" "$1"
    else
        die "curl or wget is needed to download $1"
    fi
}

# Single-quote a string for use in a generated shell script.
quote() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }

is_wsl() { grep -qi microsoft /proc/version 2>/dev/null; }
is_vmware() { grep -qi vmware /sys/class/dmi/id/sys_vendor 2>/dev/null; }
has_systemd() { [ -d /run/systemd/system ]; }

# Prints the distribution family: debian, fedora, suse, arch, alpine or unknown.
distro_family() {
    id='' like=''
    if [ -r /etc/os-release ]; then
        id=$(. /etc/os-release && printf '%s' "${ID:-}")
        like=$(. /etc/os-release && printf '%s' "${ID_LIKE:-}")
    fi
    for word in $id $like; do
        case $word in
            debian|ubuntu) echo debian; return ;;
            fedora|rhel|centos) echo fedora; return ;;
            suse|opensuse*|sles) echo suse; return ;;
            arch) echo arch; return ;;
            alpine) echo alpine; return ;;
        esac
    done
    echo unknown
}

install_compiler() {
    family=$(distro_family)
    say "installing a C compiler ($family)"
    case $family in
        debian) apt-get update -q && DEBIAN_FRONTEND=noninteractive apt-get install -y -q gcc libc6-dev ;;
        fedora) dnf install -y gcc glibc-devel ;;
        suse) zypper --non-interactive install gcc glibc-devel linux-glibc-devel ;;
        arch) pacman -S --noconfirm --needed gcc ;;
        alpine) apk add gcc musl-dev linux-headers ;;
        *) die "no C compiler found and this distribution is not recognised; install gcc and run this again" ;;
    esac
}

# Put a working bridge binary at $BIN: the copy from the Windows install when
# it runs on this machine, otherwise one built from source.
install_binary() {
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT

    if [ -f "$WINDOWS_COPY" ] && cp "$WINDOWS_COPY" "$tmp/xntsrv-bridge" && chmod 0755 "$tmp/xntsrv-bridge" &&
        "$tmp/xntsrv-bridge" --help >/dev/null 2>&1; then
        say "using the bridge shipped with the Windows install"
    else
        command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || install_compiler
        say "building the bridge from source"
        fetch "$BASE_URL/xntsrv-bridge.c" "$tmp/xntsrv-bridge.c"
        "$(command -v cc || command -v gcc)" -O2 -o "$tmp/xntsrv-bridge" "$tmp/xntsrv-bridge.c"
    fi

    mkdir -p "$(dirname "$BIN")"
    install -m 0755 "$tmp/xntsrv-bridge" "$BIN"
}

# Stop whatever bridge is running, so the new one can take the socket.
stop_existing() {
    if has_systemd; then
        systemctl stop xntsrv-bridge.service 2>/dev/null || true
    fi
    pkill -x xntsrv-bridge 2>/dev/null || true
    # A socket left by a bridge someone ran by hand; the service may not be
    # allowed to replace another user's file.
    if is_wsl && [ -S "$SOCKET" ]; then
        rm -f "$SOCKET"
    fi
}

install_service() {
    cat >"$UNIT" <<EOF
[Unit]
Description=XNTsrv bridge (X11 socket to the XNTsrv X server on Windows)
Documentation=https://github.com/dimmskii/xntsrv

[Service]
# XNTSRV_BRIDGE_ARGS, when set there, replaces the default mapping.
EnvironmentFile=-$CONFIG
ExecStart=$BIN \$XNTSRV_BRIDGE_ARGS
Restart=always
RestartSec=2
# The bridge only needs to create $SOCKET and open sockets.
NoNewPrivileges=yes
CapabilityBoundingSet=
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/tmp
RestrictAddressFamilies=AF_UNIX AF_VSOCK AF_INET AF_INET6

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl enable --now xntsrv-bridge.service >/dev/null 2>&1 || true
    sleep 1
    if ! systemctl is-active --quiet xntsrv-bridge.service; then
        journalctl -u xntsrv-bridge.service -n 20 --no-pager 2>/dev/null || true
        die "the xntsrv-bridge service did not start; its last messages are above. If display :0 belongs to an X server on this machine, give the bridge another socket in $CONFIG"
    fi
    say "service xntsrv-bridge is running"
}

# Where Windows keeps the current user's X authentication cookie, as a Linux
# path.  Prints nothing when it cannot be worked out.
windows_cookie() {
    cmd=/mnt/c/Windows/System32/cmd.exe
    [ -x "$cmd" ] || return 0
    dir=$(cd /mnt/c 2>/dev/null && "$cmd" /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r' | head -n 1) || return 0
    case $dir in
        ?:\\*) ;;
        *) return 0 ;;
    esac
    dir=$(wslpath -u "$dir" 2>/dev/null) || return 0
    printf '%s/XNTsrv/Xauthority' "$dir"
}

install_profile() {
    cookie=$(windows_cookie)
    mkdir -p "$(dirname "$PROFILE")"
    {
        echo '# XNTsrv: send X11 programs to the XNTsrv X server on Windows.'
        echo '# Written by xntsrv-install.sh; removed by xntsrv-uninstall.sh.'
        echo '[ -n "${DISPLAY:-}" ] || export DISPLAY=:0'
        echo 'if [ -z "${XAUTHORITY:-}" ]; then'
        if [ -n "$cookie" ]; then
            echo "    export XAUTHORITY=$(quote "$cookie")"
        elif ! is_wsl; then
            # No Windows drive to read it from: use a copy placed on this machine.
            echo "    [ -r $LOCAL_COOKIE ] && export XAUTHORITY=$LOCAL_COOKIE"
        else
            # Not known at install time: take the first cookie found at login.
            echo '    for _xntsrv in /mnt/c/Users/*/AppData/Local/XNTsrv/Xauthority; do'
            echo '        [ -r "$_xntsrv" ] && export XAUTHORITY="$_xntsrv" && break'
            echo '    done'
            echo '    unset _xntsrv'
        fi
        echo 'fi'
        if ! has_systemd; then
            echo '# No systemd here, so nothing else keeps the bridge running.'
            echo "if ! pgrep -x xntsrv-bridge >/dev/null 2>&1; then"
            echo '    mkdir -p "$HOME/.cache"'
            echo "    (setsid $BIN >>\"\$HOME/.cache/xntsrv-bridge.log\" 2>&1 &)"
            echo 'fi'
        fi
    } >"$PROFILE"
    chmod 0644 "$PROFILE"
    if [ -n "$cookie" ]; then
        say "X authentication cookie: $cookie"
    elif ! is_wsl; then
        say "X authentication: copy %LOCALAPPDATA%\\XNTsrv\\Xauthority from Windows to $LOCAL_COOKIE on this machine"
    else
        say "could not ask Windows where the cookie is; login shells will look for it under /mnt/c/Users"
    fi
}

main() {
    [ "$(id -u)" = 0 ] || die "this needs root; run it with sudo"

    if [ "${XNTSRV_FORCE:-}" = 1 ]; then
        :
    elif is_wsl; then
        [ -e /dev/vsock ] || die "no /dev/vsock here; WSL 1 is not supported, convert the distribution to WSL 2"
        if [ -S /mnt/wslg/.X11-unix/X0 ]; then
            die "WSLg is active and owns display :0. Turn it off: add 'guiApplications=false' under [wsl2] in %USERPROFILE%\\.wslconfig on Windows, run 'wsl --shutdown', and run this again"
        fi
    elif is_vmware; then
        [ -e /dev/vsock ] || die "no /dev/vsock here; install open-vm-tools so that the VMware socket driver is loaded"
    else
        die "this is neither WSL 2 nor a VMware guest; nothing else is supported yet (XNTSRV_FORCE=1 overrides)"
    fi

    stop_existing
    install_binary
    install_profile
    if has_systemd; then
        install_service
        say "manage it with: systemctl status xntsrv-bridge, journalctl -u xntsrv-bridge"
    else
        say "no systemd here: the bridge starts with your next login shell (log: ~/.cache/xntsrv-bridge.log)"
    fi
    if is_vmware && ! is_wsl; then
        say "on Windows, start the XNTsrv VMware service (services.msc) as well as XNTsrv itself"
    fi
    say "done. Start XNTsrv on Windows, open a new terminal, and run a graphical program."
}

main "$@"

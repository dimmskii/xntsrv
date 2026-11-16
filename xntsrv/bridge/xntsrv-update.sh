#!/bin/sh
# Updates the XNTsrv bridge by removing it and installing the current version.
#
#   curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-update.sh | sudo sh
#
# Honours the same environment variables as xntsrv-install.sh.
#
# SPDX-License-Identifier: MIT
set -eu

BASE_URL=${XNTSRV_BASE_URL:-https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge}

die() { printf 'xntsrv-update: %s\n' "$*" >&2; exit 1; }

fetch() { # fetch URL DEST
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$2" "$1"
    else
        die "curl or wget is needed to download $1"
    fi
}

main() {
    [ "$(id -u)" = 0 ] || die "this needs root; run it with sudo"

    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    # Fetch both before changing anything, so a failed download leaves the
    # installed bridge alone.
    fetch "$BASE_URL/xntsrv-uninstall.sh" "$tmp/uninstall.sh"
    fetch "$BASE_URL/xntsrv-install.sh" "$tmp/install.sh"
    sh "$tmp/uninstall.sh"
    sh "$tmp/install.sh"
}

main "$@"

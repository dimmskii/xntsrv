# XNTsrv to-do

Status as of 2026-11-15. See `.github/README.md` for what XNTsrv is and how the pieces fit together.

## Done

- Fork of VcXsrv with an untouched `upstream` mirror branch, synced weekly by CI.
- Launcher (`xntsrv.exe`, `xntsrvw.exe`) with a GNU-style command line.
- Linux-side bridge (`xntsrv-bridge`).
- Services: **XNTsrv WSL** (Hyper-V socket) and **XNTsrv TCP** (network addresses).
- Placeholder service: **XNTsrv Session**.
- Setup tools: `xntsrv-register`, `xntsrv-unregister`, `xntsrv-reset`.
- NSIS installer that packages the server built from this repository (falling back to the upstream VcXsrv release when no build is available).
- End-to-end test on a real Windows 10 machine over the Hyper-V route.

## To do

In intended order.

1. ~~**Build the server from source in CI.**~~ Done: the `server` workflow builds the whole tree from source and packages it with upstream's installer script. It runs when the server's sources or the build recipe change. The installer build takes that package from the latest successful run, and its test starts the built server and queries it with an X client.
2. ~~**Rebrand script.**~~ Done: `xntsrv/build/rebrand.py` renames the executable, vendor strings, titles, About box, log file and internal object names at build time, so the committed tree stays identical to upstream.
3. **Rendering and responsiveness.** Ordinary windows already get modern title bars. Two things to look into: shaped windows such as `xeyes` show the classic Windows title bar that is normally hidden, and the display feels laggier than X410 over TCP (somewhat better with the server built from this repository than with the upstream release). First find out whether the lag comes from the two relays (the bridge and the XNTsrv WSL service) or from the server's drawing.
4. **Loopback-only TCP in the server.** Replaces the firewall block rule with a server that never opens a network port.
5. **Pick up xorg security fixes.** Upstream VcXsrv's last release was 21.1.16.1 (March 2025); check what xorg-server has fixed since.
6. ~~**Linux-side setup.**~~ Done: `xntsrv-install.sh`, `xntsrv-update.sh` and `xntsrv-uninstall.sh` install the bridge as a systemd service and set `DISPLAY` and `XAUTHORITY`. Still to do here: fetch a prebuilt bridge from a release instead of building from source, and package-manager repositories.
7. **Event Log output from the services.** Failures inside them are silent today.
8. **Control Panel applet** (`xntsrv.cpl`) with a classic property sheet, opened from the tray menu's "Properties…" entry.
9. **Group Policy templates** (ADMX).
10. **XNTsrv VMware service.** Written, not yet tested on a real machine: the service listens on the VMware (VMCI) socket, and `xntsrv-install.sh` accepts VMware guests. The bridge needs no VMware mode, since Linux reaches VMCI as the same vsock address it uses under WSL; its mapping can be overridden in `/etc/default/xntsrv-bridge`. Still to do: test on the maintainer's VMware machine (which uses X410 over TCP today), and get the authentication cookie into the guest automatically instead of by copying it to `/etc/xntsrv/Xauthority`.
11. **XNTsrv Session service** and per-user display mapping: start a server for each user at login, and route each user's connections to their own display.
12. **Releases.** Tagged, immutable GitHub releases instead of CI artifacts; code signing once there is a certificate.

## Known gaps

- Nothing starts the X server automatically; it is started from the Start menu (item 11).
- The services always forward to display `:0`, and with several users logged in the WSL service uses the first WSL VM it finds (item 11).
- The WSL service polls for WSL restarts every few seconds instead of being notified.
- The stock server opens its TCP port on every interface; a firewall rule blocks inbound connections to it (item 4).
- The installer is unsigned, so SmartScreen warns (item 12).

## Tests still to run by hand

- Restart WSL (`wsl --shutdown`) with the server left running, and check clients reconnect.
- Switch from the WSL service to the TCP service, including with a VPN kill switch active.

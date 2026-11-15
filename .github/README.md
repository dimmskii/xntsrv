# XNTSrv

A native X server for Windows NT, based on [VcXsrv](https://github.com/marchaesen/vcxsrv).

*Because WSLg is driving me frantic.*

> **Status: early development.** Nothing is released yet. Test installers are built by CI; for now they package the unmodified upstream VcXsrv server.

## What it is

XNTSrv is VcXsrv with a different front end and better plumbing for virtual machines:

- **Native Windows integration**: real title bars, taskbar icons and a quiet tray icon with a plain menu.
- **A conventional command line**: `xntsrv --long-options`, not a mix of dialects.
- **Hypervisor sockets**: WSL2 clients connect over the Hyper-V socket instead of the network, so VPNs and firewalls are not in the way and there is no host IP to look up. VMware (VMCI) support is planned.
- **Closed by default**: the server is reachable from the local machine only, and X authentication is on.
- **An installer** built in the open by GitHub Actions.

## How it fits together

- **`xntsrv-server.exe`** is the X server. It listens on `127.0.0.1` only.
- **`xntsrv.exe`** starts it with a conventional command line (`xntsrv --help`). `xntsrvw.exe` is the same program for shortcuts, with no console window.
- **XNTSrv services** carry connections to the server from outside, one Windows service per route, each manageable in `services.msc`:

  | Service | Starts | Route |
  |---|---|---|
  | XNTSrv WSL | with Windows | The Hyper-V socket of the running WSL virtual machine; follows WSL restarts. |
  | XNTSrv TCP | only when you start it | This computer's network addresses, limited to the local subnet by a firewall rule. To restrict it further, list addresses or adapter names in the `Listen` value (`REG_MULTI_SZ`) under `HKLM\SOFTWARE\XNTSrv\TCP`. |
  | XNTSrv VMware | placeholder | Not implemented yet. |
  | XNTSrv Session | placeholder | Not implemented yet: will start a server for each user at login. |

- **`xntsrv-bridge`** runs inside the Linux guest and forwards `/tmp/.X11-unix/X0` to the host.
- **`xntsrv-register`**, **`xntsrv-unregister`** and **`xntsrv-reset`** set up, remove, and wipe the machine-wide parts. The installer calls the first two.

## Using it with WSL

Install XNTSrv, run `wsl --shutdown` once, and start XNTSrv from the Start menu. Then inside WSL:

```sh
cp "/mnt/c/Program Files/XNTSrv/guest/xntsrv-bridge" ~/ && chmod +x ~/xntsrv-bridge
~/xntsrv-bridge &
export DISPLAY=:0 XAUTHORITY=/mnt/c/Users/<you>/AppData/Local/XNTSrv/Xauthority
xeyes
```

## Layout

| Path | Contents |
|---|---|
| `xntsrv/` | Everything XNTSrv adds. Upstream never touches this directory. |
| `xntsrv/launcher/` | `xntsrv.exe` and `xntsrvw.exe`. |
| `xntsrv/tools/` | The services and the register, unregister and reset tools. |
| `xntsrv/installer/` | The NSIS installer and the script that stages its contents. |
| `xntsrv/bridge/` | `xntsrv-bridge`, the Linux-side forwarder. |
| everything else | The VcXsrv source tree, kept identical to upstream wherever possible. |

## Branches

- `master`: XNTSrv. Upstream plus the `xntsrv/` directory.
- `upstream`: an untouched mirror of VcXsrv `master`, updated automatically.

Changes to upstream files are kept to a minimum so that merging `upstream` into `master` stays painless.

## Licence

XNTSrv additions under `xntsrv/` are MIT unless a file says otherwise. The VcXsrv tree keeps its original licences (GPL-3.0 and the X11/MIT family); see `COPYING`.

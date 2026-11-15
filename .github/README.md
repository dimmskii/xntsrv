# XNTSrv

A native X server for Windows NT, based on [VcXsrv](https://github.com/marchaesen/vcxsrv).

*Because WSLg is driving me frantic.*

> **Status: early development.** Nothing is released yet. For a working X server today, use upstream VcXsrv.

## What it is

XNTSrv is VcXsrv with a different front end and better plumbing for virtual machines:

- **Native Windows integration**: real title bars, taskbar icons and a quiet tray icon with a plain menu.
- **A conventional command line**: `xntsrv --long-options`, not a mix of dialects.
- **Hypervisor sockets**: WSL2 clients connect over the Hyper-V socket instead of TCP, so there are no firewall rules, no host IP lookup and no `-ac`. VMware (VMCI) support is planned.
- **An installer and signed-off releases** built in the open by GitHub Actions.

## Layout

| Path | Contents |
|---|---|
| `xntsrv/` | Everything XNTSrv adds. Upstream never touches this directory. |
| `xntsrv/bridge/` | `xntsrv-bridge`, the small Linux-side forwarder from a Unix socket to the host. |
| everything else | The VcXsrv source tree, kept identical to upstream wherever possible. |

## Branches

- `master`: XNTSrv. Upstream plus the `xntsrv/` directory.
- `upstream`: an untouched mirror of VcXsrv `master`, updated automatically.

Changes to upstream files are kept to a minimum so that merging `upstream` into `master` stays painless.

## Guest bridge

```sh
make -C xntsrv/bridge
./xntsrv/bridge/xntsrv-bridge                 # /tmp/.X11-unix/X0 -> host vsock port 106000
export DISPLAY=:0
```

On the Windows side the server must be listening on the Hyper-V socket (`-vmid`, `-vsockport`; see `-help`).

## Licence

XNTSrv additions under `xntsrv/` are MIT unless a file says otherwise. The VcXsrv tree keeps its original licences (GPL-3.0 and the X11/MIT family); see `COPYING`.

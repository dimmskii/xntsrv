# XNTsrv

*Because WSLg is driving me frantic.*

XNTsrv is a Windows X server, primarily meant as a graphical alternative to Microsoft's own WSLg. It gives you install-and-forget graphics for WSL that are as native as WSL itself: Linux programs reach it over the internal Hyper-V socket, the way Microsoft intended, not over the network. That removes the usual worries about networking, firewalls, and VPN kill switches.

It is based on [VcXsrv](https://github.com/marchaesen/vcxsrv).

> **Status: early development.** There are no releases yet; test installers come from CI. The server in them is built from this repository's VcXsrv source tree, with only its name changed so far. See [TODO.md](../TODO.md) for what is done and what is planned.

## Installation

You need Windows 10 or 11 (64-bit) with WSL 2.

### Step 1: Windows

1. **Download the installer.** Until there is a release, open the [latest successful builds](https://github.com/dimmskii/xntsrv/actions/workflows/windows.yml?query=branch%3Amaster+is%3Asuccess), pick the newest run, and download the `xntsrv-setup` artifact. You have to be signed in to GitHub. Unzip it to get `xntsrv-<version>-setup.exe`.
2. **Run it.** It is not signed yet, so SmartScreen warns: choose "More info", then "Run anyway". It installs to `C:\Program Files\XNTsrv`.
3. **Turn WSLg off**, since XNTsrv takes its place. Add this to `%USERPROFILE%\.wslconfig`:

   ```ini
   [wsl2]
   guiApplications=false
   ```

4. **Restart WSL** so it picks up both changes:

   ```
   wsl --shutdown
   ```

### Step 2: Linux (the WSL bridge)

Linux programs talk to an X server through a Unix socket, `/tmp/.X11-unix/X0`. The bridge is a small program that listens on that socket inside WSL and passes each connection to Windows over the Hyper-V socket. Install it once in every distribution you want graphics in:

```sh
curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-install.sh | sudo sh
```

That installs three things:

| File | Purpose |
|---|---|
| `/usr/local/bin/xntsrv-bridge` | The bridge. Taken from the Windows install, or built from source if that copy is missing. |
| `/etc/systemd/system/xntsrv-bridge.service` | Keeps the bridge running. On a distribution without systemd there is no service, and the bridge starts with your login shell instead. |
| `/etc/profile.d/xntsrv.sh` | Sets `DISPLAY` and `XAUTHORITY` for login shells. |

To remove or update it:

```sh
curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-uninstall.sh | sudo sh
curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-update.sh | sudo sh
```

You can skip this step only if you reach the server over the network instead (see [Routes](#routes)).

## Usage

1. **Start XNTsrv from the Start menu.** An X icon appears in the notification area. This step is manual for now: the part that will start the server for you at login (the XNTsrv Session service) is not finished.
2. **Open a new WSL terminal** and run a graphical program:

   ```sh
   xeyes
   ```

   Each program gets its own Windows window.

Authentication is on: programs must present a cookie that XNTsrv creates in your Windows profile, and the install script points `XAUTHORITY` at it.

### If something does not work

On the Linux side:

```sh
systemctl status xntsrv-bridge      # is the bridge running?
journalctl -u xntsrv-bridge -e      # what did it say?
echo "$DISPLAY $XAUTHORITY"         # are both set? (open a new terminal after installing)
```

On the Windows side, check that **XNTsrv WSL** is running in `services.msc`, and that the server is running (the X icon). The server writes a log to `%TEMP%\XNTsrv.0.log`.

| Symptom | Likely cause |
|---|---|
| `Can't open display` at once | The bridge is not running, or `DISPLAY` is not set in this terminal. |
| `Can't open display`, and the bridge log says `cannot reach vsock:2:106000` | The XNTsrv WSL service is stopped, or WSL was not restarted after installing. |
| `Can't open display`, nothing in the bridge log | The X server is not running; start it from the Start menu. |
| `Authorization required` or `No protocol specified` | `XAUTHORITY` does not point at your cookie. |

### Routes

The X server itself listens only on this computer (`127.0.0.1`). Each way of reaching it from outside is a separate Windows service, which you can start, stop and set to manual or automatic in `services.msc`:

| Service | Starts | Route |
|---|---|---|
| XNTsrv WSL | with Windows | The Hyper-V socket of the running WSL virtual machine. Follows WSL restarts. |
| XNTsrv TCP | only when you start it | This computer's network addresses, limited to the local subnet by a firewall rule. To restrict it further, list addresses or adapter names in the `Listen` value (`REG_MULTI_SZ`) under `HKLM\SOFTWARE\XNTsrv\TCP`. |
| XNTsrv VMware | placeholder | Not implemented yet. |
| XNTsrv Session | placeholder | Not implemented yet. Will start the server for each user at login. |

### Command line

`xntsrv` starts the server from a prompt or script, and works from any folder:

```
xntsrv --help
xntsrv --mode=desktop --dpi=144
xntsrv --dry-run
```

`xntsrvw` is the same program without a console window, for shortcuts.

### Uninstalling

Remove **XNTsrv** in Windows Settings under Apps. That keeps your settings; to delete those as well, for every user, run `xntsrv-reset` from an administrator prompt before uninstalling. On the Linux side, run the uninstall script above.

## For developers

| Path | Contents |
|---|---|
| `xntsrv/` | Everything XNTsrv adds. Upstream never touches this directory. |
| `xntsrv/launcher/` | `xntsrv.exe` and `xntsrvw.exe`. |
| `xntsrv/tools/` | The services and the `xntsrv-register`, `xntsrv-unregister` and `xntsrv-reset` tools. |
| `xntsrv/installer/` | The NSIS installer and the script that stages its contents. |
| `xntsrv/bridge/` | The Linux bridge and its install, update and uninstall scripts. |
| `xntsrv/build/` | The recipe for building the X server from source, and the script that renames it to XNTsrv at build time. |
| everything else | The VcXsrv source tree, kept identical to upstream wherever possible. |

Branches: `master` is XNTsrv; `upstream` is an untouched mirror of VcXsrv `master`, updated automatically. Changes to upstream files are kept to a minimum so that merging stays painless.

The Windows programs build with CMake (`cmake -S xntsrv -B build`), the bridge with `make -C xntsrv/bridge`.

## Licence

XNTsrv's additions under `xntsrv/` are MIT licensed unless a file says otherwise. The VcXsrv tree keeps its original licences (GPL-3.0 and the X11/MIT family); see `COPYING`.

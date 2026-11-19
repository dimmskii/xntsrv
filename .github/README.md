# XNTsrv

*Because WSLg is driving me frantic.*

XNTsrv is a Windows X server, primarily meant as a graphical alternative to Microsoft's own WSLg. It gives you install-and-forget graphics for WSL that are as native as WSL itself: Linux programs reach it over the internal Hyper-V socket, the way Microsoft intended, not over the network. That removes the usual worries about networking, firewalls, and VPN kill switches.

It is based on [VcXsrv](https://github.com/marchaesen/vcxsrv).

> **Status: early development.** There are no releases yet; test installers come from CI. The server in them is built from this repository's VcXsrv source tree, with only its name changed so far. See [TODO.md](../TODO.md) for what is done and what is planned.

## Installation

You need Windows 10 or 11 (64-bit) with WSL 2. Linux virtual machines under VMware are supported too; see [VMware guests](#vmware-guests).

### Step 1: Windows

1. **Download the installer.** Until there is a release, open the [latest successful builds](https://github.com/dimmskii/xntsrv/actions/workflows/windows.yml?query=branch%3Amaster+is%3Asuccess), pick the newest run, and download the `xntsrv-setup` artifact. You have to be signed in to GitHub. Unzip it to get `xntsrv-<version>-setup.exe`.
2. **Run it.** It is not signed yet, so SmartScreen warns: choose "More info", then "Run anyway". It installs to `C:\Program Files\XNTsrv`. On the components page you can leave out **Audio**, the **Files for Linux guests** (the bridge, which the Linux install script otherwise builds from source) and the **Start menu shortcut**. For a silent install, `/S` takes `/NOAUDIO`, `/NOGUEST` and `/NOSHORTCUT`; an upgrade keeps the previous choices unless told otherwise (`/AUDIO`, `/GUEST`, `/SHORTCUT`).
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

Linux programs talk to an X server through a Unix socket, `/tmp/.X11-unix/X0`, and to a sound server through another, here `/run/xntsrv/pulse/native`. The bridge is a small program that listens on those sockets inside WSL and passes each connection to Windows over the Hyper-V socket. Install it once in every distribution you want graphics and sound in:

```sh
curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-install.sh | sudo bash
```

That installs:

| File | Purpose |
|---|---|
| `/usr/local/lib/xntsrv/xntsrv-bridge` | The bridge. Taken from the Windows install, or built from source if that copy is missing. |
| `/etc/systemd/system/xntsrv-bridge.service` | Keeps the bridge running. On a distribution without systemd there is no service, and the bridge starts with your login shell instead. |
| `/etc/profile.d/xntsrv.sh` | Sets `DISPLAY`, `XAUTHORITY`, `PULSE_SERVER`, `PULSE_COOKIE` and the cursor and GTK theme paths for login shells. |
| `/etc/fonts/conf.d/99-xntsrv-windows-fonts.conf` | WSL only: lets X programs use the fonts installed on Windows, read where they are, so they get Segoe UI and Consolas. Nothing is copied. |
| `/etc/alsa/conf.d/99-xntsrv-pulse.conf` | Sends programs that only know ALSA to the sound server. |

For sound it also installs your distribution's ALSA plugin for PulseAudio and PulseAudio's client tools (`pactl`, `paplay`) if they are missing. No sound server runs inside the guest. To leave sound out, run the script with `XNTSRV_AUDIO=no` (`… | sudo XNTSRV_AUDIO=no bash`).

To remove or update it:

```sh
curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-uninstall.sh | sudo bash
curl -fsSL https://raw.githubusercontent.com/dimmskii/xntsrv/master/xntsrv/bridge/xntsrv-update.sh | sudo bash
```

Uninstalling leaves the distribution packages installed for sound.

You can skip this step only if you reach the server over the network instead (see [Routes](#routes)).

#### VMware guests

The same script works in a Linux virtual machine under VMware Workstation or Player, which reaches Windows through VMware's own socket instead of Hyper-V's. Three things differ from WSL:

1. On Windows, start the **XNTsrv VMware** service in `services.msc` (set it to Automatic to keep it).
2. The guest cannot read the authentication cookies from Windows by itself. Copy `%LOCALAPPDATA%\XNTsrv\Xauthority` from Windows to `/etc/xntsrv/Xauthority` in the guest, and `%LOCALAPPDATA%\XNTsrv\pulse-cookie` to `/etc/xntsrv/pulse-cookie`, readable by the users who should have access.
3. If the guest runs its own X server on display `:0`, give the bridge different sockets in `/etc/xntsrv/bridge.conf`, for example `XNTSRV_BRIDGE_ARGS="/tmp/.X11-unix/X1=vsock:2:106000 /run/xntsrv/pulse/native=vsock:2:104713"`, and use `DISPLAY=:1`.
4. A desktop guest usually runs its own sound server (PipeWire or PulseAudio). The profile then leaves `PULSE_SERVER` alone, and programs keep using the guest's own sound devices.

## Usage

1. **Nothing to start.** The XNTsrv Session service starts the X server when you log in to Windows, and right after installation, and starts it again if it ends. It shows no icon in the notification area, since the service decides when it ends: when you log off, or when the service is stopped in `services.msc`, which also closes the Linux programs shown on it. A server started from the Start menu instead shows the X icon, and the service leaves it alone.
2. **Open a new WSL terminal** and run a graphical program:

   ```sh
   xeyes
   ```

   Each program gets its own Windows window.

X programs follow your Windows settings, and aim to look and behave like classic Windows programs:

- **Display and text:** the display scale, the interface font, and font smoothing (off, standard or ClearType, in your screen's colour order). Where the guest lacks the Windows font, fonts of the same width are used instead (Selawik, then Liberation Sans), not the much wider DejaVu Sans; WSL distributions use the fonts installed on Windows.
- **Pointers:** your pointer scheme and size.
- **Mouse and keyboard:** double-click time and distance, drag distance, caret blink rate and when it stops, animations on or off, and whether access keys are always underlined.
- **Behaviour:** scroll bars that stay visible, dialogs with a title bar and a row of buttons, window buttons on the right, and a click in a scroll bar's trough moving by a page.
- **Light or dark** app mode.
- **xterm-style terminals:** the look of the Windows console: its colours, font and size, window size, scrollback and cursor shape.

Change one in Windows and running programs are told; terminal settings apply to terminals opened afterwards. This reaches programs that listen for such settings (GTK 2 and 3, for example) and ones that read X resources; start the server with `xntsrv --no-appearance` to turn it off.

The pointer scheme is converted into an X cursor theme, and GTK 2 and GTK 3 programs get a theme in your classic Windows colours, drawn flat with no pictures, shadows or animations, with Windows' control sizes (23-pixel buttons and text boxes, 75-pixel dialog buttons) and text without kerning, as Windows draws it. Both are generated in your Windows profile. WSL distributions pick them up through the install script's profile file (run the update script once if you installed the bridge before this existed); other guests keep their own cursors and theme for now.

Known limits: in Windows' dark mode the classic colours stay light, so GTK's own dark theme is used there, with its own, larger sizes. Pointers that invert the screen under them in Windows, such as the default text pointer, come out black. GTK 3 programs that are already running keep their colours when you change the Windows colours.

Authentication is on: programs must present a cookie that XNTsrv creates in your Windows profile, and the install script points `XAUTHORITY` at it.

### Sound

The server starts a sound server (PulseAudio) along with it, hidden, which ends when the X server does. Linux programs play through Windows' default output device and record from its default input (the microphone). The sound server listens on this computer only, behind its own cookie (`%LOCALAPPDATA%\XNTsrv\pulse-cookie`), to which the install script points `PULSE_COOKIE`. The XNTsrv WSL and VMware services carry its connections; the TCP service carries them only when you set the `Audio` value (`REG_DWORD`, 1) under `HKLM\SOFTWARE\XNTsrv\TCP`, since the microphone is behind it.

It is left out when you start the server with `xntsrv -A` (`--no-audio`), when the Audio component is not installed, and when something on this computer already answers on its port (4713), such as a PulseAudio server of your own, which is then used instead. Its log is `%LOCALAPPDATA%\XNTsrv\audio\pulseaudio.log`.

Check from WSL with `pactl info`: it should name the server `pulseaudio` and an output called `windows`.

With several users logged in at once, only the first user's sound server can run, as with the X server.

### If something does not work

On the Linux side:

```sh
systemctl status xntsrv-bridge      # is the bridge running?
journalctl -u xntsrv-bridge -e      # what did it say?
echo "$DISPLAY $XAUTHORITY"         # are both set? (open a new terminal after installing)
pactl info                          # does sound reach Windows?
```

On the Windows side, check that **XNTsrv WSL** is running in `services.msc`, and that the server is running (the X icon). The server writes a log to `%TEMP%\XNTsrv.0.log`.

| Symptom | Likely cause |
|---|---|
| `Can't open display` at once | The bridge is not running, or `DISPLAY` is not set in this terminal. |
| `Can't open display`, and the bridge log says `cannot reach vsock:2:106000` | The XNTsrv WSL service is stopped, or WSL was not restarted after installing. |
| `Can't open display`, nothing in the bridge log | The X server is not running; start it from the Start menu, and check that XNTsrv Session is running in `services.msc`. |
| `Authorization required` or `No protocol specified` | `XAUTHORITY` does not point at your cookie. |
| `pactl info` says `Connection failure: Connection refused` | The X server is not running, or was started with `-A`, or the Audio component is not installed. |
| `pactl info` says `Connection failure: Access denied` | `PULSE_COOKIE` does not point at your sound cookie. |

### Routes

The X server and the sound server listen only on this computer (`127.0.0.1`). Each way of reaching it from outside is a separate Windows service, which you can start, stop and set to manual or automatic in `services.msc`:

| Service | Starts | Route |
|---|---|---|
| XNTsrv WSL | with Windows | The Hyper-V socket of the running WSL virtual machine. Follows WSL restarts. |
| XNTsrv TCP | only when you start it | This computer's network addresses, limited to the local subnet by a firewall rule. To restrict it further, list addresses or adapter names in the `Listen` value (`REG_MULTI_SZ`) under `HKLM\SOFTWARE\XNTsrv\TCP`. Carries sound only when the `Audio` value there is 1. |
| XNTsrv VMware | only when you start it | The VMware (VMCI) socket, for Linux virtual machines under VMware Workstation or Player on this computer. Not working yet: on the one real machine tried, the guest could not open the connection. Use XNTsrv TCP from VMware guests for now. |
| XNTsrv Session | with Windows | Not a route: runs the X server for each user while they are logged in, starting it again if it ends. Stopping or restarting it closes those servers and the Linux programs on them. With several users logged in at once, only the first server can run for now. |

### Command line

`xntsrv` starts the server from a prompt or script, and works from any folder:

```
xntsrv --help
xntsrv --mode=desktop --dpi=144
xntsrv --dry-run
```

`xntsrvw` is the same program without a console window, for shortcuts.

### Uninstalling

Remove **XNTsrv** in Windows Settings under Apps. That closes the running server and keeps your settings; to delete those as well, for every user, run `xntsrv-reset` from an administrator prompt before uninstalling. On the Linux side, run the uninstall script above.

## For developers

| Path | Contents |
|---|---|
| `xntsrv/` | Everything XNTsrv adds. Upstream never touches this directory. |
| `xntsrv/launcher/` | `xntsrv.exe` and `xntsrvw.exe`. |
| `xntsrv/appearance/` | `xntsrv-appearance.exe`, which passes Windows appearance settings on to X programs. |
| `xntsrv/tools/` | The services and the `xntsrv-register`, `xntsrv-unregister` and `xntsrv-reset` tools. |
| `xntsrv/installer/` | The NSIS installer and the script that stages its contents. |
| `xntsrv/bridge/` | The Linux bridge and its install, update and uninstall scripts. |
| `xntsrv/bench/` | `xbench.py`, which measures how quickly the X server answers from inside a guest: connections, round trips, popups and image transfer. Python's standard library only. |
| `xntsrv/build/` | The recipe for building the X server from source, and the script that renames it to XNTsrv at build time. |
| everything else | The VcXsrv source tree, kept identical to upstream wherever possible. |

Branches: `master` is XNTsrv; `upstream` is an untouched mirror of VcXsrv `master`, updated automatically. Changes to upstream files are kept to a minimum so that merging stays painless.

The Windows programs build with CMake (`cmake -S xntsrv -B build`), the bridge with `make -C xntsrv/bridge`.

## Licence

XNTsrv's additions under `xntsrv/` are MIT licensed unless a file says otherwise. The VcXsrv tree keeps its original licences (GPL-3.0 and the X11/MIT family); see `COPYING`.

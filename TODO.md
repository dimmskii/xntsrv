# XNTsrv to-do

Status as of 2026-11-17. See `.github/README.md` for what XNTsrv is and how the pieces fit together.

`master` is protected from here on: each piece of work below goes on its own branch and arrives by pull request. The next ones dig into the server itself (items 3, 4, 5 and 13.6), and the performance tests on WSL (item 15).

## Done

- Fork of VcXsrv with an untouched `upstream` mirror branch, synced weekly by CI.
- Launcher (`xntsrv.exe`, `xntsrvw.exe`) with a GNU-style command line.
- Linux-side bridge (`xntsrv-bridge`).
- Services: **XNTsrv WSL** (Hyper-V socket) and **XNTsrv TCP** (network addresses).
- **XNTsrv Session** service: runs each logged-in user's server, starting it at login and again if it ends, and ending it at logoff and when the service stops.
- Setup tools: `xntsrv-register`, `xntsrv-unregister`, `xntsrv-reset`.
- NSIS installer that packages the server built from this repository (falling back to the upstream VcXsrv release when no build is available).
- End-to-end test on a real Windows 10 machine over the Hyper-V route.

## To do

In intended order.

1. ~~**Build the server from source in CI.**~~ Done: the `server` workflow builds the whole tree from source and packages it with upstream's installer script. It runs when the server's sources or the build recipe change. The installer build takes that package from the latest successful run, and its test starts the built server and queries it with an X client.
2. ~~**Rebrand script.**~~ Done: `xntsrv/build/rebrand.py` renames the executable, vendor strings, titles, About box, log file and internal object names at build time, so the committed tree stays identical to upstream.
3. **Rendering and responsiveness.** Ordinary windows already get modern title bars. Open points:
    - Shaped windows such as `xeyes` show the classic Windows title bar that is normally hidden.
    - The display has felt laggier than X410 over TCP, somewhat less so with the server built from this repository. Measurements through both relays show high throughput (an 8 MB screenshot in 0.17 s, 240 frames a second from `glxgears`), which points away from the relays and towards the server's window handling or connection set-up (about 50 ms per new connection).
    - Opening a menu takes about 90 ms, against 17 ms to repaint an existing window and 0.3 ms for a round trip. The figure is the same to within a millisecond under Adwaita and under XNTsrv's flat theme, so it is not the theme's drawing. Measured with `xntsrv/bench/xbench.py` over the TCP route from a VMware guest: a popup's MapNotify arrives in 0.5 ms but its Expose 41 ms later, and only 1.8 ms later when the guest acknowledges at once. The server is quick; the relays sent with Nagle's algorithm on, so a second small message waited for Linux's delayed acknowledgement (40 ms). Branch `relay-nodelay` turns it off in the TCP service and the bridge; still to measure after it, and over the Hyper-V route (WSL).
    - The server's main loop polls: it cannot wait for sockets and Windows messages together, so it calls `select()` with a 1 ms timeout (`winblock.c`, `os/WaitFor.c`), which Windows rounds up to its 15.6 ms timer tick. Mouse and keyboard input can wait that long, and a new connection takes about 15 ms. Worse, `WaitForSomething` turned a zero timeout into 10 ms, so whenever clients still had requests queued after their time slice the server idled for a tick: 100 full-HD fills sent at once took 50 ms instead of 21, so busy clients ran at about half speed. And only one Windows message was handled per pass (a key press is three). Branch `event-loop` waits on sockets and messages at once (`WSAEventSelect` with `MsgWaitForMultipleObjectsEx`), handles every queued message per pass, and drops the 10 ms.
    - Every pointer change (hovering over a link or a text box, say) makes `winSetCursor` build a new Windows pointer from the X image and destroy the old one; nothing is cached. Item 13.6 removes this for the standard pointers.
    - OpenGL through the X server (indirect rendering, `LIBGL_ALWAYS_INDIRECT=1`) reaches the Windows graphics card but is not healthy: `glxinfo` reports `GLXBadCurrentWindow`, `glxgears` prints no frame count, and it only offers OpenGL 1.4. Inherited from upstream. By default programs render in software inside the guest, which works.
4. **Loopback-only TCP in the server.** Replaces the firewall block rule with a server that never opens a network port.
5. **Pick up xorg security fixes.** Upstream VcXsrv's last release was 21.1.16.1 (March 2025); check what xorg-server has fixed since.
6. ~~**Linux-side setup.**~~ Done: `xntsrv-install.sh`, `xntsrv-update.sh` and `xntsrv-uninstall.sh` install the bridge as a systemd service and set `DISPLAY` and `XAUTHORITY`. Still to do here: fetch a prebuilt bridge from a release instead of building from source, and package-manager repositories.
7. **Event Log output from the services.** Failures inside them are silent today.
8. **Control Panel item** (`xntsrv.cpl`): a small window in the style of Windows' own Control Panel items, with roughly four tabs: General, Network, WSL and VMware. Servers started by the Session service have no tray icon, so it is opened from Control Panel and the Start menu, not from a tray menu.
9. **Group Policy templates** (ADMX).
10. **XNTsrv VMware service.** Written: the service listens on the VMware (VMCI) socket, and `xntsrv-install.sh` accepts VMware guests. The bridge needs no VMware mode, since Linux reaches VMCI as the same vsock address it uses under WSL; its mappings can be overridden in `/etc/xntsrv/bridge.conf`. Tried on the maintainer's VMware machine (Ubuntu 24.04 guest): the guest's direct connection fails with `EINVAL`, and the kernel logs `Could not attach to queue pair with -10`, so that guest uses the XNTsrv TCP service over the host-only network instead. Still to do: find out why the VMCI connection is refused, and get the authentication cookies into the guest automatically instead of by copying them to `/etc/xntsrv/Xauthority` and `/etc/xntsrv/pulse-cookie`.
11. **XNTsrv Session service and per-user displays.** The service runs the server for each logged-in user, as that user and not elevated, and owns it (see the branch below). Still to do: a display per user (today every server wants `:0`, so a second logged-in user's server fails to start), and routing each user's connections to their own display.
    - Branch `session-manager`: the Session service owns the servers it starts, one job object per login session, so stopping or restarting the service ends them, and so does logging off. It starts them without a tray icon and restarts them when they exit. Restart limits wait for Event Log output (item 7), which comes next.
    - To consider: Windows 10's per-user services (`SERVICE_USER_OWN_PROCESS`), the ones listed as `Name_1a2b3c` with a hex suffix, one instance per logged-in user, started at logon and stopped at logoff by Windows itself. They might replace the job bookkeeping. Find out first whether such an instance runs in the user's interactive session (the X server needs the desktop), how its instances can be stopped and disabled, and whether third-party services are supported, since Microsoft documents them mostly for administrators to turn off.
12. **Releases.** Tagged, immutable GitHub releases instead of CI artifacts; code signing once there is a certificate.
13. **Follow the Windows appearance.** Make X programs match the user's Windows settings, and follow changes to them. A per-user helper, started with the server, pushes settings to clients over X's own channels (XSETTINGS and the root window's resources), so it works for every kind of guest with nothing installed in it; files are used only where a program has to read them itself. Only the classic settings are read (colours, fonts, metrics, cursors), plus display scale and the light/dark switch; Aero effects are not. Uses Windows' built-in imaging, no bundled image library. In order:
    1. ~~DPI, UI font, cursor size and light/dark preference.~~ Done: `xntsrv-appearance.exe`, started by the launcher. Not yet checked: what each toolkit does with the values, and live changes on a real desktop.
    2. ~~Terminal colours from the Windows console palette, as xterm resources.~~ Done, for xterm-style terminals, along with the console's font and size, window size, scrollback and cursor shape.
    3. ~~The Windows cursor scheme converted to X cursor files (WSL first, read through `/mnt/c`).~~ Done for WSL. Still to do: get the theme to guests that cannot read the Windows profile (VMware), and the real frame rate of animated pointers (a fixed 50 ms per frame is used).
    4. ~~A GTK 2 and GTK 3 colour scheme generated from the classic system colours.~~ Done for WSL: a flat theme with no pictures, gradients, shadows or animations, used in light mode. The GTK 3 theme was checked against `gtk3-widget-factory`; the GTK 2 one has not been seen running. Still to do: Qt and other toolkits, reloading running programs when the Windows colours change, and guests that cannot read the Windows profile.
    5. ~~Sizes and text as classic Windows programs have them.~~ Done: font smoothing and ClearType order, mouse and caret timings, animations and access-key underlining are read from Windows; scroll bars that stay, dialogs with title bars, window buttons on the right and paging scroll-bar troughs are set; the UI font falls back to Selawik and Liberation Sans (DejaVu Sans, the usual fallback, is 15% wider); terminals get the console font's em size rather than its cell height; the GTK 3 theme draws Windows' control sizes (23-pixel buttons, text boxes and lists, 75-pixel dialog buttons, 22-pixel menu items) without kerning, as GDI does; WSL guests use the Windows fonts in place. Measured with GTK 3 on a VMware guest and run by CI on Windows; GTK 2's sizes are unchanged and unseen. The pointer size is published as three quarters of Windows' (X themes count 24 where Windows counts 32) until the next item makes it unnecessary.
    6. **Windows' own pointers in the server.** Cursors reach the server named (libXcursor names them through XFixes, even the old cursor-font ones), so `winLoadCursor` can show the matching shared Windows pointer (`LoadCursor(NULL, IDC_IBEAM)`, ...) and convert only the rest. That follows the scheme, size and animations exactly in every guest with nothing installed, replacing the cursor theme and the three-quarter size. `winSetCursor` must then not destroy shared pointers.
    7. **Paused here (2026-11-17).** 13.5 is on the WSL laptop for testing. Still seen there: pointers that come out black where Windows inverts the screen under them, most likely the default text pointer. The conversion turns inverting pixels into solid black (`PixelsFromTwoBackgrounds`), and X has no inverting colour for ARGB cursors; 13.6 avoids the problem. GTK 2 sizes not yet seen (`gtk-chtheme` from universe).

14. **Sound.** Linux programs play and record through a PulseAudio server on Windows, started with the X server, carried by the same services as X:
    - The services carry channels: X (vsock 106000 to 127.0.0.1:6000) and audio (vsock 104713 to 127.0.0.1:4713). The TCP route carries audio only when `HKLM\SOFTWARE\XNTsrv\TCP\Audio` is 1.
    - The launcher starts `audio\bin\pulseaudio.exe` (pulseaudio-win32 v5, PulseAudio 15.0, pinned by checksum) unless given `-A` / `--no-audio`, the component is missing, or something already listens on 4713. It runs hidden in a kill-on-close job that only the X server holds, behind a cookie in `%LOCALAPPDATA%\XNTsrv\pulse-cookie`, playing and recording through Windows' default devices (`module-waveout`), with a silent output where there is no sound device. The microphone is on.
    - The installer has components: XNTsrv (always), Audio, Files for Linux guests, Start menu shortcut.
    - The Linux install script maps `/run/xntsrv/pulse/native`, sets `PULSE_SERVER` and `PULSE_COOKIE` unless the session has its own sound server or `PULSE_SERVER` is set, and installs the ALSA PulseAudio plugin and client tools without asking (`XNTSRV_AUDIO=no` leaves sound out). The bridge moved to `/usr/local/lib/xntsrv/`, its settings to `/etc/xntsrv/bridge.conf`, and the scripts are run with `bash`; earlier installs are not migrated.
    - Tried on the WSL laptop (2026-11-18): playback works end to end (`sox`'s `play`) after removing the old install by hand. CI checks on Windows that the server starts, answers `pactl info` with the cookie, refuses without it, and ends with the X server.
    - Still to do: the microphone, programs beyond `sox` (Audacity, mpv with `--ao=pulse` and `--ao=alsa`, a browser, `mikmod`), the VMware route, and what happens when the Windows default device changes during playback. **Pinned for the performance tests (item 15):** latency (`parec | pacat` round trip), crackling under load, and the sample rate it settles on. To watch: whether clients behind the relayed Unix socket try shared memory. pulseaudio-win32's last release is from 2021 and is 32-bit with only `module-waveout` (no WASAPI), so a newer build, or one of our own, may be needed. A desktop guest with its own sound server could get a tunnel to Windows (`module-tunnel-sink`) instead of being left alone. With several users logged in, only the first user's sound server can run (item 11).

15. **Performance tests on WSL, for the README.** **Pinned.** Measure picture and sound on the WSL route and put the results in a table near the top of `.github/README.md`, with a screenshot or short video. Do it on the WSL development machine, where the Windows side can be built and tested natively.
    - Picture: `xntsrv/bench/xbench.py` (connections, round trips, popups, image transfer), `glxgears` and a heavier OpenGL program with Mesa's Direct3D 12 driver (`GALLIUM_DRIVER=d3d12`, which already works) against software rendering (`llvmpipe`), window dragging and resizing, scrolling in a browser and a terminal.
    - Sound: the items pinned under 14 (round-trip latency, crackling under load, sample rate), and the sound staying in step with video in mpv.
    - Against WSLg and X410 where they can be run on the same machine, with the versions and hardware written down beside the table.

## Known gaps

- Starting XNTsrv from the Start menu while the Session service's server is running starts a second one, which fails with an error box (item 11). The service itself no longer does: it leaves a running server alone.
- A session whose server the Session service did not start (one started from the Start menu) stays unsupervised: when that server ends, the service does not take over, so the session has no server until the next logon or a restart of the service.
- The Session service starts an ended server again with no limit, only waiting longer (up to a minute) while it keeps failing at once, and says nothing about it outside `--console` until it has Event Log output (item 7).
- The services always forward to display `:0`, and with several users logged in the WSL service uses the first WSL VM it finds (item 11).
- The WSL service polls for WSL restarts every few seconds instead of being notified.
- The stock server opens its TCP port on every interface; a firewall rule blocks inbound connections to it (item 4).
- The installer is unsigned, so SmartScreen warns (item 12).
- VMware guests get neither the converted cursor theme nor the GTK theme, so they keep their own (items 13.3, 13.4 and 13.6).
- In Windows' dark mode GTK's own dark theme is used, so the classic control sizes and text without kerning apply in light mode only (item 13.5).
- Changing the Windows colours rewrites the GTK theme's colour file, but running GTK programs do not reload it, since the theme's name stays the same (item 13.4).
- Pointers that invert the screen under them in Windows come out black (item 13.7).

## Tests still to run by hand

- Restart WSL (`wsl --shutdown`) with the server left running, and check clients reconnect.
- Switch from the WSL service to the TCP service, including with a VPN kill switch active.

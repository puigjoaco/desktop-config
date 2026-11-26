# Sunshine on Joaquin's KDE host

A local Sunshine build based on `v2026.914.233613` runs as `joaquin` in the
**system** unit `sunshine-host.service`. In Plasma it creates a separate virtual
display at the first Moonlight client's native resolution and refresh rate,
captures it through KWin/PipeWire DMA-BUF, and encodes on the RTX 4090 with NVENC.
The currently installed build uses extend mode. The prepared exclusive-mode
update below has not been installed or live-tested. KMS remains the startup
and pre-login fallback, with the same uinput keyboard and mouse. SDDM, boot
configuration, autologin and KWallet were not changed. See
[VERIFICATION.md](VERIFICATION.md) for measured results and remaining tests.

The original official package, configuration and pairing keys are preserved.
One-command rollback (disconnects any active stream):

```sh
./rollback-virtual-build.sh
```

## Native client virtual display build

Source: `../../Sunshine`, branch `feat/kwin-virtual-display`, based on installed
tag `v2026.914.233613` / commit `63d35f702ee9e362e43263742981836ec0710384`.
Task changes are uncommitted. No review loop was run by the implementation worker.

Build with pinned submodules and the documented CMake toolchain:

```sh
cd ../../Sunshine
git submodule update --init --recursive
PATH=/usr/local/cuda/bin:$PATH cmake -S . -B cmake-build-kwin -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF -DBUILD_TESTS=ON \
  -DSUNSHINE_ENABLE_TRAY=OFF -DSUNSHINE_ENABLE_VULKAN=OFF \
  -DCMAKE_INSTALL_PREFIX=/opt/sunshine-puig -DSUNSHINE_ASSETS_DIR=share/sunshine \
  -DSUNSHINE_PUBLISHER_NAME=puigjoaco \
  -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/puigjoaco/Sunshine/issues
~/.local/bin/heavy cmake --build cmake-build-kwin --parallel 12 --target sunshine test_sunshine
cd ../desktop-config/sunshine
./use-virtual-build.sh
```

`SUNSHINE_ASSETS_DIR` is relative to the install prefix in this revision. An
absolute value is incorrectly prefixed by the existing build scripts. The
installer copies matching official package assets into the private prefix;
it does not overwrite package-owned files or need to rebuild the unchanged Web UI.
This host now has CUDA NVCC 13.1.115 and the required development packages;
see VERIFICATION for the package inventory and the PipeWire dependency updates.

The installer writes `/opt/sunshine-puig/bin/sunshine`, applies
`cap_sys_admin,cap_sys_nice+p`, installs its KWin permission desktop file, copies
the existing host config into `~/.config/sunshine/sunshine-virtual.conf`, and adds
only `/etc/systemd/system/sunshine-host.service.d/90-virtual-build.conf`.
It preserves the original `/usr/bin/sunshine` and `sunshine.conf`. Rollback removes
that drop-in and restarts the official host using the unchanged original config.
Re-enable the test build with `./use-virtual-build.sh`.

The installer now selects:

```ini
virtual_display = enabled
virtual_display_scale = 100
virtual_display_mode = exclusive
virtual_display_restore_delay = 5
```

`extend` preserves the current extra-monitor behavior. `exclusive` saves the
physical layout and windows, makes the virtual output primary, moves windows and
Plasma desktop/panels to it, and makes physical outputs replicas of it. Every
physical monitor stays enabled and powered on, showing the same desktop as the
phone, scaled by KWin. Physical outputs stop being independent logical screens.
Native client pixels and refresh are retained; scale 150 or 200 enlarges controls. This is the existing desktop seat and login.
The installer resets the virtual config copy to these defaults. Existing live
config is unchanged until the new build and helper are installed deliberately.

The recovery helper needs `python3-dbus`, `python3-gi`, `qdbus6`, and
`kscreen-doctor`. The installer checks these and copies the source asset
`../../Sunshine/src_assets/linux/assets/kwin-exclusive.py` and the built native
`sunshine-kwin-output` helper into `/opt/sunshine-puig/share/sunshine/`. The
virtual-build systemd drop-in adds its `restore` action as `ExecStopPost`, with a 60-second shutdown allowance.
**Deploy both helpers and the drop-in together with the binary before enabling
exclusive mode.** No change is made to SDDM or the pre-login KMS fallback.

Before the first virtual output is created, an independent guardian writes and
fsyncs `~/.local/state/sunshine/kwin-exclusive.json` (0600): connector modes,
positions, scale, rotation, enabled status, priorities and replication sources,
plus surviving windows' opaque IDs, output names, geometry and fullscreen/
maximized/minimized states, and Plasma desktop/panel IDs and connector assignments.
It records no window titles, widget contents or application data. The native
helper configures replication through KWin output-management v2 (version 13+),
using device UUIDs; `kscreen-doctor` exposes the state but cannot change it. The
native helper sends no enable/disable or DPMS requests. A previously disabled
connected physical output causes exclusive initialization to fall back, because
this policy never disables a physical output, including during restoration.
A temporary KWin script routes existing and new windows to the virtual
desktop. Restore clears temporary replicas, restores original connector layout
and Plasma assignments, then restores surviving windows and removes the virtual
output. If exact output restore fails, it enables connected
original outputs at preferred modes and retains the snapshot for retry.

Disconnect starts a configurable grace period (five seconds by default) with
the virtual output still alive and physical mirrors still enabled. A reconnect
claims that same output and resizes its mode to the new request. This avoids a
KWin hotplug transition, which can reload the remembered physical layout.
Phone rotation therefore needs a client disconnect/reconnect with swapped
physical dimensions; automatic Android reconnection is a separate client change.
Sunshine exit/crash, initialization failure and emergency restore bypass grace.
The independent guardian detects Sunshine's lost lifeline; `ExecStopPost` also
covers crashes that kill the entire service process group. The original
10-second `ping_timeout` still precedes capture teardown on silent network loss.
Only one client at a time is supported. Closed windows cannot be recreated;
custom tiling and hidden unmaximize geometry are limited by KWin's scripting API.

KWin accepted virtual-source mirroring in the isolated test: both original
connectors remained enabled while KScreen reported the virtual output as their
replication source and Plasma exposed one logical desktop. Each physical monitor
keeps its native mode/scale; portrait can produce aspect-ratio bars. The actual
NVIDIA scanout/visual scaling still needs the supervised live check below.

If mirroring is unavailable, the fallback leaves all physical monitors enabled
and independent, temporarily moves their logical positions beyond the stream
viewport, and routes windows and all panels to the virtual display. Physical
monitors then retain their wallpaper desktops, including desktop icons/widgets;
they show no application windows or panels after routing. Logs explicitly report
`policy=mirror` or `policy=move`. Original positions are restored after grace.

### Emergency restore

Run from a local terminal or an existing SSH session as the desktop user. This
also asks an active guardian to restore immediately; it does not restart any
service:

```sh
XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
/usr/bin/python3 /opt/sunshine-puig/share/sunshine/kwin-exclusive.py restore
```

Before deployment, substitute the source asset path above for the `/opt` path.
Do not delete the snapshot while recovery is incomplete. It is deliberately
retained if output, Plasma placement or visible window restoration cannot be
verified. Snapshots from another boot/KWin session, or without valid session
identity, are archived as `kwin-exclusive.stale-<id>.json` without applying them.
Completed output recovery is recorded durably, so later retries cannot overwrite
a newer monitor layout. When hardware is missing, degraded recovery brings
panels/windows onto available physical outputs and keeps the original snapshot. If no
snapshot exists, the command is a no-op, including before login.

### Supervised live validation (not run yet)

1. With the user present and a working SSH/local terminal, review/build the fork,
   run focused and nested checks, then deliberately run `./use-virtual-build.sh`.
   That installer restarts only Sunshine; it must be a separately authorized live
   step. Confirm the deployed helper and `ExecStopPost` exist before connecting.
2. Save read-only `kscreen-doctor -j` evidence outside Git. Open identifiable
   test windows on both physical monitors, including normal, maximized and
   fullscreen examples. Record their geometry and panel positions. Keep the
   emergency command above ready in the SSH terminal.
3. Connect the phone at 3120×1440. Check the panel/desktop and every test window
   appear on the phone, the virtual screen is primary, and both physical outputs
   are enabled and show scaled copies of the phone desktop. Confirm KScreen
   `replicationSource` points to the virtual output for each. Check mouse, typing
   and audio with the user.
4. Disconnect and reconnect within five seconds at 1440×3120 (until the Android
   change exists, request that resolution explicitly). Check there is one retained
   virtual output resized to portrait, with both physical replicas still enabled.
   Repeat portrait→landscape. Check readable, unclipped windows and native stream size.
5. Disconnect for longer than five seconds after capture teardown. Compare modes,
   positions, scale, rotation and priorities against the saved JSON, and verify
   original window geometry and panel positions. Repeat a reconnect after grace
   has expired to verify a fresh snapshot.
6. In a separately agreed test window, exercise normal Sunshine stop and a
   Sunshine crash; verify immediate original-layout/window restore and the service
   recovery hook. Run the emergency command while streaming and verify it brings
   the desk back immediately. If any step misbehaves, run that command first.
   Roll back with `./rollback-virtual-build.sh` after the desk is restored.
7. Separately validate a pre-login/reboot session only when the user authorizes
   it. Before login, exclusive mode must do nothing and the KMS greeter path
   remains available. Reconnect after login to enter the virtual desktop.

Before login, the user's compositor and permission service are unavailable, so
KMS streams physical DP-3 at its existing mode. The daemon keeps the same pairing
identity. After login, reconnect the stream to create the native virtual output;
automatic handoff inside the same running stream is not implemented. A reboot
and SDDM-to-Plasma test remain user validation, as before.

## Install and inspect

This configuration targets Ubuntu 26.04 amd64, `joaquin` / UID 1000, and display
`DP-3`. Review those values before using it on another machine.

```sh
./install.sh
systemctl status sunshine-host.service
journalctl -u sunshine-host.service -b
~/.local/libexec/sunshine/admin.py health
```

The installer verifies the pinned official `.deb` against its published GitHub
SHA-256, installs the package, generates credentials only if absent, installs
the system unit, and masks the packaged user unit to prevent two instances.
The host already had user lingering enabled; this is preserved for boot-time
PipeWire. Sunshine runs with service-only `input`, `video` and `render` groups.
The package supplies `cap_sys_admin,cap_sys_nice+p`, udev rules and uhid module
loading. The binary uses a privileged DRM worker; application commands do not
run as root.

Live configuration and pairing keys are in `~/.config/sunshine/` (private).
The random web admin password is stored **only** in
`~/.config/sunshine-admin/admin.json` (0600). Do not paste it into chat or Git.
The separate `web-credentials.json` contains Sunshine's salted password hash.
The installed helpers live in `~/.local/libexec/sunshine/`.

## Pair the phone and laptop

1. Open Moonlight on Android or the laptop, add `192.168.4.27`, and select the PC
   to obtain a PIN. Install Moonlight only on the clients; this PC is the host.
2. Submit that PIN at `https://localhost:47990` on the host, or use the helper:

   ```sh
   ~/.local/libexec/sunshine/admin.py pending
   ~/.local/libexec/sunshine/admin.py pair
   ```

   The helper reads the admin password privately and prompts for the client PIN.
   For agent operation, deliver the PIN through a private secret file and run
   `admin.py pair --pin-file /absolute/private/file`; delete the PIN file after
   use. With several pending devices, inspect their names/addresses and supply
   `--pairing-id ID`. This release requires that ID in `POST /api/pin`; PIN alone
   is insufficient. The helper validates the host certificate without exposing
   the password in command arguments.
3. Repeat once for the other device: each client gets its own pairing under the
   same host identity. Start **Desktop** and test keyboard, mouse and audio.

Use native/full-screen resolution in Moonlight, **60 FPS** initially, hardware
decoding, and **Use touchscreen as a trackpad**. Start with automatic codec
selection or HEVC; choose AV1 only if the phone has a hardware AV1 decoder.
Try 15–30 Mbps on a good LAN and reduce it for internet links. For minimum
latency select Moonlight's lowest-latency frame pacing / no extra buffered
frames; use balanced pacing if jitter makes it uneven. Leave HDR off initially.
The custom build uses the requested native/full-screen dimensions to create its
virtual host display. On the official fallback build, native resolution alone
does not change the captured physical display's aspect ratio.

For the laptop, use Moonlight's desktop client (Windows, macOS or Linux), its
native/custom display resolution, full-screen mode, hardware decoding and
initially 60 FPS. High-DPI displays can have a different pixel resolution from
their logical desktop size; check the actual requested stream dimensions.
Enable keyboard shortcut capture/full-screen keyboard capture where available.
Start with captured relative mouse for the greeter, then try **Optimize mouse
for remote desktop instead of games** for RDP-style pointer behavior after
checking positioning on the two-monitor desktop.

Desktop shortcuts from the [Moonlight setup guide](https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide#keyboardmousegamepad-input-options):
`Ctrl+Alt+Shift+Z` toggles keyboard/mouse capture, `Ctrl+Alt+Shift+X` toggles
full screen, and `Ctrl+Alt+Shift+Q` disconnects while leaving the host app
running. To execute prep undo, use **Quit Session** from Moonlight's app list.
Windows reserves some secure/system shortcuts; macOS may require input
permissions and uses Option for Alt, with Command/Control mapping worth checking;
Linux Wayland shortcut interception depends on the client's compositor.
These differences affect local input capture, not the host's boot-time service.
On every OS, prefer HEVC/AV1 only when the laptop GPU can decode it in hardware.

## Pre-login test (the user performs the reboot)

One persistent process owns the same TLS keys and pairing state before and
after login. There is no greeter/session server handoff and no second pairing.
KMS works without access to a user's Wayland socket, as separately tested.
PipeWire uses the same UID and its existing lingering user manager. Sunshine
can create virtual audio sinks even if no physical sink is active yet.

After pairing both clients, reboot when convenient, **do not log in locally**,
wait for SDDM and networking, then launch Desktop from either client's Moonlight.
Verify the greeter appears,
relative mouse and keyboard work, and the typed password enters Plasma. Check
whether video recovers through the compositor transition without reconnecting,
and whether audio works once logged in. Record freezes, required reconnects,
and `journalctl -u sunshine-host.service -b`. This transition is not yet proven;
connector/plane reassignment can still require a reconnect. Keep the captured
DP-3 monitor connected and powered for this first test.
Repeat with the other client at a later reboot; both use the same pre-login
capture and input path, without installing anything extra on this host.

## Resolution and privacy

KWin 6.6 supports `addCustomMode`, but the tested 2400×1080@60 timing on DP-3
was rejected by NVIDIA's atomic modeset test. The definition was removed.
The default Desktop app changes no display modes or power settings.

An **optional**, unactivated prep/undo template is
`apps-fit-existing.example.json`. `display-mode.py prepare` reads
`SUNSHINE_CLIENT_WIDTH`, `SUNSHINE_CLIENT_HEIGHT`, `SUNSHINE_CLIENT_FPS`, matches
only an existing DP-3 mode, saves the old mode and monitor positions, and
applies it. At the greeter, or if no matching mode exists, it leaves the display
unchanged. `plan` performs no changes. `undo` restores the snapshot. A supported
2560×1440@60 switch and automatic three-second undo were verified.
There is no hardcoded client size: a phone and laptop supply their own values
on each fresh Desktop launch. The host output name is deliberately host-specific.

The optional template is **not installed as the active apps file**. Sunshine's
prep undo runs on **Quit app/session**, not every disconnect or network loss.
After opting in, use Quit rather than simply disconnecting, and recover a stale
snapshot with `~/.local/libexec/sunshine/display-mode.py undo`. This is not full
RDP dynamic resizing or guaranteed restoration after an abrupt disconnect.
Quit the old Desktop app before switching clients, then launch it fresh so the
new device's environment is used; **resume** does not re-run prep commands.
The same applies when moving from greeter capture to an optional resized user
session. Two clients share this one physical desktop; they cannot each have a
different host resolution simultaneously. The optional prep hooks are intended
for one active client at a time.

For exact phone aspect ratio with pre-login KMS, a display exposing that exact
mode is still needed. A programmable HDMI/DP EDID emulator is the predictable
hardware path; an ordinary fixed 16:9 dummy plug does not solve a wide phone
ratio. This NVIDIA driver also exposes runtime EDID override and force controls
on disconnected DP-4/DP-5, and its source consumes overridden EDIDs. A bounded
test on an unused connector is worth doing in a later maintenance window before
buying hardware. No overrides, connector forcing or boot changes were applied.
Generic reports that NVIDIA always ignores DRM EDID overrides are insufficient
for this version; successful forced scanout is still unverified.

KWin virtual outputs are created by its screencast protocol, not physical DRM
scanout. The custom build above implements session-only KWin/PipeWire capture;
KMS cannot capture that virtual output. The official binary remains the
physical-output fallback and does not have the new virtual-display option.

Physical monitors remain enabled and powered on. Exclusive mode uses scaled
replicas of the virtual desktop; it never disables a connector or turns DPMS off.
KMS still needs an active physical framebuffer for the pre-login fallback.

## Network

Sunshine's own listeners use IPv4; the web UI has
`origin_web_ui_allowed = lan` and authentication. UPnP is disabled. No router or
firewall changes were made. For normal internet streaming, forward to this PC:

- TCP **47984, 47989, 48010**.
- UDP **47998, 47999, 48000**.
- **Never forward TCP 47990**: that is the administration UI.

No blanket port ranges are needed. Reserve the host's LAN address at the router
when convenient. LAN pairing and streaming should be validated before WAN use.

## Undo

Run optional display undo first, if a snapshot exists. Then:

```sh
sudo systemctl disable --now sunshine-host.service
sudo rm /etc/systemd/system/sunshine-host.service
sudo rm -rf /etc/systemd/system/sunshine-host.service.d/90-virtual-build.conf /opt/sunshine-puig
rm -f ~/.local/share/applications/dev.lizardbyte.app.Sunshine.kwin.puig.desktop
sudo systemctl daemon-reload
systemctl --user unmask app-dev.lizardbyte.app.Sunshine.service
sudo apt-get remove sunshine miniupnpc libminiupnpc21
sudo udevadm control --reload-rules
```

Those three packages were newly installed for this task. Removing Sunshine
removes its binary/capabilities, `/usr/lib/udev/rules.d/60-sunshine.rules`,
`/usr/lib/modules-load.d/60-sunshine.conf` and its packaged user unit. Do not
disable lingering: it predates this setup. Runtime `/dev/uinput` permissions
were changed by package udev rules from root:root 0600 to root:input 0660 plus
the active user's ACL. A reboot without the package rules resets that state;
do not revoke runtime permissions while another input application needs them.

Optional deletion of task-created private files also removes pairing: the two
`~/.config/sunshine*` directories, `~/.local/libexec/sunshine/`, and
`~/.local/state/sunshine/` (if created). Nothing was committed or pushed.

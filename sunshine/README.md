# Sunshine on Joaquin's KDE host

A local Sunshine build based on `v2026.914.233613` runs as `joaquin` in the
**system** unit `sunshine-host.service`. In Plasma it creates a separate virtual
display at the first Moonlight client's native resolution and refresh rate,
captures it through KWin/PipeWire DMA-BUF, and encodes on the RTX 4090 with NVENC.
Both physical monitors keep their modes and positions. KMS remains the startup
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

The test config enables `virtual_display = enabled` and
`virtual_display_scale = 100`. A scale of 100 gives native pixel density; set
150 or 200 in `sunshine-virtual.conf` for larger controls without changing the
client's pixel dimensions, then restart only `sunshine-host.service`.
The install script resets this copy to the original config plus these defaults.

The service retains `XDG_RUNTIME_DIR=/run/user/1000`, `WAYLAND_DISPLAY=wayland-0`
and the existing user D-Bus/PipeWire addresses. Every real stream tries the
user's current Wayland socket afresh. A `zkde_screencast_unstable_v1` connection
owns a `Virtual-Sunshine-<pid>-<sequence>` output; KScreen configures only that
output's scale and mode. Startup encoder probes never create virtual outputs.
KWin creates outputs at 60 Hz initially, and virtual-only custom modes allow
other refresh rates. Teardown closes the screencast connection; KWin removes
the output and moves its windows back to enabled physical outputs.

A fresh launch or resume after disconnect creates a new output using that
connection's requested mode. The existing network timeout (10 seconds) handles
silent network loss; an explicitly closed control connection can clean up sooner.
A disconnected app may still appear resumable in Moonlight even though its
virtual monitor was removed. Simultaneous clients share one capture owner and
the first client's mode; disconnect the previous client before switching devices.

This is an additional monitor in the existing Plasma session. The cursor is
moved to it at stream start, but mouse and keyboard still share the local seat.
Click its desktop/window before typing; existing windows may retain keyboard
focus until then. There is no persistent rule forcing new application windows
onto the virtual output: use KDE's Move to Screen action when an application's
saved placement chooses a physical monitor. A new virtual output has wallpaper
and no taskbar by default; Plasma's main panel remains on the physical monitor.
Absolute mouse uses the virtual output's logical rectangle; native touch/pen
remain disabled in this host config and were not tested.

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

Do not disable or DPMS-off the currently captured physical output for privacy:
KMS needs its active framebuffer. Monitors currently remain on. With a working
dummy/forced DRM output, physical-output disable/restore can be evaluated while
capturing that independent output. Stretch-to-fill is an immediate client-side
alternative if available, but distorts the desktop and is not native resizing.

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

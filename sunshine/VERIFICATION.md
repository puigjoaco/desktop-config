# Host verification — 2026-11-25

Current state: the custom virtual-display build is active. The first sections
record the earlier official-package baseline; see **Native virtual-display build**
below for the implementation and phone test completed later today.

- Ubuntu 26.04.1, KDE 6.6.7 Wayland, NVIDIA 595.99.02 / RTX 4090.
- `nvidia_drm` modeset parameter: `Y`.
- Original and restored outputs: HDMI-A-5 2560×1080@60 at (0,183),
  DP-3 2560×1440@144 at (2560,0), both enabled, scale 1, normal rotation.
- No logout, reboot, VT switch, display-manager/compositor restart, boot change,
  SDDM configuration change, firewall change or router change was performed.

## Official package

LizardByte/Sunshine's GitHub latest-release endpoint redirected to
[SunshineStream/Sunshine v2026.914.233613](https://github.com/SunshineStream/Sunshine/releases/tag/v2026.914.233613).
The package identifies its publisher as LizardByte, source commit
`63d35f702ee9e362e43263742981836ec0710384`.

Artifact: `sunshine_2026.914.233613-1+ubuntu26.04_amd64.deb`.
GitHub asset digest and locally calculated SHA-256 both equal:

```
b8a8ceeeb2b3f30a18a994ad268c94e0c91170f33a864c7aad31eed927ad7d47
```

Verification used GitHub HTTPS metadata, not an independent detached signature;
none was present among this release's assets. Newly installed packages:
`sunshine` 2026.914.233613-1+ubuntu26.04, `miniupnpc` and `libminiupnpc21`
2.3.3-2build1. No existing packages were upgraded or removed.

## Capture, input, audio and API

The enabled system unit runs under UID 1000 with service supplementary groups
`input video render`. Final daemon PID at verification: **583998**.
Its startup log contains:

```
Screencasting with KMS
Mapped 'DP-3' to kmsgrab monitor index 1
/dev/dri/card1 -> nvidia-drm
Found monitor for DRM screencasting
Found connector ID [132]
Found H.264 encoder: h264_nvenc [nvenc]
Found HEVC encoder: hevc_nvenc [nvenc]
Found AV1 encoder: av1_nvenc [nvenc]
Configuration UI available at [https://localhost:47990]
```

The initial setup process, PID **583356**, was deliberately restarted after
application-file validation; it is stopped. No inherited process was stopped.

AV1 YUV444 capability probes fail, as expected on this card; ordinary AV1
4:2:0 passes. The startup checks initialize the KMS display and encode test
images. They are not a phone-to-host streaming test or proof of uninterrupted
greeter-to-session capture.

A separate **four-second** system-service probe, PID **588712**, used an empty
private runtime directory, a nonexistent Wayland socket and loopback-only ports
58084/58089/58090/58110. Its log first reports failure to connect to Wayland,
then successfully initializes KMS and all three NVENC codecs. This demonstrates
that capture initialization does not depend on Plasma's socket or portal.
`RuntimeMaxSec=4s` stopped it; its expected timeout status was reset and the
transient unit was collected. The live user's desktop was not stopped.

The final daemon owns three `/dev/uinput` descriptors. Kernel input devices
`libvirtualhid Keyboard`, `libvirtualhid Mouse`, and
`libvirtualhid Mouse (Absolute)` exist. Relative mouse input supports Android
trackpad operation; no synthetic clicks/keystrokes were injected while the
user worked. The phone must still test actual pointer and keyboard delivery.

The same UID's PipeWire-Pulse monitor was read for two seconds without playback
or changing its default sink. Existing user lingering, PipeWire sockets and
WirePlumber are enabled. Sunshine stream audio remains a client test.

The local helper successfully authenticated to `/api/config` and `/api/pin`
while validating the generated certificate. No phone was paired; pending list
was empty. Admin directory 0700, plaintext admin JSON 0600, salted hash JSON
0600. No password was printed. User Sunshine unit is masked; the system unit
is enabled and running, with zero automatic restarts at verification.
UFW reports inactive; no firewall or router settings were changed. Sunshine's
idle TCP listeners are 47984, 47989, 47990 and 48010. Streaming UDP sockets open
when a client starts a stream.
The helper preserves the pairing request's device name by default, so both
phone and laptop can pair with distinct identities. Neither client was paired
during this implementation phase.

## Resolution tests

At 14:22:55, KWin accepted the definition created by:

```
kscreen-doctor output.DP-3.addCustomMode.2400.1080.60000.reduced
```

Selecting that mode reported `applying config failed` / driver rejection.
KWin's journal recorded `Atomic modeset test failed! ... invalid argument`.
An independent three-second systemd timer was already armed to restore
2560×1440@144. The custom definition was subsequently removed. This rules out
that tested timing on this output/driver; it is not proof that every possible
custom timing or EDID override is impossible.

At 14:30:07, the optional prep script successfully applied the advertised
2560×1440@60 mode. At 14:30:10, the independently armed timer ran its undo and
restored 2560×1440@144. JSON comparisons verified both monitors' enabled flags,
modes, positions, scale and rotation matched their initial states. The recovery
snapshot was removed. A 2400×1080 plan correctly leaves the output unchanged.
No mode-changing prep command is enabled in the active Desktop app.

Read-only NVIDIA source inspection found overridden EDID handling in
`/usr/src/nvidia-595.99.02/nvidia-drm/nvidia-drm-connector.c`:
`drm_edid_override_connector_update()` feeds the EDID blob and `overrideEdid`
into `nvKms->getDynamicDisplayInfo()`. Runtime `edid_override` and `force`
files exist under `/sys/kernel/debug/dri/0000:01:00.0/DP-4/` and `DP-5/`.
Both connectors are disconnected. No writes were made to these controls.

KWin 6.6.7 source creates virtual outputs in
[`ScreencastManager::streamVirtualOutput`](https://github.com/KDE/kwin/blob/v6.6.7/src/plugins/screencast/screencastmanager.cpp).
They belong to the compositor; they are not DRM scanout connectors that KMS
can capture. The pinned Sunshine `kwingrab.cpp` captures an existing wl_output;
it does not itself request a native-sized virtual output. A session-only
implementation would need additional lifecycle handling and a different
capture path; it was not substituted for pre-login KMS.

## Persistent and temporary resources

- Running: `/etc/systemd/system/sunshine-host.service` and its enable symlink.
- Mask: `~/.config/systemd/user/app-dev.lizardbyte.app.Sunshine.service`.
- Private runtime configuration, TLS keys, logs and pairing state:
  `~/.config/sunshine/`; private admin password: `~/.config/sunshine-admin/admin.json`.
- Installed helpers: `~/.local/libexec/sunshine/admin.py`, `display-mode.py`.
- Optional display state directory: `~/.local/state/sunshine/`; no pending snapshot.
- Package-owned udev/module files and binary capabilities are listed in README.
- Temporary mode timers `sunshine-mode-test-revert` and
  `sunshine-mode-restore-test` completed and were collected. The isolated
  capture test was stopped and collected. No task-owned test processes remain.
- Scratch: `~/.cache/agent-scratch/sunshine/`; bulky downloads/source were
  removed after verification. Live evidence remains in Sunshine's journal/log.

## Required user validation

Pair both the Android phone and laptop on LAN; stream Desktop and test each
client's mouse, typing and audio. Then reboot without logging in locally and
test SDDM → password → Plasma in the same stream, once from each client.
If video freezes, reconnect without re-pairing and preserve the boot journal.
Exact phone resolution and private physical monitors remain unresolved on the
current active physical-display configuration. No autologin fallback was
deployed: that would bypass normal PAM wallet unlocking and can cause KWallet
or Chrome secret prompts after reboot.
The prep helper consumes the launching client's dimensions/FPS, with no phone
size hardcoded. A fresh app launch (not resume) is required when changing
clients. Independent simultaneous host resolutions are not available on one
shared physical desktop. Laptop OS is still unknown; OS-specific input capture
notes and desktop shortcuts are in README.


## Native virtual-display build — 16:15 local time

Implemented in `../../Sunshine`, branch `feat/kwin-virtual-display` from
`v2026.914.233613` / `63d35f702ee9e362e43263742981836ec0710384`. Source changes are
uncommitted and unpushed; the implementation worker did not run a review loop.

The existing KWin backend already supports PipeWire, DMA-BUF, CUDA GL import and
NVENC. New `virtual_display` and `virtual_display_scale` settings opt into an
owned output. A real-capture flag distinguishes actual Moonlight sessions from
Sunshine's encoder probes. Probes and the configured KMS fallback use physical
DP-3; real streams first try a newly created KWin output using the client's
width, height and rational refresh rate. KScreen selects the mode/scale by the
unique owned output name, then verifies the result before capture continues.
Failed initialization closes the output and returns to KMS with an explicit log.

Primary implementation files: `src/platform/linux/kwingrab.cpp`,
`src/platform/linux/kwin_virtual.h`, `src/platform/linux/misc.cpp`,
`src/video.{h,cpp}`, `src/config.{h,cpp}` and `src/input.{h,cpp}`. Configuration
behavior is documented in the fork's `docs/configuration.md`. Regression tests
cover probe suppression, mode validation, fractional FPS, pixel/scale separation
and cursor positioning. Opt-in live tests exercise actual KWin/PipeWire capture.

### Creation, refresh and removal evidence

KWin 6.6.7 `ScreencastManager::streamVirtualOutput()` owns the lifetime of the
virtual output and removes it on the Wayland stream's `finished` signal.
`DrmVirtualOutput` starts with a 60,000 mHz mode, advertises CustomModes, and
updates both its render loop and software vsync monitor when applying a mode.
Its implementation accepts physical pixels despite the protocol XML's historical
"logical resolution" argument descriptions. Plasma can override the initial
scale, so the new backend applies and verifies scale after discovery.

A bounded standalone probe created 3120×1440, set only its virtual mode to 90 Hz
and scale 2, and automatically removed the output after eight seconds. JSON
comparisons showed no changes to the two physical monitors. No physical mode,
position, scale, power or primary-output change was requested.

The compiled backend then passed three opt-in live capture cases, all receiving
a correctly sized DMA-BUF frame and removing their output on teardown:

- 3120×1440 @ 60 Hz, scale 100%, logical 3120×1440.
- 2880×1800 @ 90 Hz, scale 150%, logical 1920×1200.
- 2400×1600 @ 59.94 Hz, scale 200%, logical 1200×800.

The second and third cases exercised virtual custom-mode creation and selection
through the new C++ path. Final KScreen output contained only the two original
physical monitors, with modes, positions, scale, rotation, priority, replication
and enabled flags unchanged.

### Phone evidence

Moonlight 12.2 on the Samsung S26 Ultra connected using its existing pairing and
Desktop app. The native stream created `Virtual-Sunshine-749765-0` at (5120,0),
3120×1440 @ 60 Hz, scale 1. Physical HDMI-A-5 remained 2560×1080 @ 60 at (0,183);
DP-3 remained 2560×1440 @ 144 at (2560,0). Both remained enabled at scale 1.

Host logs at 16:08:46:

```
[kwingrab] Created owned virtual output Virtual-Sunshine-749765-0 3120x1440@60 scale=1
[pipewire] Size: 3120x1440
[pipewire] using DMA-BUF buffers
Creating encoder [hevc_nvenc]
[pipewire] Sunshine frame pacing: enabled (16.666666ms)
Streaming bitrate is 36588000
```

Phone log: `c2.qti.hevc.decoder.low_latency` hardware decoder, crop-right 3119,
crop-bottom 1439 and frame-rate 60. Its codec buffer is padded to width 3136;
the displayed/cropped image is exactly 3120×1440. Screenshots showed full-width
wallpaper and all four edges of a 3120×1440 test frame without aspect-ratio bars.
The screenshots were task-only verification data and were deleted afterward.

A temporary Qt text field on the virtual output recorded Moonlight trackpad
movement/clicks (`mouse press local=1615,773`) and an Android hardware-key event
forwarded through Moonlight (`key press code=65`). After force-stopping only
Moonlight, Sunshine logged `CLIENT DISCONNECTED` at 16:10:21 and closed PipeWire.
The virtual output disappeared. The test window reported relocation first to
HDMI-A-5, then DP-3, and automatically closed 200 ms after relocation. This
verified window recovery without moving or closing any user window.

The first Qt placement attempt exposed a KDE limitation: an application's
`setScreen()` request still allowed KWin to place its window on the saved physical
HDMI output. That task-owned test window was closed. A temporary script scoped
only to the exact test-window title moved the later test onto the virtual output;
that script was unloaded. No persistent application-placement rule was installed.
The remote cursor starts on the virtual screen; click the desired window before
typing, because the local and remote users share a keyboard/mouse seat.

Wireless ADB subsequently became offline and a single reconnect returned
`Connection refused`. No ADB server was killed/restarted. The explicit Moonlight
Quit menu was not separately retested after that; transport-disconnect removal
and backend-owned teardown were verified. The task-created
`/sdcard/sunshine-ui.xml` remains on the phone because ADB is unavailable; it
contains only Moonlight's app/host-list UI, not a desktop screenshot.

### Build, checks and install

Built with GCC 15.2, CUDA NVCC 13.1.115, CMake 4.2.3, Ninja, pinned submodules,
Boost 1.89 fetched by the upstream build, and pinned FFmpeg prebuilt dependencies.
All full native builds used `~/.local/bin/heavy`; binary and tests built cleanly.
The unchanged Web UI/assets were copied from the matching official package.
`uv sync --locked` and the required `lb-update-clang-format` ran successfully;
`git diff --check` and installer shell syntax checks passed.

- Focused regression/config/input run: 99 passed.
- Noninteractive suite: 588 passed, 6 platform/build/hardware skips, zero failures.
- Existing EncoderVariants integration: NVENC passed; VAAPI skipped. Its software
  variant entered an interactive XDG portal request, so the task-owned runner was
  terminated and the noninteractive suite excluded that three-case integration
  group. No screen-selection dialog was approved. The live phone's HEVC NVENC
  path and separate native CUDA/DMA-BUF tests supply the relevant GPU checks.
- Final opt-in native-output matrix: 3 passed (phone, laptop-like 90 Hz and
  fractional 59.94 Hz), total 732 ms. Each test owns and removes its virtual output.

Build/test logs remain under `../../Sunshine/cmake-build-kwin/` for review.
The upstream test suite wrote a task-created `portal_token` and `tests/hello*`
files into the user's Sunshine config directory; these were identified by their
creation times and removed. Future broad runs should use a private
`XDG_CONFIG_HOME` under the build directory, with config migration disabled.

The first side-by-side service smoke test failed to find shaders because an
absolute asset directory was incorrectly combined with CMake's install prefix.
The rollback script restored the official service, demonstrating rollback, and
the build was corrected to relative `share/sunshine`. Final startup validates
H.264, HEVC and AV1 NVENC through KMS. Only the expected AV1 YUV444 probe fails.

Active binary: `/opt/sunshine-puig/bin/sunshine`, SHA-256:
`8a954738811f903660e01b48515447dc7a4aec0091ce0f04dcd480ad247999e8`.
Capabilities: `cap_sys_admin,cap_sys_nice=p`, matching the official package model.
Active system service PID: **749765**, zero automatic restarts. It remains enabled
for boot; the new drop-in changes only ExecStart. Config is
`~/.config/sunshine/sunshine-virtual.conf`; the original config, binary,
credentials, TLS keys and pairing state were preserved. See README for build,
installation, activation and one-command rollback commands.

### Latency, pre-login and remaining limits

The tested path imports DMA-BUF into the existing CUDA/GL conversion pipeline
and uses NVENC, avoiding CPU screen readback. It still performs GPU color
conversion; "DMA-BUF" does not mean every GPU copy has been eliminated. KWin's
variable-rate capture is paced by Sunshine at 16.667 ms for this client. The
phone selected its low-latency hardware HEVC decoder. No numerical end-to-end
latency, sustained animation FPS or Wi-Fi jitter measurement is available from
these info-level logs; no latency number is inferred from setup timestamps.

KMS is still used when the user's Wayland socket/PipeWire/compositor is absent,
including the greeter. An already running greeter stream needs a reconnect after
login to select the native virtual backend. No SDDM-to-Plasma transition or reboot
was performed. A real laptop was not available; differing dimensions, 90 Hz and
fractional-rate handling were verified directly against this live compositor.
The standard 10-second network timeout is retained; a clean control-connection
close may tear down sooner. Multiple concurrent clients share the first capture
owner's mode. Native absolute touch/pen remain disabled and untested; absolute
mouse bounds use the virtual output's logical geometry. New application windows
can retain physical-monitor placements, and the virtual monitor has no panel by
default. This is an additional monitor in the shared session, not an independent
RDP login, desktop seat or private desktop.

### Packages and persistent resources

`BUILD-DEPENDENCIES.txt` records all 73 newly installed packages and 12 dependency
updates from this task. Most are build development packages, including minimal
CUDA compiler/runtime development components; no NVIDIA driver/kernel package
was changed. APT upgraded the PipeWire security revision from
1.6.2-1ubuntu1.2 to 1.6.2-1ubuntu1.3 and libxrender1 while resolving development
dependencies despite `--no-upgrade`. KWin PID 2228, PipeWire PID 1727 and
PipeWire-Pulse PID 1737 retain their original 2026-11-24 start timestamps; none
was restarted. No SDDM, KWin, Plasma, session, VT, boot, firewall or router change.

Persistent task resources: `/opt/sunshine-puig/`, the `90-virtual-build.conf`
system-unit drop-in, `sunshine-virtual.conf`, and
`~/.local/share/applications/dev.lizardbyte.app.Sunshine.kwin.puig.desktop`.
Only `sunshine-host.service` remains running for this task. No test process,
virtual output, KWin script, server or watcher remains. Temporary probe/test
permission desktop files and the task screenshots were removed. The fork's
ignored build directories and pinned submodule checkouts remain for review and
rebuilding; no commits or pushes were made.

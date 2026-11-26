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

## Exclusive mirrored desktop — prepared 2026-11-26, not deployed

Changes are uncommitted in Sunshine (`feat/kwin-virtual-display`, base
`6cd88313`) and this deployment repo. No service was restarted, no live output
was reconfigured, no input was injected into the live seat, and nothing was
installed into `/opt` or `/etc`. The live configuration remains unchanged.
The prepared installer selects `virtual_display_mode = exclusive` with
`virtual_display_restore_delay = 5`; the fork's compatibility default is `extend`.

### Final mechanism and supported behavior

The final policy **never disables a physical connector or sends DPMS-off**.
KWin output-management v2 version 13+ accepts a virtual device UUID as the
replication source of other enabled devices. A small native
`sunshine-kwin-output` helper sets only replication and verifies its protocol
readback. `kscreen-doctor` exposes `replicationSource` in JSON but has no setter.
The guardian makes the virtual source primary and mirrors it onto the physical
connectors. All physical modes and scales stay in place. KWin and Plasma expose
one logical screen, so application windows, panels and the desktop belong to the
virtual source. Physical monitors show scaled copies; real NVIDIA scanout and
aspect-ratio fitting remain part of the supervised live check.

If mirroring is unavailable, the fallback keeps physical outputs enabled at
existing modes/scales, temporarily places their logical coordinates beyond the
virtual viewport, and sends application windows and all panels to the virtual
output. The original primary Plasma desktop follows it. Physical monitors retain
wallpaper desktops, including desktop icons/widgets, without panels or
application windows after routing. Logs identify `policy=mirror` or `policy=move`.
Both paths were tested in headless KWin, including a deliberately missing native
replication helper for the fallback.

Before virtual creation, the independent Python guardian writes/fsyncs a 0600
snapshot of connector modes, positions, scale, rotation, enabled status,
priorities, replication source names, window opaque IDs/visible geometry/state,
and Plasma desktop/panel IDs and connector assignments. It records no titles,
widget contents or application data. A previously disabled connected original
output causes exclusive initialization to fall back, because restoring it could
otherwise require an explicit disable request.

After capture ends the C++ lease retains the protocol-owned output for five
seconds. A reconnect claims and resizes the same output, including landscape to
portrait, while the physical replicas stay enabled. Removing/adding outputs can
cause KWin to reload a remembered layout, so a retained output avoids that hotplug
transition. Once grace expires, restore clears temporary replicas, restores the
connector/Plasma layout and surviving window geometry, then releases the output.
Normal exit and guardian/parent crashes bypass grace. The systemd drop-in's
`ExecStopPost` covers a crash that kills the whole service group. Exact-restore
failure enables preferred modes and retains the snapshot for retry. The manual
emergency command and detailed live steps are in README.

### Changed files

Sunshine configuration and lifecycle wiring: `src/config.cpp`, `src/config.h`,
`src/platform/linux/kwingrab.cpp`; new lease implementation
`src/platform/linux/kwin_exclusive.cpp` and `.h`; guardian asset
`src_assets/linux/assets/kwin-exclusive.py`; native replication tool
`tools/kwin_output.cpp`; build/install wiring in
`cmake/compile_definitions/linux.cmake`, `cmake/packaging/linux.cmake`, and
`tests/CMakeLists.txt`; settings/recovery documentation in `docs/configuration.md`.

Tests: `tests/unit/platform/linux/test_kwin_virtual.cpp`, new
`tests/unit/platform/linux/test_kwin_exclusive.py`, and new nested harness/owner
`tests/integration/linux/kwin_exclusive_nested.py` and `kwin_virtual_owner.c`.

Deployment: `sunshine/use-virtual-build.sh`, `sunshine/rollback-virtual-build.sh`,
`sunshine/sunshine-virtual-build.conf`, `sunshine/README.md`, and this verification
record. The base official-package config and service unit remain unchanged.

### Validation

- `heavy cmake --build cmake-build-kwin --parallel 12 --target sunshine test_sunshine`:
  passed; also builds and stages the new replication helper. Existing Boost
  Process `wait_for` deprecation warnings remain.
- Focused C++/NVENC/config checks: **17 passed**. Includes native portrait mode
  validation and an actual display-free HEVC NVENC encode at 1440×3120, plus
  existing probe suppression, fractional-refresh and scale checks.
- Pure Python guardian tests: **11 passed**. Cover semantic mode IDs, scale and
  priority readback, original replication identity, durable/private snapshots,
  reconnect preservation, failed persistence, invalid snapshots, preferred-mode
  fallback, no disable commands, and pre-login/no-snapshot restore without D-Bus
  or a Wayland session.
- `heavy python3 tests/integration/linux/kwin_exclusive_nested.py`: **passed**.
  Private D-Bus, Wayland, PipeWire, Plasma and XDG state; two original outputs with
  unequal positions, reversed primary and fractional scale. Tests prove enabled
  physical replicas, one logical Plasma desktop/panel, retained-source resize
  from 3120×1440 to 1440×3120, exact original layout and normal/maximized/fullscreen
  window geometry restoration, parent EOF recovery, disk recovery after guardian
  SIGKILL, active emergency restore, and the unavailable-replication fallback.
- The nested run includes **3 C++ lease tests**: grace/ownership resume and final
  expiration; recovery after killing the guardian; and immediate restore when an
  active lease's process exits while its independent Wayland owner remains alive.
- `git diff --check`, changed C/C++ clang-format checks, Python syntax checks and
  installer/rollback shell syntax checks passed.

Build and verification logs remain in Sunshine's ignored `cmake-build-kwin/`:
`exclusive-build.log`, `exclusive-unit.log`, and `exclusive-nested.log`.
The nested harness waits for its private Wayland socket and KWin D-Bus name
before starting Qt display tools, guards later queries, and disables KCrash UI
inside the isolated environment. Its temporary state and process groups are
cleaned after each run. The implementation child did not run an authoritative
review loop, commit or push.

### Remaining live validation and limits

The new binary/helper/drop-in are not installed. Read README's supervised live
sequence before deployment: verify both physical screens stay enabled and mirror
landscape/portrait, then verify grace expiration, exit/crash and emergency
restore. The existing service and pre-login KMS path were not restarted or
retested. Android rotation-triggered reconnect is a separate client change.

Visible saved geometry and states are tested. Closed windows cannot be recreated;
KWin scripting does not expose the hidden restore rectangle of a maximized or
fullscreen window, nor a serializable custom tiling tree. Such application/tiling
behavior can limit exact restoration; failures retain the snapshot. Missing or
changed monitor hardware can require the preferred-mode fallback. Actual
Moonlight input/audio and NVIDIA physical mirror scanout still need the live test.

## Advisory review follow-up

All five review IDs were addressed without deployment, service restarts or live
monitor/input changes; changes remain uncommitted.

- **A1 fixed:** versioned recovery records bind to the boot, KWin D-Bus owner and
  Wayland socket identity. Legacy, corrupt and foreign-session records are
  archived without applying them. A durable output-stage journal prevents
  replaying modes/positions after completed or interrupted recovery. Directory
  metadata is fsynced after snapshot archival/deletion as well as writes.
- **A2 fixed:** restore resolves saved desktops/panels by containment ID, including
  inactive containments, retries connector availability with a bounded timeout,
  then verifies every assignment. Mismatches retain the snapshot while surviving
  windows are still restored. Retained Sunshine outputs are parked outside the
  physical viewport: otherwise Plasma can classify an overlapping restored
  connector as redundant and never expose it through `screenForConnector`.
- **A3 fixed:** preferred-mode fallback selects an available physical primary,
  moves panels/desktop and surviving windows onto available physical outputs,
  verifies that window migration, and retains the original recovery record.
- **B1 fixed:** both helper assets are staged by a test target after
  `SUNSHINE_TEST_RUNTIME_DIR` is defined. A non-default runtime directory was
  configured and both staged files were checked against their source/build
  hashes; the build configuration was then reset to its original runtime path.
- **B2 fixed:** an isolated test uses actual `kwin_display()` system-memory
  capture, releases the landscape display, resumes the same retained screencast
  at portrait resolution, receives portrait PipeWire frames and verifies final
  output removal. The existing test hook explicitly clears the platform input
  handle before platform setup, so cursor positioning cannot reach uinput. The
  private PipeWire instance uses WirePlumber's policy-only profile, which loads
  no audio/Bluetooth/camera hardware monitors. The test follows the production
  capture reinit contract if PipeWire initially advertises the previous format.

Final verification: **19 Python tests passed**, **17 focused C++/NVENC/config
checks passed**, and the expanded nested suite passed with **exit 0**, including
three lease tests, the real capture reconnect test, missing-Plasma recovery,
newer-layout preservation, foreign-boot archival and unplugged-monitor degraded
recovery. Test servers use their own process groups for deterministic cleanup.
Syntax, clang-format and diff whitespace checks passed.

Logs: Sunshine's ignored `cmake-build-kwin/exclusive-review-fixes-build.log`,
`exclusive-review-fixes-python.log`, `exclusive-review-fixes-unit.log`,
`exclusive-review-fixes-nested.log`, and `exclusive-staging-*.log`.
No authoritative publishing review, commit, push or live supervised validation
was performed by this implementation child.

## Android native rotation and per-client scale — 2026-11-26

Implemented, built and left uncommitted for orchestrator review. No live host
redeployment or phone installation occurred in this pass.

The companion Android fork (upstream revision `b48494cb`, branch
`feat/portrait-rotation`) builds a separate `com.limelight.puig` debug APK,
labeled **Moonlight Puig**. Native fullscreen selection follows the phone's
orientation using native panel dimensions; fixed resolution choices keep
upstream behavior. Android rotation lock is respected. Axis changes coalesce
for 250 ms, stop only the stream, wait for native teardown, then quietly recreate
the stream activity. The host app is never cancelled. Backgrounding during that
transition cancels recreation. Same-axis and 180-degree changes do not reconnect.

The Android HTTP layer sends `virtualDisplayScale` on both `/launch` and
`/resume`: densityDpi / 240 as a percentage, rounded to a 25% step and clamped to
100–400%. Sunshine strictly parses the integer and carries it through the launch
session and RTSP capture configuration into KWin creation and retained-output
resize. Missing or invalid values use the configured fallback. The installer
now writes a moderate **150%** fallback for clients without a hint. The host's
compiled compatibility default stays at 100%. Official Moonlight omits the
parameter; official Sunshine ignores unknown query parameters. No native
Moonlight library or submodule changes were necessary.

Validation:

- Android `testNonRootDebugUnitTest assembleNonRootDebug lintNonRootDebug`:
  successful; **3 JVM tests passed**, lint completed without errors. Tests cover
  density rounding/bounds, both physical orientations, and reconnect axis policy.
  APK identity and label verified with `aapt`; all four upstream ABIs included.
- Sunshine `heavy cmake --build cmake-build-kwin --parallel 12 --target sunshine
  test_sunshine`: successful. Existing Boost Process deprecation warnings remain.
- Focused KWin mode and client-scale checks: **7 tests passed**, including strict
  scale parsing, invalid-value fallback and independent client selection. Old
  gcov data produced checksum warnings after recompilation; test assertions passed.
- Isolated nested KWin suite: **all functional assertions passed**, including
  real PipeWire portrait frames, retaining the same output while switching from
  the configured 100% fallback to a client 250% hint, logical desktop dimensions
  576×1248 at 1440×3120 physical pixels, and unchanged global configuration.
  The inner session exited 0. The outer harness exited 1 during temporary-directory
  cleanup because an auto-started document portal's FUSE mount briefly returned
  `ENOTCONN`. After its mount/processes exited, the task-owned residue was removed.
  The harness source was not changed for that unrelated cleanup race.
- Changed C++ clang-format, diff whitespace and installer shell syntax checks:
  passed. Host monitors and recovery state were inspected read-only.

Build/check logs and a copy of `Moonlight-Puig-debug.apk` are under
`~/.cache/agent-scratch/moonlight/`. The APK also remains at the Android fork's
`app/build/outputs/apk/nonRoot/debug/app-nonRoot-debug.apk`.

Blocked real-device work: the designated phone had no advertised wireless ADB
service or open debugging port; a direct legacy-port connection was refused.
No other device or emulator was accessed. The official app was not modified.
Installation, pairing, copying settings, phone screenshots, live rotation/grace
timing, and removing the old phone-side UI XML remain pending Wireless debugging.
No phone rotation setting was changed. No screenshot exists for this pass.
The real host service/session was not restarted; its current configuration is
unchanged. Both original physical connectors remain enabled in normal geometry,
and no exclusive-layout snapshot was present.

### Follow-up live device check

The separate debug APK is now installed for Android user 0 and paired through
Sunshine's local `/api/pin` endpoint. Admin credentials were consumed only by
the request command and were never printed or saved in task files. The official
Moonlight package remains installed and unchanged. The companion preferences
match native fullscreen (3120×1440), 60 FPS, lowest-latency frame pacing,
47 Mbps, trackpad touch, and remote-desktop mouse mode. Resolution, FPS,
frame pacing and bitrate were inspected in the official settings UI; the two
mouse options were applied as requested.

Sunshine was redeployed with `use-virtual-build.sh` after checking the journal
and idle server state. Only `sunshine-host.service` restarted. The deployed
configuration fallback is 150%. A real portrait connection requested and captured
**1440×3120 at 60 FPS, scale 3.75**, as recorded by the host journal. The phone
reports logical density 600 dpi, so the client's measured 375% hint overrides
that fallback correctly. A native portrait screenshot was captured under
`~/.cache/agent-scratch/moonlight/portrait-start.png`; it contains the real shared
desktop and remains outside Git.

The phone changed to another foreground app before the rotation sequence, so
that stream ended normally on backgrounding. A second controlled attempt also
lost the foreground before connecting. Phone input was paused to avoid tapping
another app. Landscape/reverse-portrait reconnect timing is **not yet verified**;
no claim is made about a successful live orientation reconnect from these runs.

The original `accelerometer_rotation`, `user_rotation` and temporary screen
sleep timeout were restored. `/sdcard/sunshine-ui.xml` and the task's
`/sdcard/moonlight-ui.xml` were removed. Readback verified both original host
connectors enabled at their previous modes, positions, scale, rotation,
priorities and replication state; no exclusive-layout snapshot remains.
Logs and before/after layout JSON are outside Git under the same scratch path.
A short hands-off phone window is needed to complete both rotation directions.

### Final scale policy and successful rotation check

Auto now uses **densityDpi / 240**, rounded to 25% steps and bounded to 100–400%.
The initial 375% live measurement above used the earlier 160 dpi baseline and
is superseded. The new baseline gives this 600 dpi client **250%**, with
576×1248 logical pixels in native 1440×3120 portrait. The Android streaming
settings now include **Desktop scale**: Auto (default), or explicit 100–400% in
25% steps. A saved explicit choice applies on the next connection; invalid
restored values safely fall back to Auto.

Changed Android files for this adjustment: `NativeDisplayPolicy.java`,
`NativeDisplayPolicyTest.java`, `preferences/PreferenceConfiguration.java`,
`Game.java`, `res/xml/preferences.xml`, `res/values/arrays.xml`,
`res/values/strings.xml`, and the fork README. Only default English resources
were added. This deployment README and verification record describe the new
policy. Sunshine's per-session transport/selection code required no further change.

After the phone was left untouched, the rebuilt debug-signed update installed
for user 0 and retained its pairing. `testNonRootDebugUnitTest`,
`assembleNonRootDebug` and `lintNonRootDebug` passed: **5 JVM tests**, including
600 dpi → 250%, rounding boundaries, bounds, every supported explicit override,
and invalid saved values. A resource check confirmed Auto and all 13 explicit
choices, with Auto as default.

The live sequence completed successfully:

- Portrait start: **1440×3120 @ 60 FPS, 250%**.
- Landscape rotation: **3120×1440 @ 60 FPS, 250%**. The new client connected
  0.233 seconds after the old disconnect; the requested mode was applied after
  0.399 seconds, within the five-second grace.
- Portrait rotation back: **1440×3120 @ 60 FPS, 250%**. The new connection
  followed disconnect by 0.234 seconds; portrait mode applied after 0.354 seconds.
- Both rotations reused the exact same virtual output and PipeWire node.
  **40 KScreen samples**, approximately 150 ms apart, showed both physical
  connectors continuously enabled and replicating that same virtual source.
  The recovery snapshot remained present throughout; no intermediate physical
  layout restoration was observed.
- Phone screenshots after each change showed the desktop and bottom Plasma
  panel, without a Moonlight dialog or app-list transition. Journal intervals
  above measure control/mode events, not end-to-end visual latency.
- Leaving the stream restored both physical connectors' original modes,
  positions, scale, rotation, priority and replication state. No virtual output
  or exclusive-layout snapshot remained. Original rotation and sleep-timeout
  settings were read back successfully; temporary phone XML files were removed.
  The resumed phone activity was verified as the Samsung Home launcher.

The host logged its existing unknown-virtual-connector diagnostic and a PipeWire
format reinitialization during each resize; frames resumed successfully after
that expected capture reinitialization. No compositor/session restart occurred.

Evidence outside Git under `~/.cache/agent-scratch/moonlight/`:
`portrait-start.png`, `landscape-rotation.png`, `portrait-return.png`,
`live-phone-journal.log`, `rotation-layout-samples.json`, the live-layout JSON
files, and `android-scale-build.log`. The updated APK copy is
`Moonlight-Puig-debug.apk`. All source/deployment changes remain uncommitted;
no authoritative publishing review, commit or push was performed by this child.

### Touch-sized panel follow-up — prepared, not deployed

Only the helper's panel thickness behavior changed. Original panel thickness is
saved with each panel ID/connector in the same durable exclusive snapshot.
While those panels are on the virtual output, the helper sets
`max(original thickness, 44, round(28 * output scale))` logical pixels: the
usual 44-pixel panel becomes **70 at 250%**, or 175 stream pixels. This is a
scale-based estimate in the requested 64–72 logical-pixel range, not a measured
physical-DPI conversion. A larger original thickness is preserved, and panels
created later without an original saved thickness are not resized. No contents,
location, alignment, length or hiding settings are written by this adjustment.

A separate `panel_recovery` stage records `restoring` before applying saved
thickness and `exact` after verified readback. Normal disconnect, Sunshine/guardian
crash, emergency and degraded-output recovery all restore thickness. Failed
readback retains the snapshot while window/connector recovery still proceeds.
Once thickness restoration completes durably, later placement/window recovery
retries do not overwrite a subsequent user thickness adjustment. Older snapshots
without thickness values preserve their existing recovery behavior.

Files: Sunshine `src_assets/linux/assets/kwin-exclusive.py`,
`tests/unit/platform/linux/test_kwin_exclusive.py`,
`tests/integration/linux/kwin_exclusive_nested.py`, and `docs/configuration.md`;
this deployment README and verification record. No Android change was needed.

Validation: **23 pure guardian tests passed**; helper staging/build passed;
the expanded **nested KWin suite exited 0**, including actual Plasma panel
44→70 at 250%, original thickness in the snapshot, unchanged thickness across
retained-output landscape/portrait, exact disconnect restore, parent EOF,
guardian SIGKILL plus disk recovery, the three C++ lease recovery cases,
manual emergency restore, degraded recovery, and preserving a later 52-pixel
user choice across a completed-stage retry. Real capture/portrait PipeWire
reconnect tests also remained green. Python syntax and diff whitespace checks
passed. Logs: `~/.cache/agent-scratch/moonlight/panel-unit.log`,
`panel-build.log`, and `panel-nested.log`.

The nested session cleaned up successfully. This follow-up did not redeploy or
restart the live host, change the live desktop/panels, or access the phone.
A current hands-off phone window was not confirmed, so live verification of
this particular thickness adjustment remains pending. Earlier Android rotation
screenshots show the previous panel thickness. Changes remain uncommitted.

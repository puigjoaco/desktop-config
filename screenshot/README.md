# Quick Region Overlay

Meta+Shift+S and Print freeze the screen and copy a dragged region to the clipboard through the resident overlay daemon. Launching Spectacle from the application menu remains stock and opens its normal window.

## Requirements

- KDE Plasma 6 on **Wayland**. The overlay uses KWin's ScreenShot2 interface and layer-shell surfaces, which don't exist on X11. Developed and tested on Kubuntu 26.04 (Plasma/KWin 6.6.6).
- Spectacle installed (it stays the stock application-menu entry).
- On NVIDIA hardware, the proprietary driver (`sudo ubuntu-drivers install`), not nouveau, so that Wayland runs properly.
- Tested on a three-monitor setup and a single-output virtual session; mixed scales and laptop hardware are untested.

## Install

Install the dependencies, then clone and run the installer:

```sh
sudo apt install git build-essential cmake pkg-config libwayland-dev libwayland-bin qt6-base-dev qt6-wayland libkf6guiaddons-dev liblayershellqtinterface-dev libkf6globalaccel-dev libkf6config-bin qdbus-qt6 libkf6service-bin kde-spectacle ydotool jq coreutils gawk sed grep perl util-linux systemd dbus-user-session
git clone https://github.com/puigjoaco/desktop-config.git
sudo usermod -aG input "$USER"   # then log out and back in
systemctl --user enable --now ydotool.service
cd desktop-config/screenshot && ./install.sh
```

The clipboard stack reads held modifier keys from `/dev/input` and types copy/paste keys through `ydotoold`, which needs the `input` group for `/dev/uinput`. Log out and back in after adding the group, before starting the service.

`heavy` is optional: when available it wraps the build; otherwise CMake builds directly. Missing dependencies or a failed CMake configure print the dependency command before desktop files or shortcuts change. Spectacle's stock `_launch` owns both Print and Meta+Shift+S; install transfers both to the daemon and preserves any other keys. The original complete key sequences are recorded in `~/.local/state/quick-region-overlay/fresh-spectacle-keys` before the transfer. `./rollback.sh` removes the daemon and restores exactly those keys, leaving the stock Spectacle menu entry and pipeline in place. The record remains for repeatable rollback; a new install after a completed rollback records the current keys again. Do not remove it while the daemon is installed. `--originals` is only for legacy installs.

## Clipboard stack

- **Ctrl+Alt+C** copies the focused app's selection and adds it to the stack. Normal Ctrl+V still pastes that last copy.
- **Ctrl+Alt+S** freezes the screen and adds the selected region to the stack and clipboard. Print and Meta+Shift+S copy a region without adding it.
- **Ctrl+Alt+V** pastes the stack, oldest first, then restores the previous clipboard with all its formats, except that an image offered as PNG is kept only as PNG: other image encodings (Qt offers about 38, each encoded on request) would take over a second to read. The same applies to images copied with Ctrl+Alt+C. Pasting keeps the stack.
- **Ctrl+Alt+X** clears the stack.

By default, after a successful stack paste, the next successful copy or stack screenshot starts a new stack containing just that item (OSD: “New stack: 1 item”). You can paste the same stack repeatedly until then. A failed or empty copy leaves the previous stack intact; an aborted paste does not mark it as pasted. Clearing removes both items and the marker.

To keep appending after pasting, or enable the default behavior again:

```sh
kwriteconfig6 --file quick-region-overlayrc --group Stack --key NewStackAfterPaste false
kwriteconfig6 --file quick-region-overlayrc --group Stack --key NewStackAfterPaste true
```

The daemon reads `[Stack] NewStackAfterPaste` in `~/.config/quick-region-overlayrc` (or `$XDG_CONFIG_HOME/quick-region-overlayrc`) on every add; no restart is needed. Install never writes this setting. Successful pastes record a marker even with the setting off, so enabling it after a paste can start a new stack. A successful add with the setting off appends and retires that marker.

Consecutive text items paste together with one line break between them. Rich-text runs also offer HTML, with plain text escaped. Images and files paste individually; no separators are inserted around them. An item offering `image/*` is an image. Otherwise, an item offering plain text (including charset variants and `UTF8_STRING`) is text unless it also offers a file URI list. Everything else is pasted separately. A single text item keeps every original format; a merged run offers only the joined plain text and HTML, omitting auxiliary ODT, markdown and browser formats. The receiving app decides which formats it accepts.

A discreet 14 px whitish-grey busy wheel follows the pointer while the stack is processing, starting immediately on copy/paste or when a stack region selection closes, and staying visible for at least 400 ms. It passes clicks and scrolling through and never takes keyboard focus; its brief lingering display does not delay the next shortcut.

Stack additions prepare the clipboard restoration ahead of paste: stack screenshots are encoded once and offered through the exact-byte transport, and copies retain their snapshot with its selection generation. After modifier release, paste reuses a live owned offer or a generation-matched snapshot without reading clipboard payloads; a compositor round-trip validates the selection first, and any intervening clipboard change requires a fresh snapshot.

Short Plasma OSD messages show the item count or an error. The stack survives daemon restarts and reboots in `~/.local/state/quick-region-overlay/stack/`, with owner-only directory and file permissions. It holds up to 100 items, at most 64 MiB each across the offered formats; additions over either cap are refused. Clear it when finished with sensitive material. Rollback retains stack data; clear it first if you want the data removed.

The private `stack/pasted` file records the successfully pasted item count. Starting a new stack first saves its new item in a private sibling directory, then atomically exchanges it with the old stack using Linux `renameat2(RENAME_EXCHANGE)`. A process crash leaves either the entire old stack and marker or the new one-item stack, and startup removes interrupted staging/cleanup directories. Filesystems without directory exchange refuse the replacement and retain the old stack. A full pasted stack can still start a new stack; the 100-item limit applies to appending.

Copy/paste requires `ydotool`, its active user service (`systemctl --user enable --now ydotool.service`), and readable keyboard devices in `/dev/input` (membership in the `input` group, effective after logging out and back in). The daemon queries modifier state without reading or recording key events. While the shortcut is still held, it writes the copy/paste keys straight into that keyboard's evdev device (key-ups for Alt, Meta, Shift, C and V, then Ctrl+C/V, then Alt and Shift pressed again if still held, so further shortcuts work without letting go), so copying and pasting start on the key press; the app sees plain Ctrl+C/V. Keys already released go through ydotool. If a held keyboard cannot be written to, release Ctrl/Alt/Shift/Meta within 1.5 seconds; otherwise it aborts with an OSD. KWin's active-window script selects Ctrl+Shift+C/V for recognised terminal emulators, with Ctrl+V for terminal images. If window detection is unavailable, it uses normal Ctrl+C/V. A focus change during an operation stops further input.

Stack paste offers carry `x-kde-passwordManagerHint=secret`, which Klipper honours, so intermediate items do not enter history. Ordinary copy and region capture keep their usual history behavior. Before each next offer the daemon waits for a payload request and completed pipe write, followed by 100 ms without another completed request. If the target does not request data within 1.2 seconds, pasting stops and restores the clipboard. Apps that defer clipboard reads or accept only one kind of item may therefore need individual pastes. Reading all MIME formats allows 10 seconds without progress and 60 seconds overall; slow reads show a short “Reading clipboard…” OSD before any clipboard replacement.

Stopping or restarting the daemon preserves a clipboard it owns through the stack transport. It saves the original clipboard during a paste, or the current offer after a paste, to a private `clipboard-holder-*.qrs` file in `~/.local/state/quick-region-overlay/`, then starts the same binary with `--hold-clipboard` in an independent transient user service (`qro-clipboard-holder-*`). The holder confirms ownership before the daemon exits, removes the snapshot, and serves the exact formats until another app copies or the session ends. It does not register shortcuts or the daemon's D-Bus name. Handoff waits up to three seconds; a failure is logged and the snapshot is retained for recovery. Startup removes holder snapshots older than one day. This covers orderly stops (including reinstall); a forced kill or crash cannot hand off an in-memory clipboard.

The exact-byte stack transport uses Wayland's `ext_data_control_v1`; Qt/KSystemClipboard's image-serving path re-encodes images. Its protocol XML is an unchanged copy from [wayland-protocols 1.47](https://github.com/wayland-mirror/wayland-protocols/blob/1.47/staging/ext-data-control/ext-data-control-v1.xml) (license in the file). Build dependencies additionally include `pkg-config`, `libwayland-dev` and `libwayland-bin`. Install checks these, ydotool and keyboard access before changing desktop files or shortcuts.

The installer sets Plasma clipboard history (`klipperrc`, `[General]`) to `IgnoreImages=false`, so screenshots enter history, and raises `MaxClipItems` to 100 when it is lower or unset; a longer history is left unchanged. It records each previous value, including absent keys, in `~/.local/state/quick-region-overlay/klipper-settings.json`; reinstall preserves that original record until a completed rollback. Rollback restores the values or removes originally absent keys. Both scripts call Klipper's D-Bus `reloadConfig()` when it is running; this applies the settings live without restarting plasmashell (Plasma 6.3 or newer). If Klipper is stopped, the settings apply when it starts. Reducing a history limit can discard older entries; restoring the limit cannot recreate them.

## Install details and roll back

Run these from this directory (`desktop-config/screenshot`). To reinstall or update:

```sh
./install.sh
```

The dependency command is under Install; it includes the Qt 6 development files, compiler, CMake, KF6/LayerShellQt headers and runtime tools. Both scripts hold `~/.local/state/quick-region-overlay/lock` while they run, so a second run of either fails at once; they check all owned files before their first change and are safe to rerun. A failure after a change prints exactly one recovery command. Once the launcher is retired, the rollback mode for that command is kept in `previous-mode` next to the handover marker until the handover completes.

`install.sh` identifies the previous launcher backup, refuses third-party owners of either key, refuses edited old-pipeline files or an edited Spectacle override, records fresh-install keys when applicable, installs the unit, D-Bus service and authorization entry and then the daemon binary, removes the override only when it matches its backup, and rebuilds sycoca. The daemon restarts right after its binary is installed, and only when a file changed, the unit is not active (including failed), or its running executable differs from the installed inode: KWin authorizes ScreenShot2 by the caller's executable path, so a daemon whose binary was replaced can't capture until it restarts. It retires the old launcher, transfers Print from Spectacle when needed, and leaves Spectacle's `_launch` registered without Print or Meta+Shift+S (`_launch=none` when it has no other keys) so a future login cannot restore its desktop-file defaults. Retiring the launcher or taking Meta+Shift+S from stock Spectacle can reset the daemon action: when the action lacks Meta+Shift+S after the launcher is gone, it is unregistered and registered again to acquire both defaults. Print taken from Spectacle is added to the action's current keys, so a rerun while Spectacle holds Print keeps custom and cleared keys. A pending marker under `~/.local/state/quick-region-overlay/handover` (`pending-meta` for Meta+Shift+S, `pending-print` for both defaults, `pending-print-add` for Print alone) records an interrupted handover; a rerun completes that handover instead of reporting a false success. The marker becomes `complete` only after verification. The old clipboard service and wrapper are then stopped and removed. When the override was removed, by this run or an interrupted one, the last line asks you to log out and back in; until then Spectacle's other shortcuts, such as Meta+Shift+Print, still run the removed override's command. `QUICK_REGION_TEST_FAIL` steps are `after-files`, `after-daemon-start`, `after-launcher-retire`, `after-print-retire`, `after-spectacle-empty`, `after-handover` and `after-clipboard-service`.

Installed files:

- `~/.local/libexec/quick-region-overlay`
- `~/.config/systemd/user/quick-region-overlay.service`
- `~/.local/share/dbus-1/services/org.local.QuickRegionOverlay.service`
- `~/.local/share/applications/quick-region-overlay.desktop` (KWin ScreenShot2 authorization)
- the `[quick-region-overlay]` KGlobalAccel registration in `kglobalshortcutsrc`

The daemon also creates stack state as described above and a temporary, owner-only KWin focus script in the runtime directory. It unloads and removes that script at shutdown. Rollback unregisters the four stack actions as well as `capture-region`; reinstall preserves saved or cleared bindings for each action.

These files are generated from this directory and owned by `install.sh`; a manifest records their hashes so edited or untracked installed files are saved under `~/.local/state/quick-region-overlay/saved/` before replacement or rollback removal. Change the sources here instead of the installed copies. Local unit changes made with `systemctl --user edit quick-region-overlay.service` live in a drop-in and survive reinstalls.

To roll back:

```sh
./rollback.sh
./rollback.sh --originals
```

For a legacy install without a fresh-install record, rollback checks the selected backup and every target it will overwrite or delete, restores the override and old pipeline, rebuilds sycoca, stops the overlay unit in any state but inactive or failed and clears a failed state, unregisters the daemon action, restores the old launcher on Meta+Shift+S, and sets Spectacle `_launch` to its existing keys minus Meta+Shift+S plus Print. It uses the same short-lived Spectacle registration helper as install, and both kill only a process they started, also when they fail. It starts the selected clipboard service under `graphical-session.target` and is safe to rerun. When it restored the override, the last line asks you to log out and back in; in a session that loaded stock Spectacle, Print and Spectacle's other shortcuts open Spectacle's window until then, so a real Print press after the re-login is the proper rollback check. Its failure hooks are `after-service-files`, `after-daemon-removed`, `after-launcher` and `after-clipboard-service`.

`kbuildsycoca6` runs through the systemd user manager so it uses the session's XDG environment. Scripts never edit `kglobalshortcutsrc` or `spectaclerc` directly. They read and set Spectacle's keys with `busctl` through KGlobalAccel's `shortcutKeys`, `setShortcutKeys` and `setForeignShortcutKeys`, which carry whole multi-key sequences; the older `shortcut` methods keep only each sequence's first key. The stale `[services][quick-region-screenshot.desktop]` section is intentionally left alone. `spectaclerc` still names the old handoff file as `lastImageSaveLocation`; the scripts leave Spectacle's own settings alone.

## How it works

The daemon registers the KGlobalAccel capture action, `quick-region-overlay/capture-region`, with two defaults: Meta+Shift+S (Qt key code `301989971`) and Print (`16777225`). KGlobalAccel keeps the user's saved keys, including cleared bindings, across daemon restarts and installs. `ShortcutStatus` reports `registered:<key>; <key>` for active keys (a multi-key sequence is written `Meta+Shift+S, Ctrl+C`), `none` for a cleared action, or `none:<component/action>, ...` when another component owns a default. The four stack actions are `stack-copy`, `stack-capture`, `stack-paste` and `stack-clear`, with Ctrl+Alt+C/S/V/X defaults. `StackShortcutStatus` reports their current bindings. `/Stack` (`org.local.QuickRegionOverlay.Stack`) exposes `Count()` and `State()`, `Paste()` and `Clear()` through the shortcuts' existing paste/clear paths, and `Changed(int count, QString state)` whenever the count or operation state changes. `State()` is `<operation>:<count>`; operation 0 is idle, all others are busy. Menu requests are also rejected while a region capture is in progress. The older `[services][quick-region-screenshot.desktop]` section in `kglobalshortcutsrc` is a stale leftover and is not a live binding.

1. `quick-region-overlay` (Qt 6 + LayerShellQt + KF6 GlobalAccel, `quick-region-overlay.cpp`) stays resident in `quick-region-overlay.service`, which starts with the graphical session. A shortcut arrives as a D-Bus signal to that process; no launcher is spawned. D-Bus or KGlobalAccel errors are logged as `shortcut-registration-error`, and `shortcut-status` logs the result. `systemctl --user stop` or `restart` marks the action inactive while retaining its saved keys; rollback unregisters it. `quick-region-overlay --capture`, or `org.local.QuickRegionOverlay.Capture`, asks the service for a capture and D-Bus-activates it if needed. The binary without arguments exits 2 with usage; a second `--daemon` exits 3 and does not restart-loop. The unit requires an active `graphical-session.target`, so D-Bus activation outside a graphical session fails immediately instead of crash-looping. It unsets `QT_WAYLAND_RECONNECT`, so after a compositor crash Qt exits (status 255) and systemd restarts the daemon.
2. The daemon asks KWin's restricted `org.kde.KWin.ScreenShot2.CaptureWorkspace` for the whole desktop at native resolution without the cursor. KWin replies with raw image metadata and writes pixels to a sealed memfd; the daemon validates the metadata before checking layout shape, polls until stride × height bytes are present, then maps the image read-only without a copy. A malformed reply logs `unusable-reply type/size/format/stride`; a layout mismatch is retried after rebuilding windows; a second shape mismatch in the same press fails with `capture-error shape …`.
3. The frozen image is shown dimmed on every monitor as overlay-layer surfaces with an exclusive keyboard grab. Dragging draws an undimmed selection, which can span monitors; release crops exact native pixels and offers them through KDE's `KSystemClipboard` for ordinary captures, or the exact-byte stack transport for Ctrl+Alt+S. Escape closes without changing the clipboard. Pressing the shortcut again while the overlay is open closes it the same way; presses during a capture are ignored (`capture-busy`).

### Print key

Print uses the same overlay action as Meta+Shift+S, so it has the same region selection, clipboard ownership and Escape behavior. The user override `~/.local/share/applications/org.kde.spectacle.desktop` is removed by `install.sh`; the stock `/usr/share/applications/org.kde.spectacle.desktop` is then visible in the application menu and launching it opens Spectacle normally. During install, Print and Meta+Shift+S are taken from Spectacle's `_launch` when that action owns them. The action stays registered with both defaults removed: an empty list is written as `_launch=none`, while any other user keys are preserved. `none` and the remaining explicit keys prevent the desktop-file default from returning at the next login or sycoca rescan. The daemon receives Print. Legacy rollback restores the override and gives `_launch` Print back while preserving other Spectacle keys; fresh-install rollback restores the recorded original keys. Removing or restoring the override reaches Spectacle's other shortcuts only after logging out and back in, because kglobalacceld keeps each component's desktop entry until the next login; both scripts say so when they changed it.

## Not available compared with Spectacle (overlay shortcuts only)

No annotations, editor or toolbar; no OCR, saving, sharing or notifications; no delay timer; no window, active-window, full-screen or single-monitor modes; no adjusting a selection after release; no recording; no cursor in the image. Spectacle covers those features when launched normally or through its other shortcuts.

Each ordinary capture includes KDE's `x-kde-force-image-copy` marker, as Spectacle does, so Klipper stores it as an image even if image history is turned off. The decoded image is still offered immediately; encoding happens only when a clipboard client requests it. Klipper's history size limits how many captures and other clipboard items it retains; copying the same image again moves its existing entry to the top.

Ordinary captures belong to the daemon's `KSystemClipboard` connection until their offer is replaced or the service stops. Klipper retains the capture in history and can restore it after the daemon exits. Stack captures use the clipboard handoff described above.

## Tests

`clipboard-image.h` has a regression test for Klipper's explicit-copy marker, intact image pixels and deferred encoding.

`selection-geometry.h`, `capture-checks.h`, `daemon-logic.h` and `clipboard-stack.h` contain pure decisions covered by their unit tests. The stack tests cover text/image/file classification, real-app auxiliary formats, HTML escaping and paragraph boundaries, charset decoding, ordered runs, exact persistence bytes, malformed lengths and both caps. The shell helpers also check a real running executable whose installed inode is replaced. The tests cover raw-buffer reasons, malformed-reply ordering, layout shape, multi-key `ShortcutStatus`, argument handling and the capture state machine. `tests/check-scripts.sh` runs `bash -n` and shellcheck; `tests/script-helpers-test.sh` tests per-key ownership, fixed-string matching, a recorded one-line qdbus array and the same format with several owners and quoted display names, recorded `shortcutKeys` replies (single, several, multi-key sequences, empty), whole-sequence key removal, failed or unreadable key queries (which stop the script instead of reading as no keys), that every sequence sent to KGlobalAccel is exactly `4` and four int32 chords and bad input never reaches `busctl`, the exact `busctl` arguments, adding Print to the daemon's keys, fresh-profile detection, exact fresh rollback including cleared and multi-key bindings, the handover decisions for every marker × launcher × Spectacle-Print combination and stock Spectacle Meta+Shift+S, the reset and Print-add decisions per daemon status, removing both of Spectacle's stock defaults, the recovery mode, the lock (which the Spectacle helper does not inherit), the shared exit handler, the re-login note, unit-state classification, launcher matching and multi-key status parsing.

```sh
b=~/.cache/agent-scratch/desktop-config/build
cmake -S "$PWD" -B "$b" && heavy cmake --build "$b" && ctest --test-dir "$b" --output-on-failure
```

The stack harness uses a private D-Bus session, isolated XDG state, two virtual KWin outputs and an authorised fake-input helper; it never sends keys to the live display. Its test-only preload helper masks live physical modifier state and can hold a disk sync to check the busy wheel during saving. Build `stack-probe`, `stack-input` and `stack-klipper` (the last needs Qt QML development files), then run `heavy python3 tests/stack-e2e.py /absolute/build/path` (Python Pillow produces the zoomed wheel captures). `tests/stack-live.py` is the separately guarded live modifier check, used only with its own focused editor and an unlocked screen. The test-only fake-input XML is unchanged from [plasma-wayland-protocols 1.20.0](https://github.com/KDE/plasma-wayland-protocols/blob/v1.20.0/src/protocols/fake-input.xml).

Live harness scenarios are under `measurements/harness/`; they inject keys and clean up their own uinput, clipboard-probe and KWin-script resources. Print now uses the overlay, so the Print checks exercise the same daemon path as Meta+Shift+S.

Backups use home-relative paths. `backup-2026-10-18-spectacle-fast/source/` holds the retired Spectacle-based service and `backup-2026-10-16/` holds the originals. Backup directories are immutable archives and are not edited.

## Measurements

2026-10-18, Plasma/KWin 6.6.6 Wayland, Qt 6.10.2, RTX 4090, scale 1: HDMI-A-5 2560x1440 at 100 Hz, DP-5 3440x1440 at 175 Hz, DP-4 1920x1080 at 240 Hz. Real Meta+Shift+S presses were injected through uinput and handled by KGlobalAccel. Cells show the median and, in brackets, the range.

| Stage | Original (kioclient + Spectacle, 2026-10-16) | Spectacle fast version | Overlay, qdbus6 launcher + pipe (3 presses) | **Overlay, own shortcut + memfd (6 presses)** |
| --- | ---: | ---: | ---: | ---: |
| Key → daemon | — | — | ~30 ms; seen at ~220 ms and ~1 s | **1 ms** (1–1) |
| Capture request → frozen image | — | — | 79 ms (78–81) | **57 ms** (55–67) |
| Press → first overlay mapped | 1,056 ms | 335 ms | 121 ms (112–127) | **78 ms** (68–92) |
| Press → all three overlays mapped | 1,152 ms | 512 ms | 127 ms (121–128) | **79 ms** (69–93) |
| Drag frame interval on DP-5, median / p95 | 15.9 / 43.2 ms | 6.4 / 11.2 ms (2026-10-16) | 4 / 5 ms | **4–5 / 5 ms**, magnifier on |
| Release → image decoded by a native Wayland app | 187 ms | 22 ms | 9 ms (8–10) | **11 ms** (8–40) |

After the shortcut and install simplification and the non-blocking capture wait (5 real presses, `simplified-measure-2026-10-18.jsonl`): key → daemon 1 ms (0–1), capture request → frozen image 54 ms (52–58), press → first overlay 64 ms (62–81), press → all three overlays 69 ms (65–81), release → decoded 11 ms (9–18), so polling from the event loop cost nothing measurable.

The 40 ms release outlier is the first copy after a daemon restart; the next ones took 8–15 ms. The key path no longer goes through KGlobalAccel's desktop-file launch (a `qdbus6` process per press), which caused the old ~30 ms and the ~1 s outliers. In an earlier session, D-Bus activation from a stopped service added about 180 ms to the first press. That applies only to the D-Bus method now.

### Capture experiments

KWin 6.6.6's ScreenShot2 renders the requested area into an offscreen texture on its main thread, reads it back with `glReadnPixels`, swizzles and flips it on the CPU and only then replies. A thread-pool worker then writes the pixels to the client's fd (non-blocking, `poll` + `write`). The only options are `include-cursor`, `native-resolution`, `include-decoration`/`include-shadow` (windows only) and `hide-caller-windows`. Any fd type is accepted. A standalone benchmark (`harness/capture-bench.cpp`, interleaved rounds, ms from request; raw output in `capture-bench-2026-10-18.txt`):

| Variant (trials) | KWin reply | All pixels readable |
| --- | ---: | ---: |
| `CaptureWorkspace`, default 64 KiB pipe (6) | 45 (43–47) | 85 (81–89) |
| `CaptureWorkspace`, pipe raised to 1 MiB (6) | 45 (44–50) | 82 (76–87) |
| **`CaptureWorkspace`, memfd mapped when full (6)** | 46 (44–49) | **60 (56–65)** |
| same, `hide-caller-windows=false` (3) | 48 (48–48) | 61 (60–67) |
| 3 × `CaptureScreen` in parallel, pipe (6) | first 17, last ~50 | first 32 (31–33), all 60 (57–65) |
| 3 × `CaptureScreen` in parallel, memfd (6) | first 17, last ~49 | first 23 (22–25), all 53 (50–55) |

The memfd was adopted. Parallel per-screen captures were also built into the daemon, mapping each monitor's overlay as soon as its image arrived, and measured with 5 real presses each in the same session (`experiment-*-2026-10-18.jsonl`). The first overlay came 10 ms sooner (62 ms median, 58–68, against 72, 66–85), but all overlays came no sooner (74 ms, 69–82, against 72, 67–88). KWin captures each screen synchronously on its main thread, so it can't map the first overlay until it has finished the other captures. The monitors also froze up to ~30 ms apart and appeared one after another, so that mode was dropped. Not tried: anything needing KWin changes. Most of the remaining ~45 ms is KWin's own render, readback and CPU conversion, and ~15 ms is from capture to KWin mapping the overlays.

### Method and earlier verification

A KWin script logged `workspace.windowAdded` times. The drag was 100 pointer moves over 500 ms with Shift held. Overlay drag figures are the daemon's paint intervals on the monitor under the pointer. The earlier Spectacle figures were `wl_surface.commit` intervals, so compare that row only roughly. The reader was a resident Qt client using `KSystemClipboard` that requested and decoded `image/png` as soon as the clipboard changed. The test pointer included a middle button: without one, libinput's middle-button emulation holds every press for 50 ms. `harness/analyze.py` turns a run into per-press numbers. Raw data: `measurements/*-2026-10-18.jsonl`, `results-2026-10-18.json` (pipe version) and `results-shortcut-2026-10-18.json` (the own-shortcut version) and `simplified-*-2026-10-18.jsonl` (this version). Scripts: `measurements/harness/`. Earlier data: `results.json` (2026-10-16) and `prototype-2026-10-17.md`.

Verified on the live desktop after the shortcut and memfd change (2026-10-18): the real shortcut after `systemctl --user restart` and after stop + start; click-only, a second press while open, Escape (clipboard unchanged) and a cross-monitor drag; the magnifier over text, with dimming measured at 0.60–0.61 brightness on all three monitors; Print → Spectacle → clipboard; pasting in native Wayland and in a focused XWayland Qt app; a registration conflict, which is logged and then fixed by `install.sh`; rollback to the Spectacle version, a real press and drag there, and reinstall. Verified after the review fixes (2026-10-18): the geometry test; real presses with the same 401x251 span dragged in all four directions (all 401x251; up/left drags were 2 px smaller before) and 1-px-inverted drags (401x2 and 2x401; empty before); a second instance with and without `--capture` while the service ran (the shortcut kept working; before, it went inactive); click-only, a second press while open, Escape and a cross-monitor drag; the magnifier with Shift held from the shortcut and no motion; Print → clipboard; decoding in a native Wayland client; `install.sh` keeping a binding changed to Meta+Shift+F12 and a disabled one (same PID), then a no-op rerun; rollback to the Spectacle version with a real press and drag, and reinstall from it, where the daemon's first registration found the launcher's key, was dropped and then succeeded after the launcher was retired. Verified after the second review's fixes (2026-10-18): both unit tests; a no-argument launch (usage, exit 2) with the service stopped and running, a manual `--daemon` while it ran (exit 3, no restart loop) and `--capture` with it stopped (D-Bus activation started the service, which opened the overlay) and running; `systemctl --user stop` (the component became inactive) and `restart` keeping the default and a changed binding; Escape, click-only, a second press while open and a cross-monitor drag decoded by a native Wayland client; Print → clipboard and Print click-without-drag (the selection reopened once in its own Spectacle unit, a second click ended it); `install.sh` forced to fail after the launcher was retired and after the daemon restarted (both times the launcher had Meta+Shift+S back, the daemon was disabled and unregistered, and a real press and drag went through Spectacle); `rollback.sh` forced to fail after restoring the service files, after stopping the daemon and after binding the launcher (each time the daemon had Meta+Shift+S again and nothing restored was left); the launcher's empty-binding reload; rollback with a launcher already in place, a real press there, then reinstall and a no-op rerun (same PID). Verified after the simplification (2026-10-18): the unit tests and script checks below; `install.sh` from the installed state (restart, `registered:Meta+Shift+S`) and a no-op rerun (same PID); real presses with drags decoded by a native Wayland client, Escape (clipboard unchanged), click-only, a second press while open, a cross-monitor drag; `secondlaunch` with the service running and stopped; with the earlier Print → Spectacle handoff design, since replaced, Print → clipboard and Print click-without-drag (reopened once, a second click ended it; the `kioclient` the daemon started had an empty `SigBlk`, the daemon `0x4003`); a binding changed to Meta+Shift+F12 and a cleared one, each kept by an `install.sh` rerun (same PID) and by `systemctl --user restart`; `rollback.sh` from the installed state and again from the rolled-back state; `rollback.sh --originals` (a real press and drag landed in the clipboard), a rerun, a switch from there to the Spectacle-based version (the Python scripts were removed) and `install.sh` from both; every `QUICK_REGION_TEST_FAIL` step of both scripts, each followed by exactly the printed command and a real press and drag that landed in the clipboard; an interrupted harness run, which left no uinput device, KWin script or probe behind. Verified for the current version, where the daemon handles Print itself (2026-10-18): the overlay appears in 51–66 ms; a second press while open closes it (`shortcut-cancel`); a drag copied 501x301 and a native Wayland client decoded it; holding Meta+Shift+S (~1.3 s) or Print (~2 s) keeps the overlay open until Escape. A Print check after a rollback counts only after logging out and back in, since kglobalacceld keeps the Spectacle desktop entry it loaded at login. Not tested live: a scale change, mixed scales, a hotplug that trips the capture-shape check, a layout change during a press (the capture state machine is unit-tested), the `layout` scenario after its restore fix, the current Print handover paths (`pending-print-add` and the `after-print-retire` and `after-spectacle-empty` fail steps), and `_launch=none` surviving a re-login. Verified earlier with the pipe version: moving DP-4 while the overlay was open, then putting it back. Monitor removal and addition were not tested live, so the user's windows on DP-4 wouldn't be moved. That path uses the same handling as the tested geometry change.

### Fresh-profile verification

2026-10-28: in an isolated user's virtual KWin 6.6.6 Wayland session, install without `heavy`, a no-op rerun (same daemon PID), rollback, repeated rollback and reinstall passed. Both defaults moved from Spectacle to the daemon, an extra Spectacle key was preserved, and rollback restored the recorded original list. A 201×141 logical selection copied 302×212 native pixels at 150% and 252×177 at 125%, with every pixel matching the test pattern. Both injected shortcuts also produced that clipboard image. Five fresh-install failure points and the rollback's `after-daemon-removed` failure recovered through the printed command. This checks a single virtual output; mixed scales and laptop hardware remain untested.

## Known issues

Clipboard stack pacing observes Wayland data requests and transfer completion, not an acknowledgement that a document has finished inserting the item. Apps that defer later format requests beyond the grace period, and apps that cannot accept some stacked formats, may stop or omit unsupported items. The stack remains available for retry. Arbitrary target applications and non-KDE compositors have not all been tested.

The final review before the first commit (2026-10-18) reported these low-severity issues. The remaining issues are listed below, keeping the original numbers. Fresh-profile key handover and the fixed persistence delay have been addressed.

2. `parse_key_owners` (`shortcut-lib.sh`) returns nothing when a `"` appears in an owner's component display name or action unique name, so `check_key_owner` lets that owner through. The tests only cover a quote in the action's display name.
4. `install.sh`'s precheck rejects `~/.local/share/applications/quick-region-overlay-proto.desktop` even when it matches the backup, so the protected removal later in the script never runs.
5. Neither script recognises the launcher in `backup-2026-10-18-overlay-qdbus/`. A machine still on that version gets "matches neither backup's launcher".
7. In memfd mode, `measurements/harness/capture-bench.cpp` busy-waits inside the D-Bus reply handler, so the per-screen memfd row under Capture experiments measures the bench's own serialisation. The design conclusion was cross-checked in the daemon.
8. `measurements/harness/run.py` cleans up only on an exception or Ctrl-C. SIGTERM, for example from `timeout`, leaves the clipboard probe and the KWin script behind, and in the `layout` scenario leaves DP-4 moved.
9. `spectacle_config_no_print` also matches the default-keys field, so it would misread a `_launch=<keys>,<defaults>,<name>` line. kglobalacceld currently writes only the keys for this group (`_launch=none`), so this isn't reachable today.

Not covered by tests: a stubbed end-to-end run of `install.sh` or `rollback.sh`, the live Print handover paths (`pending-print-add`, `after-spectacle-empty`), and the daemon's capture file-descriptor handling (`captureReplied`, `pollCapture`).

## License

Licensed under the [MIT License](../LICENSE).

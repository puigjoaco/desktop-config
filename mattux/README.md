# Mattux

Mattux is the desktop-config tray menu. Its Screenshot & clipboard section shows the clipboard stack count, pastes or clears the stack, configures clipboard history and edits both key bindings for each screenshot shortcut.

```sh
./screenshot/install.sh
./mattux/install.sh
```

The installer prints an apt command if dependencies are missing. It builds a Release binary, records installed files in `~/.local/state/mattux/manifest` and starts `mattux.service`. The user service follows `graphical-session.target` so it starts and stops with the desktop. Reruns preserve settings and refuse to overwrite edited installed files.

Click either mouse button on the icon to open its menu. Enabled uses `systemctl --user enable/disable --now quick-region-overlay.service`; its state survives logins. While disabled, Mattux makes no calls to the screenshot daemon. Quit exits Mattux for this session; it returns at the next login.

Shortcut dialogs use KDE's standard conflict check and Reassign behaviour. Reassign releases the conflicting key immediately, including if the dialog is later cancelled. Defaults fills the two key slots; OK saves them through KGlobalAccel. Each tweak adds its section at the call site in the Tray constructor.

Closing or hiding a dialog, choosing Defaults or OK, and quitting Mattux cancel any active key recording immediately.

```sh
./mattux/rollback.sh
```

Rollback removes only manifest-owned Mattux files, leaving screenshot shortcuts, the clipboard stack and settings intact. Focused checks: `cmake -S mattux -B mattux/build`, `heavy cmake --build mattux/build --parallel 2`, then `ctest --test-dir mattux/build --output-on-failure`.

Manual UI check (requires KWin, python3-dbus and python3-gi): build `shortcut-dialog-test`, then run `heavy python3 mattux/tests/isolated-ui.py /absolute/path/to/mattux/build`. It creates a private D-Bus session, virtual KWin and isolated XDG directories, uses a mock screenshot service and `systemctl`, and cleans up its processes. Key events stay inside the test windows. Evidence defaults to `~/.cache/agent-scratch/desktop-config/mattux-ui/`; set `MATTUX_TEST_OUT` to choose another directory. Never drive these checks with input injected into the live desktop.

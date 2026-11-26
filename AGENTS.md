# Agent rules for desktop-config

- When testing, never inject keyboard or pointer input (ydotool, uinput, evdev writes, fake-input) into the user's live desktop session, and never leave a window open there that grabs the keyboard. Installing or running the tweaks themselves, which use ydotool, is fine. `screenshot/tests/stack-live.py` injects live keys: run it only when the user asks and is at the desktop.
- Drive input-dependent UI tests in the isolated nested-KWin harness (`screenshot/tests/stack-e2e.py`). Other live checks are read-only (D-Bus queries, screenshots) or done by the user.
- Ctrl+Alt+F1–F12 switches the virtual terminal and blanks the desktop. If the screen goes blank, check `cat /sys/class/tty/tty0/active` and run `loginctl activate <session>`.

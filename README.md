# desktop-config

Desktop tweaks for KDE Plasma 6 on Wayland, developed on Kubuntu 26.04. Fork of [StantonMatt/desktop-config](https://github.com/StantonMatt/desktop-config). Each tweak lives in its own folder with its own installer and rollback.

| Tweak | What it does |
| --- | --- |
| [Screenshot](screenshot/) | Meta+Shift+S / Print freeze the screen and copy a dragged region. Clipboard stack: Ctrl+Alt+C copies and Ctrl+Alt+S screenshots onto a stack, Ctrl+Alt+V pastes it all (oldest first), Ctrl+Alt+X clears it. Ctrl+Alt can stay held throughout. |
| [Mattux](mattux/) | Tray icon with the stack count. Its menu pastes or clears the stack and sets the new-stack-after-paste option, clipboard history, every shortcut above, and an on/off switch. |
| [Portapapeles](clipboard-panel/) | Meta+V opens a clipboard history panel with text and image cards in a stable order. Choosing a card copies it; you paste with Ctrl+V. |
| [KWin rules](kwin/) | Keeps WhatsApp Companion in the tray only. |
| [Sunshine](sunshine/) | Sunshine streaming host with a login-screen service and a virtual display. |
| [PR checks](pr-checks/) | Automatic T3 Code reports for pull requests on public repos. |

## Install

```sh
sudo apt install git build-essential cmake pkg-config libwayland-dev libwayland-bin qt6-base-dev qt6-wayland libkf6guiaddons-dev liblayershellqtinterface-dev libkf6globalaccel-dev libkf6config-bin libkf6config-dev qdbus-qt6 libkf6service-bin kde-spectacle ydotool jq coreutils gawk sed grep perl util-linux systemd dbus-user-session libkf6statusnotifieritem-dev libkf6xmlgui-dev libkf6widgetsaddons-dev
sudo usermod -aG input "$USER"   # then log out and back in
systemctl --user enable --now ydotool.service
git clone https://github.com/puigjoaco/desktop-config.git
cd desktop-config
./screenshot/install.sh
./mattux/install.sh
```

The clipboard stack needs the `input` group (to see held keys and type copy/paste) and the ydotool user service. Each installer checks its requirements and prints the missing `apt` line before changing anything.

## Update

```sh
cd desktop-config && git pull
./screenshot/install.sh
./mattux/install.sh
```

## Uninstall

`./mattux/rollback.sh`, then `./screenshot/rollback.sh` (restores Spectacle's keys). Details are in each folder's README.

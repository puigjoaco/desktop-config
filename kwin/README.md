# KWin window rules

## WhatsApp Companion: tray only

`whatsapp-companion-tray-only.kwinrule` keeps [WhatsApp Companion](https://github.com/puigjoaco/whatsapp-companion) out of the task manager, so it shows only as the tray icon next to the clock, whether its window is open or closed. It matches windows whose class is exactly `whatsapp-companion` and forces **Skip taskbar**.

To install it, open **System Settings → Window Management → Window Rules**, click **Import…**, choose `whatsapp-companion-tray-only.kwinrule` and click **Apply**. To remove it, select the rule there and delete it.

Tested on Kubuntu 26.04 with KDE Plasma 6.6.7 on Wayland.

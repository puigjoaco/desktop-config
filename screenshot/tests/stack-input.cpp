// Isolated virtual-session test helper. Never point this at the user's Wayland display.
#include "fake-input-client-protocol.h"
#include <wayland-client.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <string>
int main(int argc, char **argv) {
    const char *displayName = getenv("WAYLAND_DISPLAY");
    if (!displayName || strncmp(displayName, "qro-stack-test", 14) != 0) return 2;
    auto *display = wl_display_connect(nullptr);
    if (!display) return 3;
    org_kde_kwin_fake_input *input = nullptr;
    auto *registry = wl_display_get_registry(display);
    const wl_registry_listener listener{
        [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
            if (strcmp(interface, "org_kde_kwin_fake_input") == 0)
                *static_cast<org_kde_kwin_fake_input **>(data) = static_cast<org_kde_kwin_fake_input *>(wl_registry_bind(registry, name, &org_kde_kwin_fake_input_interface, version < 5 ? version : 5));
        }, [](void *, wl_registry *, uint32_t) {}};
    wl_registry_add_listener(registry, &listener, &input);
    wl_display_roundtrip(display);
    if (!input) return 4;
    org_kde_kwin_fake_input_authenticate(input, "QRO isolated test", "Automated clipboard-stack test in virtual compositor");
    wl_display_roundtrip(display);
    if (argc > 1 && strcmp(argv[1], "--keepalive") == 0) {
        // Keep wl_seat's keyboard capability present between short commands. Removing the last virtual keyboard
        // invalidates Qt's keyboard/selection serial before its queued copy action can publish the clipboard.
        while (wl_display_dispatch(display) >= 0) {}
        wl_display_disconnect(display);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "drag") == 0) {
        org_kde_kwin_fake_input_pointer_motion_absolute(input, wl_fixed_from_int(atoi(argv[2])), wl_fixed_from_int(atoi(argv[3])));
        wl_display_roundtrip(display); usleep(30000);
        org_kde_kwin_fake_input_button(input, 272, 1);
        wl_display_roundtrip(display); usleep(30000);
        org_kde_kwin_fake_input_pointer_motion_absolute(input, wl_fixed_from_int(atoi(argv[4])), wl_fixed_from_int(atoi(argv[5])));
        wl_display_roundtrip(display); usleep(30000);
        org_kde_kwin_fake_input_button(input, 272, 0);
        wl_display_roundtrip(display); usleep(30000);
    } else if (argc > 1 && strcmp(argv[1], "wheel") == 0) {
        org_kde_kwin_fake_input_pointer_motion_absolute(input, wl_fixed_from_int(atoi(argv[2])), wl_fixed_from_int(atoi(argv[3])));
        org_kde_kwin_fake_input_axis(input, 0, wl_fixed_from_int(15));
        wl_display_roundtrip(display);
    } else if (argc > 1 && strcmp(argv[1], "mouse") == 0) {
        org_kde_kwin_fake_input_pointer_motion_absolute(input, wl_fixed_from_int(atoi(argv[2])), wl_fixed_from_int(atoi(argv[3])));
        if (argc > 4) org_kde_kwin_fake_input_button(input, 272, atoi(argv[4]));
        wl_display_roundtrip(display);
        usleep(30000);
    } else {
        for (int i = 1; i < argc; ++i) {
            unsigned key, pressed;
            if (sscanf(argv[i], "%u:%u", &key, &pressed) != 2) continue;
            org_kde_kwin_fake_input_keyboard_key(input, key, pressed);
        }
        // One ordered batch releases the shortcut modifiers before KWin dispatches the resulting D-Bus action.
        // These virtual keys are absent from /dev/input; physical held-modifier behavior is checked separately.
        wl_display_roundtrip(display);
    }
    org_kde_kwin_fake_input_destroy(input);
    wl_display_flush(display);
    wl_display_disconnect(display);
}

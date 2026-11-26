#!/bin/bash
# Install the resident overlay daemon as Meta+Shift+S and Print. The Spectacle menu entry remains stock.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=shortcut-lib.sh
source "$source_dir/shortcut-lib.sh"
scratch="$HOME/.cache/agent-scratch/desktop-config"
state_dir="$HOME/.local/state/quick-region-overlay"
state_file="$state_dir/handover"
mode_file="$state_dir/previous-mode"
manifest_file="$state_dir/manifest"
fresh_file="$state_dir/fresh-spectacle-keys"
recover=
work=
trap on_exit EXIT

# Check dependencies before creating state or changing the desktop. The same complete command is printed for a
# missing tool, header/CMake package or compiler; configure diagnostics remain visible.
apt_line='sudo apt install git build-essential cmake pkg-config libwayland-dev libwayland-bin qt6-base-dev qt6-wayland libkf6guiaddons-dev liblayershellqtinterface-dev libkf6globalaccel-dev libkf6config-bin qdbus-qt6 libkf6service-bin kde-spectacle ydotool jq coreutils gawk sed grep perl util-linux systemd dbus-user-session'
missing_dependency() { printf '%s\n%s\n' "$1" "$apt_line" >&2; exit 1; }
for tool in dpkg-query cmake c++ make qdbus6 busctl jq kreadconfig6 kwriteconfig6 kbuildsycoca6 spectacle systemctl systemd-run cmp perl flock \
    sha256sum awk sed install stat paste date cat mkdir mktemp cp mv rm dirname sleep grep ydotool wayland-scanner pkg-config; do
    command -v "$tool" >/dev/null || missing_dependency "Missing tool: $tool. Install dependencies with:"
done
id -nG | grep -qw input || missing_dependency "Clipboard shortcuts need membership in the input group (sudo usermod -aG input \"$USER\", then log out and back in)."
systemctl --user --quiet is-active ydotool.service || missing_dependency 'The ydotool user service must be active (systemctl --user enable --now ydotool.service).'
input_readable=0
for input_device in /dev/input/event*; do [[ ! -e $input_device || ! -r $input_device ]] || input_readable=1; done
[[ $input_readable == 1 ]] || missing_dependency 'No readable /dev/input/event* devices; clipboard shortcuts cannot check held modifiers.'
for package in qt6-base-dev qt6-wayland libkf6guiaddons-dev liblayershellqtinterface-dev libkf6globalaccel-dev; do
    [[ $(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null) == installed ]] ||
        missing_dependency "Missing dependency: $package. Install dependencies with:"
done
take_lock
validate_klipper_record
work=$(mkdir -p "$scratch" && mktemp -d "$scratch/overlay-install.XXXXXX")
cmake -S "$source_dir" -B "$work/cmake" -DCMAKE_BUILD_TYPE=Release >"$work/configure.log" 2>&1 || {
    cat "$work/configure.log" >&2
    missing_dependency "Could not configure the build. Install dependencies with:"
}
if command -v heavy >/dev/null; then
    heavy cmake --build "$work/cmake" --parallel 2 --target quick-region-overlay
else
    cmake --build "$work/cmake" --parallel 2 --target quick-region-overlay
fi

# 1. Prechecks. Every file that a forward-only step can delete is compared with an immutable backup first.
previous=$(launcher_mode)
handover_state=
[[ -f "$state_file" ]] && handover_state=$(cat -- "$state_file")
valid_marker "$handover_state" ||
    fail "The handover state marker is invalid; remove $state_file after checking the desktop state. Nothing was changed."
recorded_mode=
if marker_pending "$handover_state" && [[ -f $mode_file ]]; then
    recorded_mode=$(cat -- "$mode_file")
    valid_mode "$recorded_mode" || fail "$mode_file is invalid; remove it after checking the desktop state. Nothing was changed."
fi
rollback_mode=$(recovery_mode "$previous" "$handover_state" "$recorded_mode")
fresh=0
record_fresh=0
if [[ -e $fresh_file || -L $fresh_file ]]; then
    load_fresh_keys
    fresh=1
fi
if fresh_install_needed "$previous" "$handover_state"; then
    fresh=1
    record_fresh=1
fi
if [[ $fresh == 0 ]]; then check_backup "$rollback_mode"; fi
[[ -f "$spectacle_fast_backup/$spectacle_override_relative" ]] || fail "The Spectacle override backup is missing; nothing was changed."
check_override
check_key_owner "$meta_shift_s_code" "$daemon_owner" "$launcher_owner" org.kde.spectacle.desktop/_launch
check_key_owner "$print_code" "$daemon_owner" org.kde.spectacle.desktop/_launch
for i in "${!stack_actions[@]}"; do
    check_key_owner "${stack_codes[i]}" "quick-region-overlay/${stack_actions[i]}"
done
for relative in \
    "$launcher_relative" .config/systemd/user/quick-region-clipboard.service .local/bin/quick-region-screenshot \
    .local/libexec/quick-region-clipboard-service .local/libexec/quick-region-clipboard-service.py .local/libexec/quick-region-clipboard.py; do
    check_owned_file "$HOME/$relative" "$relative"
done
[[ ! -e "$HOME/.local/share/applications/quick-region-overlay-proto.desktop" ]] ||
    fail "$HOME/.local/share/applications/quick-region-overlay-proto.desktop is not an owned old-pipeline file; nothing was changed."

spectacle_meta=0
checked action_has_key org.kde.spectacle.desktop _launch "$meta_shift_s_code" && spectacle_meta=1
spectacle_print=0
checked spectacle_owns_print && spectacle_print=1
launcher_present=0
[[ -n $previous ]] && launcher_present=1
handover=$(handover_kind "$handover_state" "$launcher_present" "$spectacle_print" "$spectacle_meta")

# 2. Record fresh Spectacle keys before the first desktop change. Archives in the repository are immutable.
if [[ $record_fresh == 1 ]]; then
    action_keys org.kde.spectacle.desktop _launch >"$fresh_file.tmp" || fail "Could not record Spectacle's original keys. Nothing was changed."
    mv -- "$fresh_file.tmp" "$fresh_file"
fi
recover=$(rollback_command "$rollback_mode")
install_klipper_settings
# A recorded mode outlives its handover only after an interruption between the two writes that complete it.
marker_pending "$handover_state" || rm -f -- "$mode_file"
changed=0
install_if_changed() {
    local mode=$1 source=$2 target=$3 relative=${3#"$HOME/"}
    if [[ -L $target ]] || ! cmp -s -- "$source" "$target"; then
        protect_managed_file "$relative"
        install -Dm"$mode" -- "$source" "$target"
        changed=1
    fi
    record_managed_file "$relative"
}
sed "s|@HOME@|$HOME|g" -- "$source_dir/quick-region-overlay.desktop" >"$work/quick-region-overlay.desktop"
install_if_changed 644 "$source_dir/quick-region-overlay.service" "$HOME/.config/systemd/user/quick-region-overlay.service"
install_if_changed 644 "$source_dir/org.local.QuickRegionOverlay.service" "$HOME/.local/share/dbus-1/services/org.local.QuickRegionOverlay.service"
install_if_changed 644 "$work/quick-region-overlay.desktop" "$HOME/.local/share/applications/quick-region-overlay.desktop"
protect_managed_file .local/share/applications/quick-region-overlay-proto.desktop
rm -f -- "$HOME/.local/share/applications/quick-region-overlay-proto.desktop"
rebuild_cache
systemctl --user daemon-reload
systemctl --user --quiet enable quick-region-overlay.service
# The binary goes last and the daemon restarts right after it. KWin authorizes ScreenShot2 by the caller's executable
# path, which reads "(deleted)" once the binary is replaced, so captures fail until the restart. A normal rerun keeps all
# saved keys; a failed or crash-looping unit is restarted too.
install_if_changed 755 "$work/cmake/quick-region-overlay" "$HOME/.local/libexec/quick-region-overlay"
if [[ $changed == 1 ]] || ! daemon_running || ! daemon_executable_current; then restart_daemon; fi
fail_point after-files

# 3. Remove the private Spectacle override. A stock file is now selected by the desktop cache.
if [[ -e "$spectacle_override_entry" ]]; then
    check_override
    note_relogin
    rm -f -- "$spectacle_override_entry"
    rebuild_cache
fi

# 4. Check the daemon, then hand over the keys its old owners still hold. Taking Meta+Shift+S from the launcher or
# Spectacle resets the action once so both defaults are acquired; Print taken from Spectacle is added to the action's current keys. See handover_kind.
status=$(wait_daemon_status) || fail 'The daemon did not answer; see journalctl --user -u quick-region-overlay.service'
[[ $status != error:* ]] || fail "The daemon could not read its shortcut from KGlobalAccel ($status)."
stack_status=$(qdbus6 org.local.QuickRegionOverlay / org.local.QuickRegionOverlay.StackShortcutStatus) || fail 'The daemon did not report its clipboard stack shortcuts.'
for stack_action in "${stack_actions[@]}"; do
    grep -q "^$stack_action=" <<<"$stack_status" || fail "Missing clipboard stack action: $stack_action"
done
[[ $stack_status != *error:* ]] || fail "Could not read clipboard stack shortcuts: $stack_status"
fail_point after-daemon-start

if [[ -n $handover ]]; then
    printf '%s\n' "$handover" >"$state_file"
    if [[ -n $previous ]]; then
        # Recorded before the launcher goes, so a rerun after a failure still prints the right rollback command.
        printf '%s\n' "$previous" >"$mode_file"
        retire_launcher
        fail_point after-launcher-retire
    fi
    # Stock Spectacle may still have saved keys from before the override was removed. Remove only Print and
    # Meta+Shift+S without unregistering the action; an explicit none value or the remaining keys survives a later login
    # and ksycoca rescan.
    if checked action_has_any_key org.kde.spectacle.desktop _launch; then
        ensure_spectacle_action_print_removed
        fail_point after-print-retire
    fi
    if handover_needs_reset "$handover" "$status"; then
        systemctl --user stop quick-region-overlay.service
        unregister_shortcut quick-region-overlay capture-region
        restart_daemon
        status=$(wait_daemon_status) || fail 'The daemon did not answer after the handover; see journalctl --user -u quick-region-overlay.service'
    fi
    if handover_needs_print "$handover" "$status"; then
        add_daemon_print
        wait_for 30 daemon_has_print || fail 'The daemon did not get Print after Spectacle gave it up.'
        status=$(wait_daemon_status) || fail 'The daemon did not answer after the handover; see journalctl --user -u quick-region-overlay.service'
    fi
    handover_keys_present "$status" "$handover" || fail "The daemon did not get the required shortcuts after the handover ($status)."
    fail_point after-handover
    printf 'complete\n' >"$state_file"
    rm -f -- "$mode_file"
fi

# Older installs may have already completed the handover after unregistering Spectacle, leaving no action to update. Ensure
# that the action is present with Print removed even when there is no pending marker; this is a no-op once that state has
# been persisted and the daemon already owns the intended shortcuts.
if ! spectacle_config_no_print || ! spectacle_action_present || checked spectacle_owns_print; then
    ensure_spectacle_action_print_removed
    fail_point after-spectacle-empty
fi

# 5. Retire the old clipboard service and its files. The backup remains the rollback source.
if state_needs_stop "$(unit_state quick-region-clipboard.service)"; then systemctl --user stop quick-region-clipboard.service; fi
if [[ -e "$HOME/.config/systemd/user/quick-region-clipboard.service" ]]; then systemctl --user --quiet disable quick-region-clipboard.service; fi
rm -f -- "$HOME/.config/systemd/user/quick-region-clipboard.service" \
    "$HOME/.local/libexec/quick-region-clipboard-service" "$HOME/.local/libexec/quick-region-clipboard-service.py" \
    "$HOME/.local/libexec/quick-region-clipboard.py" "$HOME/.local/bin/quick-region-screenshot"
systemctl --user daemon-reload
systemctl --user reset-failed quick-region-clipboard.service 2>/dev/null || true
fail_point after-clipboard-service
recover=

case $status in
    registered:*) echo "Installed Quick Region Overlay (daemon PID $(systemctl --user show -P MainPID quick-region-overlay.service)), shortcuts ${status#registered:}." ;;
    none:*) echo "Installed Quick Region Overlay. Its shortcuts have no keys, and ${status#none:} owns a default; set them in System Settings → Shortcuts → Quick Region Overlay." ;;
    *) echo "Installed Quick Region Overlay. Its shortcuts have no keys (cleared in System Settings); set them in System Settings → Shortcuts → Quick Region Overlay." ;;
esac
printf '%s\n' "$stack_status"
print_relogin_note "The Spectacle override was removed. Log out and back in so Spectacle's other shortcuts, such as Meta+Shift+Print, stop using it."

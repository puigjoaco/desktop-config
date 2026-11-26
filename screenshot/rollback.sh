#!/bin/bash
# Remove a fresh-installed overlay daemon, or restore a backed-up legacy Spectacle pipeline. Other shortcuts and Spectacle settings are untouched.
#   rollback.sh              Spectacle-based fast version (resident service + idle Spectacle), 2026-10-18 backup
#   rollback.sh --originals  original kioclient/Spectacle + Python clipboard scripts, 2026-10-16 backup
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=shortcut-lib.sh
source "$source_dir/shortcut-lib.sh"
if [[ "${1:-}" == "--originals" && $# -eq 1 ]]; then mode=originals
elif [[ $# -eq 0 ]]; then mode=spectacle-fast
else echo "usage: $0 [--originals]" >&2; exit 2; fi
backup=$(backup_dir "$mode")
state_dir="$HOME/.local/state/quick-region-overlay"
state_file="$state_dir/handover"
mode_file="$state_dir/previous-mode"
manifest_file="$state_dir/manifest"
fresh_file="$state_dir/fresh-spectacle-keys"
recover=
trap on_exit EXIT

# 1. Prechecks. Restores and removals are allowed only for files matching an immutable backup.
need_tools qdbus6 busctl jq kreadconfig6 kwriteconfig6 kbuildsycoca6 spectacle systemctl systemd-run cmp perl flock sha256sum awk sed install stat paste date cat mkdir cp mv rm dirname sleep grep
take_lock
validate_klipper_record
fresh=0
if [[ -e $fresh_file || -L $fresh_file ]]; then
    [[ $# == 0 ]] || fail 'A fresh install rolls back to stock Spectacle; use ./rollback.sh without --originals.'
    load_fresh_keys
    fresh=1
else
    check_backup "$mode"
fi
check_key_owner "$meta_shift_s_code" "$daemon_owner" "$launcher_owner" org.kde.spectacle.desktop/_launch
check_key_owner "$print_code" "$daemon_owner" org.kde.spectacle.desktop/_launch
if [[ $fresh == 0 ]]; then
    launcher_mode >/dev/null
    if [[ -e "$spectacle_override_entry" ]]; then
        cmp -s -- "$backup/$spectacle_override_relative" "$spectacle_override_entry" ||
            fail "$spectacle_override_entry was edited; nothing was changed."
    fi
    while read -r relative; do
        [[ $relative == "$launcher_relative" ]] && continue
        check_owned_file "$HOME/$relative" "$relative"
    done < <(backup_files "$mode")
    for relative in .local/libexec/quick-region-clipboard-service .local/libexec/quick-region-clipboard-service.py .local/libexec/quick-region-clipboard.py; do
        check_owned_file "$HOME/$relative" "$relative"
    done
fi
recover=$(rollback_command "$mode")

if [[ $fresh == 0 ]]; then
    # 2. Restore the selected pipeline files and the Spectacle override. The cache is rebuilt before any shortcut handover.
    while read -r relative; do
        [[ $relative == "$launcher_relative" ]] && continue
        install -Dm"$(stat -c %a -- "$backup/$relative")" -- "$backup/$relative" "$HOME/$relative"
    done < <(backup_files "$mode")
    if [[ $mode == originals ]]; then
        rm -f -- "$HOME/.local/libexec/quick-region-clipboard-service"
    else
        rm -f -- "$HOME/.local/libexec/quick-region-clipboard-service.py" "$HOME/.local/libexec/quick-region-clipboard.py"
    fi
    [[ -e "$spectacle_override_entry" ]] || note_relogin
    install -Dm644 -- "$backup/$spectacle_override_relative" "$spectacle_override_entry"
    rebuild_cache
    fail_point after-service-files
fi

# 3. Remove the overlay daemon. Any state but inactive or failed is stopped, which also catches a unit that is
# activating or auto-restarting; a failed state is cleared so the removed unit doesn't linger.
# The D-Bus service goes first so nothing can activate the daemon again; the unit is disabled before an edited copy is
# moved aside, so it never leaves its enablement link behind.
protect_managed_file .local/share/dbus-1/services/org.local.QuickRegionOverlay.service
rm -f -- "$HOME/.local/share/dbus-1/services/org.local.QuickRegionOverlay.service"
if [[ -e "$HOME/.config/systemd/user/quick-region-overlay.service" ]]; then systemctl --user --quiet disable quick-region-overlay.service; fi
if daemon_needs_stop; then systemctl --user stop quick-region-overlay.service; fi
systemctl --user reset-failed quick-region-overlay.service 2>/dev/null || true
for relative in .config/systemd/user/quick-region-overlay.service .local/share/applications/quick-region-overlay.desktop \
    .local/libexec/quick-region-overlay; do
    protect_managed_file "$relative"
done
rm -f -- "$HOME/.config/systemd/user/quick-region-overlay.service" \
    "$HOME/.local/share/applications/quick-region-overlay.desktop" "$HOME/.local/libexec/quick-region-overlay"
systemctl --user daemon-reload
unregister_shortcut quick-region-overlay capture-region
for stack_action in "${stack_actions[@]}"; do unregister_shortcut quick-region-overlay "$stack_action"; done
fail_point after-daemon-removed

if [[ $fresh == 1 ]]; then
    restore_fresh_keys
    rebuild_cache
    wait_for 30 fresh_keys_restored || fail "Spectacle's original keys changed after rebuilding the cache."
    restore_klipper_settings
    rm -f -- "$state_file" "$mode_file" "$manifest_file"
    recover=
    echo "Removed Quick Region Overlay; restored Spectacle's original shortcuts."
    exit 0
fi

# 4. Restore the old launcher and its Meta+Shift+S binding. Set Spectacle's _launch to its existing keys minus
# Meta+Shift+S, plus Print; this makes rollback deterministic while preserving the user's other Spectacle keys.
systemctl --user --quiet disable quick-region-clipboard.service
systemctl --user --quiet add-wants graphical-session.target quick-region-clipboard.service
# Stop any idle Spectacle first, then register the desktop action briefly so both backup modes can persist Print. The
# temporary process is terminated before rollback returns; step 5 starts the selected resident service.
systemctl --user stop quick-region-clipboard.service 2>/dev/null || true
start_spectacle_action
spectacle_keys=()
load_keys_without spectacle_keys org.kde.spectacle.desktop _launch "$meta_shift_s_code" "$print_code"
spectacle_keys+=("$(single_key_sequence "$print_code")")
set_action_key org.kde.spectacle.desktop _launch "${spectacle_keys[@]}" || fail "Could not set Spectacle's _launch keys."
wait_for 30 checked spectacle_owns_print || fail "Print did not go to Spectacle's _launch after rollback."
stop_spectacle_action
systemctl --user stop quick-region-clipboard.service
if [[ -e "$launcher_entry" ]] && ! cmp -s -- "$backup/$launcher_relative" "$launcher_entry"; then retire_launcher; fi
install -Dm644 -- "$backup/$launcher_relative" "$launcher_entry"
rebuild_cache
start_spectacle_action
set_action_key org.kde.spectacle.desktop _launch "${spectacle_keys[@]}" || fail "Could not set Spectacle's _launch keys."
wait_for 30 checked spectacle_owns_print || fail "Print did not go to Spectacle's _launch after the cache rebuild."
stop_spectacle_action
if ! wait_for 30 checked launcher_owns_key; then
    retire_launcher
    install -Dm644 -- "$backup/$launcher_relative" "$launcher_entry"
    rebuild_cache
    wait_for 30 checked launcher_owns_key || fail "Meta+Shift+S did not go to the restored launcher (bound to: $(key_owners "$meta_shift_s_code" | paste -sd ' '))."
fi
fail_point after-launcher

# 5. Start the selected clipboard service with the graphical session.
systemctl --user --quiet disable quick-region-clipboard.service
systemctl --user --quiet add-wants graphical-session.target quick-region-clipboard.service
systemctl --user reset-failed quick-region-clipboard.service 2>/dev/null || true
systemctl --user restart quick-region-clipboard.service
if [[ $(systemctl --user show -P Type quick-region-clipboard.service) == simple ]]; then
    sleep 2
    systemctl --user --quiet is-active quick-region-clipboard.service && [[ $(systemctl --user show -P NRestarts quick-region-clipboard.service) == 0 ]] ||
        fail 'quick-region-clipboard.service did not stay up; see journalctl --user -u quick-region-clipboard.service'
fi
fail_point after-clipboard-service
restore_klipper_settings
rm -f -- "$state_file" "$mode_file" "$manifest_file"
recover=
echo "Restored the $mode screenshot pipeline; Meta+Shift+S is on the launcher and Print is on Spectacle."
print_relogin_note "The Spectacle override was restored. Log out and back in so Print and Spectacle's other shortcuts use it; until then they may open Spectacle's window."

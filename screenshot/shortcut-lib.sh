# Shared by install.sh and rollback.sh, which source it with set -euo pipefail. Both scripts only move forward and are
# safe to rerun: a failure stops with the exact command to run, nothing undoes itself.
# shellcheck shell=bash disable=SC2034,SC2154 # callers set source_dir
shopt -s inherit_errexit
meta_shift_s_code=301989971 # Meta+Shift+S
print_code=16777225 # Print (Qt::Key_Print)
stack_actions=(stack-copy stack-capture stack-paste stack-clear)
stack_codes=(201326659 201326675 201326678 201326680) # Ctrl+Alt+C/S/V/X
launcher_entry="$HOME/.local/share/applications/quick-region-screenshot-v3.desktop"
launcher_relative=.local/share/applications/quick-region-screenshot-v3.desktop
launcher_owner=quick-region-screenshot-v3.desktop/_launch
daemon_owner=quick-region-overlay/capture-region
spectacle_fast_backup="$source_dir/backup-2026-10-18-spectacle-fast"
originals_backup="$source_dir/backup-2026-10-16"

# Test hook for the failure paths: QUICK_REGION_TEST_FAIL=<step> makes the script fail at that step. Unset, it does
# nothing. Steps: install.sh after-files, after-daemon-start, after-launcher-retire, after-print-retire,
# after-spectacle-empty, after-handover, after-clipboard-service; rollback.sh after-service-files, after-daemon-removed,
# after-launcher, after-clipboard-service.
fail_point() {
    if [[ "${QUICK_REGION_TEST_FAIL:-}" == "$1" ]]; then
        echo "Test failure injected at $1 (QUICK_REGION_TEST_FAIL)." >&2
        exit 1
    fi
}
fail() { echo "$*" >&2; exit 1; }
need_tools() { local tool; for tool; do command -v "$tool" >/dev/null || fail "Missing tool: $tool"; done; }

# Keep an atomic per-install record, including absent keys. The active flag prevents a rerun (or interrupted
# install) from backing up our own settings. A completed rollback permits a new install to record current values.
validate_klipper_record() {
    local record="${state_dir:?}/klipper-settings.json"
    [[ -e $record || -L $record ]] || return 0
    if [[ ! -f $record || -L $record ]]; then fail "Invalid Klipper settings record: $record. Nothing was changed."; fi
    jq -e '
        type == "object" and (.active | type == "boolean") and
        (.values | type == "object" and keys == ["IgnoreImages", "MaxClipItems"] and
         all(.[]; . == null or type == "string"))
    ' "$record" >/dev/null || fail "Invalid Klipper settings record: $record. Nothing was changed."
}
klipper_value_json() {
    local key=$1 value alternate file="${XDG_CONFIG_HOME:-$HOME/.config}/klipperrc"
    # An absolute filename reads only the user's file, so an inherited system value remains an absent user key.
    value=$(kreadconfig6 --file "$file" --group General --key "$key" --default __qro_absent_1__) || return 1
    if [[ $value == __qro_absent_1__ ]]; then
        alternate=$(kreadconfig6 --file "$file" --group General --key "$key" --default __qro_absent_2__) || return 1
        if [[ $alternate == __qro_absent_2__ ]]; then printf 'null\n'; return 0; fi
    fi
    jq -cn --arg value "$value" '$value'
}
reload_klipper_settings() {
    local owner
    owner=$(busctl --user --timeout=3 call org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus NameHasOwner s org.kde.klipper) || fail 'Could not check whether Klipper is running.'
    case $owner in
        'b true')
            busctl --user --timeout=3 --auto-start=no call org.kde.klipper /klipper org.kde.klipper.klipper reloadConfig ||
                fail 'Could not reload Klipper settings; no desktop processes were restarted.' ;;
        'b false') echo 'Klipper is not running; clipboard history settings apply when it starts.' ;;
        *) fail "Unexpected Klipper owner reply: $owner" ;;
    esac
}
install_klipper_settings() {
    local record="${state_dir:?}/klipper-settings.json" count images current
    validate_klipper_record
    if [[ ! -f $record ]] || [[ $(jq -r '.active' "$record") == false ]]; then
        count=$(klipper_value_json MaxClipItems) || fail 'Could not read Klipper history length.'
        images=$(klipper_value_json IgnoreImages) || fail 'Could not read Klipper image setting.'
        (umask 077; jq -cn --argjson count "$count" --argjson images "$images" \
            '{active: true, values: {MaxClipItems: $count, IgnoreImages: $images}}' >"$record.tmp" &&
            mv -- "$record.tmp" "$record") || fail 'Could not record previous Klipper settings.'
    fi
    # Raise the effective history length to at least 100, never lower it: shrinking discards older entries.
    current=$(kreadconfig6 --file klipperrc --group General --key MaxClipItems --default 0) ||
        fail 'Could not read Klipper history length.'
    if [[ ! $current =~ ^[0-9]{1,9}$ ]] || ((10#$current < 100)); then
        kwriteconfig6 --file klipperrc --group General --key MaxClipItems 100
    fi
    kwriteconfig6 --file klipperrc --group General --key IgnoreImages --type bool false
    reload_klipper_settings
}
restore_klipper_settings() {
    local record="${state_dir:?}/klipper-settings.json" key value
    validate_klipper_record
    [[ -f $record ]] || return 0 # Installs predating the clipboard-history configuration have no record.
    [[ $(jq -r '.active' "$record") == true ]] || return 0 # Already restored; later user changes stay.
    for key in MaxClipItems IgnoreImages; do
        if [[ $(jq -r --arg key "$key" '.values[$key] == null' "$record") == true ]]; then
            kwriteconfig6 --file klipperrc --group General --key "$key" --delete
        else
            value=$(jq -r --arg key "$key" '.values[$key]' "$record")
            kwriteconfig6 --file klipperrc --group General --key "$key" "$value"
        fi
    done
    reload_klipper_settings
    (umask 077; jq '.active = false' "$record" >"$record.tmp" && mv -- "$record.tmp" "$record") ||
        fail 'Could not finish restoring Klipper settings.'
}

# Holds $state_dir/lock until the script exits, so install.sh and rollback.sh never run at the same time.
take_lock() {
    mkdir -p -- "${state_dir:?}"
    exec {lock_fd}>"$state_dir/lock"
    flock -n "$lock_fd" || fail "Another install.sh or rollback.sh is running (it holds $state_dir/lock). Let it finish, then run this again. Nothing was changed."
}
# The EXIT trap of both scripts: kills a Spectacle helper this run started, removes the run's work directory and, after
# a failure past the first change, prints the recovery command.
on_exit() {
    local status=$?
    stop_spectacle_action
    [[ -z ${work:-} ]] || rm -rf -- "$work"
    if [[ $status != 0 && -n ${recover:-} ]]; then echo "${0##*/} stopped partway. To recover, run: $recover" >&2; fi
}
# Runs a key predicate that answers 0 (yes), 1 (no) or 2 (KGlobalAccel could not be asked) and stops the script on 2,
# so a failed query is never read as "no".
checked() {
    local status=0
    "$@" || status=$?
    ((status != 2)) || fail "Could not read shortcut keys from KGlobalAccel ($*). Run this again once KGlobalAccel answers."
    return "$status"
}

# Polls a command every 0.1 s, up to tries times, until it succeeds.
wait_for() {
    local tries=$1 try
    shift
    for ((try = 0; try < tries; try++)); do "$@" && return 0; sleep 0.1; done
    return 1
}

# kbuildsycoca6 must run with the systemd user manager's desktop environment; a terminal's or AppImage's XDG paths
# and locale would rebuild a different cache.
rebuild_cache() { systemd-run --user --wait --pipe --collect --quiet -- kbuildsycoca6 --noincremental; }
unregister_shortcut() { qdbus6 org.kde.kglobalaccel /kglobalaccel org.kde.KGlobalAccel.unregister "$1" "$2" >/dev/null; }
# Every component/action bound to a key, one per line, including a stopped daemon's registration. qdbus --literal does
# not escape quotes in display names; the tuple shape and the three fields before the arrays let the parser retain such
# names while extracting the action and component. The fourth field onward is deliberately constrained to three strings
# so the greedy display-name match cannot consume the component. qdbus prints the whole array on one line, so the line
# is split at each tuple first and every tuple is matched on its own.
key_owners() {
    local code=${1:?key code required} literal
    literal=$(qdbus6 --literal org.kde.kglobalaccel /kglobalaccel org.kde.KGlobalAccel.getGlobalShortcutsByKey "$code") || return 1
    parse_key_owners "$code" <<<"$literal"
}
parse_key_owners() {
    local code=${1:?key code required}
    perl -s -ne 'for my $tuple (split /\[Argument: \(ssssssaiai\) /) { if ($tuple =~ /^"([^"]*)", "(.*)", "([^"]*)", "[^"]*", "[^"]*", "[^"]*", \[Argument: ai \{([^}]*)\}\]/) { $keys=$4; print "$3/$1\n" if grep { $_ eq $code } split(/,\s*/, $keys); } }' -- -code="$code"
}
# Stops before anything changes if a shortcut other than the supplied owners has the key.
check_key_owner() {
    local code=${1:?key code required} owners others allowed owner
    shift
    owners=$(key_owners "$code") || fail "Could not read which shortcuts use key $code from KGlobalAccel. Nothing was changed."
    allowed=$'\n'
    for owner; do allowed+="$owner"$'\n'; done
    others=$(while IFS= read -r owner; do [[ -z $owner ]] && continue; grep -Fqx "$owner" <<<"$allowed" || printf '%s\n' "$owner"; done <<<"$owners")
    [[ -z $others ]] || fail "Key $code is bound to ${others//$'\n'/, }. Free it in System Settings → Shortcuts, then run this again. Nothing was changed."
}
# 0 when the launcher has Meta+Shift+S, 1 when not, 2 when KGlobalAccel could not be asked (see checked).
launcher_owns_key() {
    local owners
    owners=$(key_owners "$meta_shift_s_code") || return 2
    grep -Fqx "$launcher_owner" <<<"$owners"
}
launcher_gone() { ! qdbus6 org.kde.kglobalaccel /component/quick_region_screenshot_v3_desktop org.kde.kglobalaccel.Component.isActive >/dev/null 2>&1; }

# kglobalacceld looks up existing actions by component and action unique names only (kglobalacceld.cpp:71 findAction,
# v6.6.5); shortcutKeys/setShortcutKeys/setForeignShortcutKeys never create actions or change stored friendly names (only
# doRegister does), so the English labels are inert on translated sessions.
action_id() {
    case $1 in
        quick-region-overlay) printf '%s\n' "$1" "$2" 'Quick Region Overlay' 'Capture region to clipboard' ;;
        *) printf '%s\n' "$1" "$2" Spectacle 'Launch Spectacle' ;;
    esac
}
kglobalaccel_call() { busctl --user --json=short call org.kde.kglobalaccel /kglobalaccel org.kde.KGlobalAccel "$@"; }

# Key sequences are handled whole, one per line as its four Qt key codes (unused chords 0), so "Print" is
# "16777225 0 0 0" and a chord starting with Print is a different line. The v2 KGlobalAccel methods carry each sequence
# as an (ai) of four ints; the legacy ai methods keep only a sequence's first chord.
single_key_sequence() { printf '%s 0 0 0\n' "${1:?key code required}"; }
# Parses a recorded shortcutKeys reply ({"type":"a(ai)","data":[[[[c1,c2,c3,c4]],...]]}); empty sequences are dropped.
parse_action_keys() { jq -r '.data[0][] | .[0] | (. + [0, 0, 0, 0])[:4] | select(.[0] != 0) | map(tostring) | join(" ")'; }
# The busctl arguments for an a(ai) of the given sequence lines: the count, then each sequence as exactly 4 and its four
# int32 chords. kglobalacceld aborts on a sequence with fewer ints, taking KWin down with it, so anything that is not one
# to four non-negative int32 chords is refused before any output.
sequence_arguments() {
    local sequence chords chord output
    output=$#
    for sequence; do
        read -ra chords <<<"$sequence"
        [[ ${#chords[@]} -ge 1 && ${#chords[@]} -le 4 ]] || fail "Not a key sequence: $sequence"
        for chord in "${chords[@]}"; do
            if [[ ! $chord =~ ^[0-9]{1,10}$ ]] || ((10#$chord > 2147483647)); then fail "Not a key code: $chord"; fi
        done
        while ((${#chords[@]} < 4)); do chords+=(0); done
        output+=$'\n'"4"
        for chord in "${chords[@]}"; do output+=$'\n'"$((10#$chord))"; done
    done
    printf '%s\n' "$output"
}

# Current keys for a named action, used for the Print handover. Unlike getGlobalShortcutsByKey this excludes defaults
# that are listed by the desktop service but have been cleared in System Settings.
action_keys() {
    local output id=()
    mapfile -t id < <(action_id "$1" "$2")
    output=$(kglobalaccel_call shortcutKeys as 4 "${id[@]}" 2>/dev/null) || return 1
    parse_action_keys <<<"$output"
}
# The action's sequences except the single-key sequences of the given key codes; fails when the keys can't be read.
action_keys_without() {
    local component=$1 action_name=$2 keys sequence remove code
    shift 2
    keys=$(action_keys "$component" "$action_name") || return 1
    while read -r sequence; do
        [[ -n $sequence ]] || continue
        remove=0
        for code; do [[ $sequence == "$(single_key_sequence "$code")" ]] && remove=1 && break; done
        (( remove == 0 )) && printf '%s\n' "$sequence"
    done <<<"$keys"
    return 0
}
# Sets the named array to action_keys_without's sequences, or stops the script when the keys can't be read, so a failed
# query is never taken for an action without keys.
load_keys_without() {
    local -n loaded_keys=$1
    local loaded_output
    shift
    loaded_output=$(action_keys_without "$@") || fail "Could not read the keys of $1/$2 from KGlobalAccel. Run this again once KGlobalAccel answers."
    loaded_keys=()
    [[ -z $loaded_output ]] || mapfile -t loaded_keys <<<"$loaded_output"
}
# Whether the action has the single-key sequence of a key code: 0 yes, 1 no, 2 the keys could not be read.
action_has_key() {
    local component=$1 action_name=$2 code=$3 keys
    keys=$(action_keys "$component" "$action_name") || return 2
    grep -Fqx -- "$(single_key_sequence "$code")" <<<"$keys" || return 1
}
action_has_any_key() {
    local component=$1 action_name=$2 keys
    keys=$(action_keys "$component" "$action_name") || return 2
    [[ -n $keys ]] || return 1
}
# Sets the action's keys to the given sequence lines. Flag 4 is NoAutoloading: the keys are set as given, not replaced
# by saved ones.
set_action_key() {
    local component=$1 action_name=$2 id=() arguments=() output
    shift 2
    mapfile -t id < <(action_id "$component" "$action_name")
    output=$(sequence_arguments "$@") || return 1
    mapfile -t arguments <<<"$output"
    ((${#arguments[@]} == 1 + 5 * $#)) || return 1
    kglobalaccel_call setShortcutKeys 'asa(ai)u' 4 "${id[@]}" "${arguments[@]}" 4 >/dev/null
}
spectacle_owns_print() { action_has_key org.kde.spectacle.desktop _launch "$print_code"; }
spectacle_action_present() {
    qdbus6 org.kde.kglobalaccel /component/org_kde_spectacle_desktop \
        org.kde.kglobalaccel.Component.shortcutNames 2>/dev/null | grep -Fqx _launch
}
spectacle_pid=

# Register Spectacle's desktop action when its component is absent. The helper owns and kills only the process it starts;
# an already-running Spectacle instance is left alone. This is also used by rollback before setting _launch to Print.
# The helper closes take_lock's descriptor first, so a killed script never leaves it holding the lock.
start_spectacle_action() {
    if spectacle_action_present; then return 0; fi
    (
        [[ -z ${lock_fd:-} ]] || exec {lock_fd}>&-
        QT_QPA_PLATFORM=wayland exec spectacle --desktopfile org.kde.spectacle --dbus
    ) >/dev/null 2>&1 & spectacle_pid=$!
    wait_for 30 spectacle_action_present || {
        kill "$spectacle_pid" 2>/dev/null || true
        wait "$spectacle_pid" 2>/dev/null || true
        spectacle_pid=
        fail "Spectacle did not register _launch."
    }
}
stop_spectacle_action() {
    if [[ -n $spectacle_pid ]]; then
        kill "$spectacle_pid" 2>/dev/null || true
        wait "$spectacle_pid" 2>/dev/null || true
        spectacle_pid=
    fi
}

# setForeignShortcutKeys sets the keys with NoAutoloading as well; KGlobalAccel serializes an empty list as none.
set_foreign_shortcut_keys_empty() {
    local id=()
    mapfile -t id < <(action_id org.kde.spectacle.desktop _launch)
    kglobalaccel_call setForeignShortcutKeys 'asa(ai)' 4 "${id[@]}" 0 >/dev/null
}
# 0 when Spectacle's _launch has neither of its stock defaults, Print and Meta+Shift+S, 1 when it has one, 2 when its
# keys could not be read.
spectacle_defaults_removed() {
    local keys
    keys=$(action_keys org.kde.spectacle.desktop _launch) || return 2
    ! grep -Fqx -e "$(single_key_sequence "$print_code")" -e "$(single_key_sequence "$meta_shift_s_code")" <<<"$keys"
}
spectacle_config_empty() {
    local config="$HOME/.config/kglobalshortcutsrc"
    [[ -f $config ]] || return 1
    awk '
        /^\[services\]\[org\.kde\.spectacle\.desktop\]$/ { in_group=1; next }
        in_group && /^\[/ { in_group=0 }
        in_group && $0 == "_launch=none" { found=1 }
        END { exit !found }
    ' "$config"
}

# Keep Spectacle's launch action registered without Print or Meta+Shift+S: a fresh registration takes the stock defaults,
# both of them. A config-only check makes a normal install rerun a true no-op; when an older install unregistered the
# action or omitted the key, briefly register it before changing its keys. The Print names are kept for install.sh.
spectacle_config_no_print() {
    local config="$HOME/.config/kglobalshortcutsrc"
    [[ -f $config ]] || return 1
    awk '
        /^\[services\]\[org\.kde\.spectacle\.desktop\]$/ { in_group=1; next }
        in_group && /^\[/ { in_group=0 }
        in_group && /^_launch=/ {
            value=$0; sub(/^_launch=/, "", value)
            # KConfig writes several sequences separated by a literal \t and the chords of one separated by ", ", then
            # the default and friendly fields after bare commas; a chord starting with Print is not Print.
            if (value !~ /(^|,|\\t)(Print|Meta\+Shift\+S)(\\t|$|,[^ ]|,$)/) found=1
        }
        END { exit !found }
    ' "$config"
}
ensure_spectacle_action_print_removed() {
    if spectacle_action_present && checked spectacle_defaults_removed && spectacle_config_no_print; then return 0; fi
    start_spectacle_action
    local keys=()
    load_keys_without keys org.kde.spectacle.desktop _launch "$meta_shift_s_code" "$print_code"
    if ((${#keys[@]} == 0)); then
        set_foreign_shortcut_keys_empty
    else
        set_action_key org.kde.spectacle.desktop _launch "${keys[@]}" || fail "Could not set Spectacle's _launch keys."
    fi
    wait_for 30 checked spectacle_defaults_removed ||
        fail "Spectacle's _launch action did not remove Print and Meta+Shift+S."
    stop_spectacle_action
    if ((${#keys[@]} == 0)); then
        wait_for 30 spectacle_config_empty || fail "Spectacle's _launch empty state was not written as _launch=none."
    else
        wait_for 30 spectacle_config_no_print || fail "Spectacle's _launch keys without Print and Meta+Shift+S were not persisted."
    fi
}

# The rollback.sh mode whose backup has the installed launcher: spectacle-fast or originals, nothing without a launcher.
# Fails for a launcher that matches neither backup, which is never replaced or deleted.
launcher_mode() {
    [[ -e "$launcher_entry" ]] || return 0
    if cmp -s -- "$launcher_entry" "$spectacle_fast_backup/$launcher_relative"; then echo spectacle-fast
    elif cmp -s -- "$launcher_entry" "$originals_backup/$launcher_relative"; then echo originals
    else fail "$launcher_entry matches neither backup's launcher; it was changed by hand. Move it away yourself, then run this again. Nothing was changed."
    fi
}
rollback_command() { if [[ $1 == originals ]]; then echo "$source_dir/rollback.sh --originals"; else echo "$source_dir/rollback.sh"; fi; }
backup_dir() { if [[ $1 == originals ]]; then echo "$originals_backup"; else echo "$spectacle_fast_backup"; fi; }
# The files each backup's pipeline runs from, apart from the Spectacle override, which has its own integrity check.
backup_files() {
    echo "$launcher_relative"
    echo .config/systemd/user/quick-region-clipboard.service
    echo .local/bin/quick-region-screenshot
    if [[ $1 == originals ]]; then
        echo .local/libexec/quick-region-clipboard.py
        echo .local/libexec/quick-region-clipboard-service.py
    else
        echo .local/libexec/quick-region-clipboard-service
    fi
}
# Checks that rollback.sh <mode> could run: the backup's files, and for the originals what their scripts call.
check_backup() {
    local mode=$1 dir relative
    dir=$(backup_dir "$mode")
    while read -r relative; do [[ -f "$dir/$relative" ]] || fail "The $mode backup is missing $relative. Nothing was changed."; done < <(backup_files "$mode")
    command -v spectacle >/dev/null || fail 'spectacle is not installed. Nothing was changed.'
    if [[ $mode == originals ]]; then
        need_tools kioclient python3
        python3 -I -c 'import PyQt6.QtGui' 2>/dev/null || fail 'The originals need PyQt6 (python3-pyqt6). Nothing was changed.'
    fi
}

spectacle_override_entry="$HOME/.local/share/applications/org.kde.spectacle.desktop"
spectacle_override_relative=.local/share/applications/org.kde.spectacle.desktop
check_override() {
    [[ -e "$spectacle_override_entry" ]] || return 0
    local backup_file="$spectacle_fast_backup/$spectacle_override_relative"
    if [[ ! -f "$backup_file" ]] || ! cmp -s -- "$backup_file" "$spectacle_override_entry"; then
        fail "$spectacle_override_entry was edited or its backup is missing; nothing was changed."
    fi
}

# A file that a forward-only script would delete or overwrite may be absent, or must match one of the immutable
# backups. This prevents an interrupted run from destroying an edit made after the backup was created.
check_owned_file() {
    local target=$1 relative=$2 backup_file
    [[ -e "$target" ]] || return 0
    for backup_file in "$spectacle_fast_backup/$relative" "$originals_backup/$relative"; do
        [[ -f "$backup_file" ]] && cmp -s -- "$backup_file" "$target" && return 0
    done
    fail "$target was edited after its backup; nothing was changed."
}

# Overlay-managed files are tracked by content hash. An absent or matching file is safe to replace or remove; an
# edited, replaced, or untracked file is moved aside so an interrupted or older install can never erase user content.
manifest_hash() {
    local relative=$1 manifest=${manifest_file:-${state_dir:-}/manifest}
    [[ -f $manifest ]] || return 1
    awk -v path="$relative" '$2 == path { print $1; found=1; exit } END { if (!found) exit 1 }' "$manifest"
}
record_managed_file() {
    local relative=$1 target="$HOME/$1" manifest=${manifest_file:-${state_dir:-}/manifest} hash tmp
    [[ -f $target ]] || fail "Cannot record missing managed file $target."
    hash=$(sha256sum -- "$target" | awk '{print $1}')
    mkdir -p -- "$(dirname -- "$manifest")"
    tmp="$manifest.tmp.$$"
    if [[ -f $manifest ]]; then
        awk -v path="$relative" '$2 != path' "$manifest" >"$tmp"
    else
        : >"$tmp"
    fi
    printf '%s  %s\n' "$hash" "$relative" >>"$tmp"
    mv -f -- "$tmp" "$manifest"
}
protect_managed_file() {
    local relative=$1 target="$HOME/$1" expected actual stamp saved
    [[ -e $target || -L $target ]] || return 0
    expected=$(manifest_hash "$relative" 2>/dev/null || true)
    actual=
    [[ -f $target && ! -L $target ]] && actual=$(sha256sum -- "$target" | awk '{print $1}')
    [[ -n $expected && $expected == "$actual" ]] && return 0
    stamp=$(date -u +%Y%m%dT%H%M%SZ)
    saved="${state_dir:?}/saved/$stamp/$relative"
    mkdir -p -- "$(dirname -- "$saved")"
    while [[ -e $saved || -L $saved ]]; do
        stamp=$(date -u +%Y%m%dT%H%M%S.%NZ)
        saved="${state_dir:?}/saved/$stamp/$relative"
        mkdir -p -- "$(dirname -- "$saved")"
    done
    mv -- "$target" "$saved"
    printf 'Saved existing %s to %s\n' "$target" "$saved"
}

# Takes the launcher out of KGlobalAccel and frees Meta+Shift+S. Unregistering while its component still exists also
# drops its saved binding; the component goes shortly after the cache rebuild that no longer has its file.
retire_launcher() {
    unregister_shortcut quick-region-screenshot-v3.desktop _launch
    rm -f -- "$launcher_entry"
    rebuild_cache
    wait_for 30 launcher_gone || fail 'KGlobalAccel still lists the old launcher after its file was removed.'
}

unit_state() { systemctl --user show -P ActiveState "$1" 2>/dev/null || echo inactive; }
# Only active runs a process that owns its name (Type=dbus); activating, reloading or a crash-looping auto-restart
# does not, and a failed unit runs nothing.
state_running() { [[ $1 == active ]]; }
# Anything but inactive or failed may still have, or be about to start, a process.
state_needs_stop() { [[ $1 != inactive && $1 != failed ]]; }
daemon_running() { state_running "$(unit_state quick-region-overlay.service)"; }
daemon_needs_stop() { state_needs_stop "$(unit_state quick-region-overlay.service)"; }
# A retry after replacing the binary must restart an old process, even when the installed bytes already match.
# Bash -ef follows /proc/<pid>/exe and compares inodes, including when the running executable has been deleted.
daemon_executable_current() {
    local pid
    pid=$(systemctl --user show -P MainPID quick-region-overlay.service) || return 1
    [[ $pid =~ ^[0-9]+$ && $pid != 0 && /proc/$pid/exe -ef $HOME/.local/libexec/quick-region-overlay ]]
}
# Restarts the daemon from any state. reset-failed clears a failed state and its start limit, so a unit that failed
# repeatedly starts again instead of being refused.
restart_daemon() {
    systemctl --user reset-failed quick-region-overlay.service 2>/dev/null || true
    systemctl --user restart quick-region-overlay.service
}
# The running daemon's answer from KGlobalAccel: registered:<keys>, none[:<owners>] or error:<message>
# (ShortcutStatus in quick-region-overlay.cpp). Asks only a running service, because a call would D-Bus-activate a
# stopped one.
daemon_status() { daemon_running && qdbus6 org.local.QuickRegionOverlay / org.local.QuickRegionOverlay.ShortcutStatus; }
# Whether a ShortcutStatus answer includes a key. The daemon uses "; " between sequences and ", " between the chords
# of one, so a chord starting with the key does not match.
has_key() { [[ $1 == registered:* && "; ${1#registered:}; " == *"; $2; "* ]]; }
has_default_key() { has_key "$1" 'Meta+Shift+S'; }

# The handover marker in $state_dir/handover records what a run is taking from the old owners until the daemon has it:
#   pending-meta       Meta+Shift+S is being taken from the launcher or Spectacle; reset to defaults if it is missing
#   pending-print      as pending-meta, and Print is being taken from Spectacle (earlier installs wrote it this way too)
#   pending-print-add  only Print is being taken from Spectacle; it is added to the daemon's current keys, nothing reset
#   complete           nothing is pending; absent means the same
valid_marker() { [[ -z $1 || $1 == complete || $1 == pending-meta || $1 == pending-print || $1 == pending-print-add ]]; }
marker_pending() { [[ $1 == pending-meta || $1 == pending-print || $1 == pending-print-add ]]; }
# The handover this run must do, from the marker, whether the launcher is installed (0/1) and whether Spectacle holds
# Print and Meta+Shift+S (0/1): one of the pending kinds, or nothing. Taking Meta+Shift+S from the launcher or Spectacle
# can reset the daemon's keys; Spectacle holding Print alone never does, so custom or cleared keys survive a rerun.
handover_kind() {
    local marker=$1 launcher=$2 spectacle_print=$3 spectacle_meta=${4:-0} meta=0 print=0
    [[ $launcher == 1 || $spectacle_meta == 1 || $marker == pending-meta || $marker == pending-print ]] && meta=1
    [[ $spectacle_print == 1 || $marker == pending-print || $marker == pending-print-add ]] && print=1
    if ((meta && print)); then echo pending-print
    elif ((meta)); then echo pending-meta
    elif ((print)); then echo pending-print-add
    fi
}
handover_required_keys() {
    [[ $1 == pending-meta || $1 == pending-print ]] && printf '%s\n' 'Meta+Shift+S'
    [[ $1 == pending-print || $1 == pending-print-add ]] && printf '%s\n' Print
    return 0
}
handover_keys_present() {
    local status=$1 kind=$2 key
    while read -r key; do [[ -z $key ]] || has_key "$status" "$key" || return 1; done < <(handover_required_keys "$kind")
}
# Whether the daemon action must be unregistered and re-registered to acquire its defaults: only for a launcher handover
# or Spectacle handover whose daemon lacks Meta+Shift+S.
handover_needs_reset() { [[ $1 == pending-meta || $1 == pending-print ]] && ! has_default_key "$2"; }
# Whether Print must be added to the daemon's current keys after Spectacle gave it up.
handover_needs_print() { [[ $1 == pending-print || $1 == pending-print-add ]] && ! has_key "$2" Print; }

# Adds Print to the daemon action's current keys and keeps its other sequences, with no unregister.
add_daemon_print() {
    local keys=()
    load_keys_without keys quick-region-overlay capture-region "$print_code"
    keys+=("$(single_key_sequence "$print_code")")
    set_action_key quick-region-overlay capture-region "${keys[@]}" || fail "Could not add Print to the daemon's shortcut."
}
daemon_has_print() { local status; status=$(daemon_status) && has_key "$status" Print; }

# The rollback.sh mode for the recovery command: the installed launcher's backup, else the mode an interrupted
# launcher handover recorded in $state_dir/previous-mode, else spectacle-fast.
recovery_mode() {
    local launcher=$1 marker=$2 recorded=$3
    if [[ -n $launcher ]]; then echo "$launcher"
    elif [[ ($marker == pending-meta || $marker == pending-print) && -n $recorded ]]; then echo "$recorded"
    else echo spectacle-fast
    fi
}
valid_mode() { [[ $1 == spectacle-fast || $1 == originals ]]; }

# Changing the Spectacle override only reaches Spectacle's other actions after a new login: kglobalacceld keeps each
# component's desktop entry until then (refreshServices never refreshes an existing component). The note is kept in the
# state directory so a run that finishes after an interrupted one still prints it.
note_relogin() { mkdir -p -- "${state_dir:?}" && : >"$state_dir/relogin"; }
print_relogin_note() {
    [[ -e ${state_dir:?}/relogin ]] || return 0
    echo "$1"
    rm -f -- "$state_dir/relogin"
}

# A fresh install has no legacy pipeline or existing overlay state. A complete legacy handover without this record
# must remain on its historical rollback path. Keep the record after rollback so rollback itself is safe to repeat.
fresh_install_needed() {
    [[ -z $1 && -z $2 ]] || return 1
    local relative
    for relative in "$launcher_relative" "$spectacle_override_relative" \
        .config/systemd/user/quick-region-clipboard.service .local/bin/quick-region-screenshot \
        .local/libexec/quick-region-clipboard-service .local/libexec/quick-region-clipboard-service.py \
        .local/libexec/quick-region-clipboard.py .local/libexec/quick-region-overlay \
        .config/systemd/user/quick-region-overlay.service; do
        [[ ! -e $HOME/$relative && ! -L $HOME/$relative ]] || return 1
    done
}
load_fresh_keys() {
    fresh_keys=()
    [[ -f $fresh_file && ! -L $fresh_file ]] || fail "Invalid fresh-install record: $fresh_file. Nothing was changed."
    mapfile -t fresh_keys <"$fresh_file"
    local key
    for key in "${fresh_keys[@]}"; do
        [[ $key =~ ^[0-9]+\ [0-9]+\ [0-9]+\ [0-9]+$ ]] || fail "Invalid fresh-install keys in $fresh_file. Nothing was changed."
    done
    sequence_arguments "${fresh_keys[@]}" >/dev/null || fail "Invalid fresh-install keys in $fresh_file. Nothing was changed."
}
fresh_keys_restored() {
    local keys expected
    keys=$(action_keys org.kde.spectacle.desktop _launch) || return 1
    expected=$(printf '%s\n' "${fresh_keys[@]}")
    [[ $keys == "$expected" ]]
}
restore_fresh_keys() {
    start_spectacle_action
    if ((${#fresh_keys[@]})); then
        set_action_key org.kde.spectacle.desktop _launch "${fresh_keys[@]}" || fail "Could not restore Spectacle's original keys."
    else
        set_foreign_shortcut_keys_empty
    fi
    wait_for 30 fresh_keys_restored || fail "Spectacle's original keys were not restored."
    stop_spectacle_action
}
# Wait for the Type=dbus service and its action to be ready after a restart.
wait_daemon_status() {
    local answer try
    for ((try = 0; try < 30; try++)); do
        if answer=$(daemon_status 2>/dev/null) && [[ $answer != error:* ]]; then printf '%s\n' "$answer"; return 0; fi
        sleep 0.1
    done
    return 1
}

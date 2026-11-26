#!/bin/bash
# Checks shortcut-lib.sh's pure helpers with stubbed or recorded KGlobalAccel answers and a scratch HOME; touches nothing live.
# shellcheck disable=SC2329 # stubs that replace library functions are called through the library
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
HOME=$(mktemp -d)
export XDG_CONFIG_HOME="$HOME/.config"
trap 'rm -rf -- "$HOME"' EXIT
# shellcheck source=../shortcut-lib.sh
source "$source_dir/shortcut-lib.sh"
# Stubs replace library functions; restore_lib puts the library's own versions back.
for function in action_keys kglobalaccel_call; do eval "lib_$(declare -f "$function")"; done
restore_lib() { local function; for function; do eval "$(declare -f "lib_$function" | sed '1s/^lib_//')"; done; }
failures=0
check() { # description, expected status (0 or 1), command...
    local description=$1 expected=$2 status=0
    shift 2
    ("$@") >/dev/null 2>&1 || status=1
    if [[ $status != "$expected" ]]; then echo "FAIL: $description (status $status, expected $expected)"; failures=$((failures + 1)); fi
}
check_status() { # description, exact expected status, command...
    local description=$1 expected=$2 status=0
    shift 2
    ("$@") >/dev/null 2>&1 || status=$?
    if [[ $status != "$expected" ]]; then echo "FAIL: $description (status $status, expected $expected)"; failures=$((failures + 1)); fi
}
expect() { # description, expected output, actual output
    [[ $3 == "$2" ]] || { echo "FAIL: $1 (got '$3', expected '$2')"; failures=$((failures + 1)); }
}

# Real KConfig tools, private config and home only. Stub just the reload transport, never the settings I/O.
(
    state_dir="$HOME/klipper-state"
    mkdir -p "$state_dir"
    reload_klipper_settings() { :; }
    mkdir -p "$HOME/system-config"
    printf '[General]\nMaxClipItems=23\nIgnoreImages=true\n' >"$HOME/system-config/klipperrc"
    expect 'inherited length remains an absent user key' null "$(XDG_CONFIG_DIRS="$HOME/system-config" klipper_value_json MaxClipItems)"
    install_klipper_settings
    expect 'absent history length is installed' 100 "$(kreadconfig6 --file klipperrc --group General --key MaxClipItems)"
    expect 'absent image preference is installed' false "$(kreadconfig6 --file klipperrc --group General --key IgnoreImages)"
    install_klipper_settings
    expect 'rerun retains absent original' null "$(jq -r '.values.MaxClipItems' "$state_dir/klipper-settings.json")"
    restore_klipper_settings
    expect 'rollback removes absent length' absent "$(kreadconfig6 --file klipperrc --group General --key MaxClipItems --default absent)"
    expect 'rollback removes absent image preference' absent "$(kreadconfig6 --file klipperrc --group General --key IgnoreImages --default absent)"
    restore_klipper_settings
    kwriteconfig6 --file klipperrc --group General --key MaxClipItems 17
    kwriteconfig6 --file klipperrc --group General --key IgnoreImages true
    kwriteconfig6 --file klipperrc --group General --key Unrelated untouched
    install_klipper_settings
    install_klipper_settings
    expect 'rerun retains original custom length' 17 "$(jq -r '.values.MaxClipItems' "$state_dir/klipper-settings.json")"
    expect 'record is private' 600 "$(stat -c %a "$state_dir/klipper-settings.json")"
    restore_klipper_settings
    expect 'rollback restores custom length' 17 "$(kreadconfig6 --file klipperrc --group General --key MaxClipItems)"
    expect 'rollback restores custom image preference' true "$(kreadconfig6 --file klipperrc --group General --key IgnoreImages)"
    expect 'unrelated Klipper settings survive' untouched "$(kreadconfig6 --file klipperrc --group General --key Unrelated)"
    expect 'short custom length is raised to 100' 100 "$(install_klipper_settings >/dev/null; kreadconfig6 --file klipperrc --group General --key MaxClipItems)"
    restore_klipper_settings
    kwriteconfig6 --file klipperrc --group General --key MaxClipItems 250
    install_klipper_settings
    expect 'longer custom length is kept' 250 "$(kreadconfig6 --file klipperrc --group General --key MaxClipItems)"
    restore_klipper_settings
    expect 'rollback keeps longer custom length' 250 "$(kreadconfig6 --file klipperrc --group General --key MaxClipItems)"
    kwriteconfig6 --file klipperrc --group General --key MaxClipItems 55
    restore_klipper_settings
    expect 'second rollback keeps later user change' 55 "$(kreadconfig6 --file klipperrc --group General --key MaxClipItems)"
    kwriteconfig6 --file klipperrc --group General --key MaxClipItems __qro_absent_1__
    expect 'sentinel-like values are present values' '"__qro_absent_1__"' "$(klipper_value_json MaxClipItems)"
    printf 'not json\n' >"$state_dir/klipper-settings.json"
    check 'corrupt settings record is refused' 1 install_klipper_settings
    exit $((failures > 0))
) || { echo 'FAIL: Klipper install and rollback settings'; failures=$((failures + 1)); }
# D-Bus reload is only sent to a running Klipper and never restarts plasmashell or activates a missing Klipper.
(
    busctl() {
        printf '%s\n' "$*" >>"$HOME/klipper-bus-calls"
        [[ $* != *NameHasOwner* ]] || printf 'b %s\n' "$klipper_running"
    }
    klipper_running=true
    reload_klipper_settings
    expect 'live reload method and no activation' '--user --timeout=3 --auto-start=no call org.kde.klipper /klipper org.kde.klipper.klipper reloadConfig' "$(tail -1 "$HOME/klipper-bus-calls")"
    : >"$HOME/klipper-bus-calls"
    klipper_running=false
    reload_klipper_settings >/dev/null
    expect 'missing Klipper only checks ownership' 1 "$(wc -l <"$HOME/klipper-bus-calls")"
    exit $((failures > 0))
) || { echo 'FAIL: Klipper reload transport'; failures=$((failures + 1)); }
stub_owners=
key_owners() { printf "%s" "$stub_owners"; }

stub_owners=''; check 'free Meta+Shift+S' 0 check_key_owner "$meta_shift_s_code" "$daemon_owner" "$launcher_owner"
stub_owners=$daemon_owner; check 'daemon owns Meta+Shift+S' 0 check_key_owner "$meta_shift_s_code" "$daemon_owner" "$launcher_owner"
stub_owners=$launcher_owner; check 'launcher owns Meta+Shift+S' 0 check_key_owner "$meta_shift_s_code" "$daemon_owner" "$launcher_owner"
stub_owners=$'kwin/Something\n'$daemon_owner; check 'a third party owns Meta+Shift+S' 1 check_key_owner "$meta_shift_s_code" "$daemon_owner" "$launcher_owner"
message=$(stub_owners='org.kde.spectacle.desktop/_launch' check_key_owner "$meta_shift_s_code" "$daemon_owner" "$launcher_owner" 2>&1) || true
[[ $message == *'org.kde.spectacle.desktop/_launch'* ]] || { echo "FAIL: the owner check names the owner ($message)"; failures=$((failures + 1)); }
stub_owners='org.kde.spectacle.desktop/_launch'; check 'Spectacle owns Print' 0 check_key_owner "$print_code" "$daemon_owner" org.kde.spectacle.desktop/_launch
stub_owners=$launcher_owner; check_status 'the launcher has Meta+Shift+S' 0 launcher_owns_key
stub_owners=$daemon_owner; check_status 'the launcher lacks Meta+Shift+S' 1 launcher_owns_key
key_owners() { return 1; }
check_status 'a failed owner query is not "no"' 2 launcher_owns_key
check 'a failed owner query stops the owner check' 1 check_key_owner "$meta_shift_s_code" "$daemon_owner"
message=$(check_key_owner "$meta_shift_s_code" "$daemon_owner" 2>&1) || true
[[ $message == *'Could not read'* ]] || { echo "FAIL: the failed owner query is named ($message)"; failures=$((failures + 1)); }
key_owners() { printf "%s" "$stub_owners"; }

# Recorded live (2026-10-18, qdbus6 --literal ... getGlobalShortcutsByKey 16777225): qdbus prints the whole array on one line.
live='[Argument: a(ssssssaiai) {[Argument: (ssssssaiai) "capture-region", "Capture region to clipboard", "quick-region-overlay", "Quick Region Overlay", "default", "Default Context", [Argument: ai {301989971, 16777225}], [Argument: ai {301989971, 16777225}]]}]'
expect 'live qdbus parser sample, Meta+Shift+S' "$daemon_owner" "$(parse_key_owners 301989971 <<<"$live")"
expect 'live qdbus parser sample, Print' "$daemon_owner" "$(parse_key_owners 16777225 <<<"$live")"
expect 'an empty array has no owners' '' "$(parse_key_owners 16777225 <<<'[Argument: a(ssssssaiai) {}]')"
quoted='[Argument: a(ssssssaiai) {[Argument: (ssssssaiai) "_launch", "A "quoted" display", "org.kde.spectacle.desktop", "Spectacle", "default", "Default Context", [Argument: ai {16777225}], [Argument: ai {16777225}]]}]'
expect 'quoted display-name parser sample' 'org.kde.spectacle.desktop/_launch' "$(parse_key_owners 16777225 <<<"$quoted")"
# Several owners in the same one-line format, built from the live sample: qdbus joins array elements with ", ".
several='[Argument: a(ssssssaiai) {[Argument: (ssssssaiai) "_launch", "A "quoted", "display", "org.kde.spectacle.desktop", "Spectacle", "default", "Default Context", [Argument: ai {16777225}], [Argument: ai {16777225}]], [Argument: (ssssssaiai) "capture-region", "Capture region to clipboard", "quick-region-overlay", "Quick Region Overlay", "default", "Default Context", [Argument: ai {301989971, 16777225}], [Argument: ai {301989971, 16777225}]], [Argument: (ssssssaiai) "_launch", "Quick Region Screenshot", "quick-region-screenshot-v3.desktop", "Quick Region Screenshot", "default", "Default Context", [Argument: ai {301989971}], [Argument: ai {301989971}]]}]'
expect 'every owner on one line, Print' $'org.kde.spectacle.desktop/_launch\nquick-region-overlay/capture-region' "$(parse_key_owners 16777225 <<<"$several")"
expect 'every owner on one line, Meta+Shift+S' $'quick-region-overlay/capture-region\nquick-region-screenshot-v3.desktop/_launch' "$(parse_key_owners 301989971 <<<"$several")"

# Recorded busctl --json=short shortcutKeys replies (2026-10-18, kglobalacceld 6.6.5).
action_empty='{"type":"a(ai)","data":[[]]}'
action_single='{"type":"a(ai)","data":[[[[218103866,0,0,0]]]]}'
action_several='{"type":"a(ai)","data":[[[[301989971,0,0,0]],[[16777225,0,0,0]]]]}'
action_chord='{"type":"a(ai)","data":[[[[218103866,0,0,0]],[[218103867,218103865,0,0]]]]}'
action_blanked='{"type":"a(ai)","data":[[[[201326650,0,0,0]],[[0,0,0,0]]]]}'
[[ -z $(parse_action_keys <<<"$action_empty") ]] || { echo 'FAIL: empty action-key parser sample'; failures=$((failures + 1)); }
[[ $(parse_action_keys <<<"$action_single") == '218103866 0 0 0' ]] || { echo 'FAIL: single action-key parser sample'; failures=$((failures + 1)); }
[[ $(parse_action_keys <<<"$action_several") == $'301989971 0 0 0\n16777225 0 0 0' ]] || { echo 'FAIL: multi-key action parser sample'; failures=$((failures + 1)); }
[[ $(parse_action_keys <<<"$action_chord") == $'218103866 0 0 0\n218103867 218103865 0 0' ]] || { echo 'FAIL: chord action parser sample'; failures=$((failures + 1)); }
[[ $(parse_action_keys <<<"$action_blanked") == '201326650 0 0 0' ]] || { echo 'FAIL: an empty sequence is dropped'; failures=$((failures + 1)); }
[[ $(parse_action_keys <<<'{"type":"a(ai)","data":[[[[16777225]]]]}') == '16777225 0 0 0' ]] || { echo 'FAIL: a short sequence is padded'; failures=$((failures + 1)); }
check 'a malformed reply fails' 1 parse_action_keys <<<'not json'

# Every a(ai) sent to kglobalacceld must be the count, then for each sequence exactly 4 and four int32 chords.
well_formed() {
    local lines=() count i
    mapfile -t lines
    count=${lines[0]:-x}
    [[ $count =~ ^[0-9]+$ && ${#lines[@]} == $((1 + 5 * count)) ]] || return 1
    for ((i = 1; i < ${#lines[@]}; i += 5)); do
        [[ ${lines[i]} == 4 ]] || return 1
        printf '%s\n' "${lines[@]:i+1:4}" | grep -Eqvx '[0-9]+' && return 1
    done
    return 0
}
expect 'no sequences' 0 "$(sequence_arguments)"
expect 'a one-chord sequence' '1 4 16777225 0 0 0' "$(sequence_arguments 16777225 | paste -sd ' ')"
expect 'a four-chord sequence' '1 4 1 2 3 4' "$(sequence_arguments '1 2 3 4' | paste -sd ' ')"
expect 'sequence arguments' '2 4 16777225 0 0 0 4 218103867 218103865 0 0' "$(sequence_arguments '16777225 0 0 0' '218103867 218103865' | paste -sd ' ')"
for input in '' 16777225 '1 2 3 4' $'16777225 0 0 0\n218103867 218103865'; do
    sequences=()
    [[ -z $input ]] || mapfile -t sequences <<<"$input"
    sequence_arguments "${sequences[@]}" | well_formed || { echo "FAIL: well-formed arguments for '$input'"; failures=$((failures + 1)); }
done
for bad in '1 2 3 4 5' '' Print '16777225 x' -1 2147483648 99999999999 1.5; do
    check "the sequence '$bad' is refused" 1 sequence_arguments 16777225 "$bad"
    expect "nothing is emitted for '$bad'" '' "$(sequence_arguments 16777225 "$bad" 2>/dev/null)"
done
expect 'the largest int32 is a key code' '1 4 2147483647 0 0 0' "$(sequence_arguments 2147483647 | paste -sd ' ')"
expect 'leading zeros are decimal' '1 4 8 9 0 0' "$(sequence_arguments '08 09' | paste -sd ' ')"

# The set helpers call busctl only with well-formed sequences, and then with exactly these arguments.
busctl_log="$HOME/busctl-calls"
busctl() { printf '%s|' "$@" >>"$busctl_log"; }
for bad in '1 2 3 4 5' '' Print -1; do
    rm -f -- "$busctl_log"
    check "set_action_key refuses '$bad'" 1 set_action_key org.kde.spectacle.desktop _launch '16777225 0 0 0' "$bad"
    [[ ! -e $busctl_log ]] || { echo "FAIL: busctl was called for '$bad'"; failures=$((failures + 1)); }
done
rm -f -- "$busctl_log"
check 'set_action_key sends whole sequences' 0 set_action_key org.kde.spectacle.desktop _launch '301989971 0 0 0' '16777225 218103865'
expect 'setShortcutKeys arguments' '--user|--json=short|call|org.kde.kglobalaccel|/kglobalaccel|org.kde.KGlobalAccel|setShortcutKeys|asa(ai)u|4|org.kde.spectacle.desktop|_launch|Spectacle|Launch Spectacle|2|4|301989971|0|0|0|4|16777225|218103865|0|0|4|' "$(cat -- "$busctl_log")"
rm -f -- "$busctl_log"
check 'setForeignShortcutKeys with no keys' 0 set_foreign_shortcut_keys_empty
expect 'setForeignShortcutKeys arguments' '--user|--json=short|call|org.kde.kglobalaccel|/kglobalaccel|org.kde.KGlobalAccel|setForeignShortcutKeys|asa(ai)|4|org.kde.spectacle.desktop|_launch|Spectacle|Launch Spectacle|0|' "$(cat -- "$busctl_log")"

# Adding Print to the daemon keeps its other sequences and changes nothing else.
action_keys() { printf '%s\n' '301989971 0 0 0' '16777225 218103865 0 0'; }
rm -f -- "$busctl_log"
check 'Print is added to the daemon' 0 add_daemon_print
expect 'daemon Print arguments' '--user|--json=short|call|org.kde.kglobalaccel|/kglobalaccel|org.kde.KGlobalAccel|setShortcutKeys|asa(ai)u|4|quick-region-overlay|capture-region|Quick Region Overlay|Capture region to clipboard|3|4|301989971|0|0|0|4|16777225|218103865|0|0|4|16777225|0|0|0|4|' "$(cat -- "$busctl_log")"
action_keys() { printf '%s\n' '16777225 0 0 0' '218103866 0 0 0'; }
rm -f -- "$busctl_log"
add_daemon_print
expect 'Print is not added twice' '2|4|218103866|0|0|0|4|16777225|0|0|0|4|' "$(sed 's/.*Capture region to clipboard|//' "$busctl_log")"
action_keys() { return 1; }
rm -f -- "$busctl_log"
check 'a failed key query stops the Print addition' 1 add_daemon_print
[[ ! -e $busctl_log ]] || { echo 'FAIL: busctl was called after a failed key query'; failures=$((failures + 1)); }
unset -f busctl
restore_lib action_keys

# A failed or unreadable shortcutKeys query is a failure, never "no keys".
kglobalaccel_fails() { return 1; }
kglobalaccel_garbles() { echo 'not json'; }
for stub in kglobalaccel_fails kglobalaccel_garbles; do
    kglobalaccel_call() { "$stub"; }
    check "action_keys fails ($stub)" 1 action_keys org.kde.spectacle.desktop _launch
    check "action_keys_without fails ($stub)" 1 action_keys_without org.kde.spectacle.desktop _launch "$print_code"
    check_status "action_has_key cannot answer ($stub)" 2 action_has_key org.kde.spectacle.desktop _launch "$print_code"
    check_status "action_has_any_key cannot answer ($stub)" 2 action_has_any_key org.kde.spectacle.desktop _launch
    check_status "spectacle_owns_print cannot answer ($stub)" 2 spectacle_owns_print
    check_status "spectacle_defaults_removed cannot answer ($stub)" 2 spectacle_defaults_removed
    check "checked stops on it ($stub)" 1 checked spectacle_owns_print
    check "load_keys_without stops on it ($stub)" 1 load_keys_without keys org.kde.spectacle.desktop _launch "$print_code"
done
message=$(checked spectacle_defaults_removed 2>&1) || true
[[ $message == *'Could not read shortcut keys'* ]] || { echo "FAIL: checked names the failed query ($message)"; failures=$((failures + 1)); }
kglobalaccel_call() { echo '{"type":"a(ai)","data":[[[[16777225,0,0,0]],[[218103866,0,0,0]]]]}'; }
check_status 'Spectacle holds Print' 0 spectacle_owns_print
check_status 'Print is not removed' 1 spectacle_defaults_removed
check_status 'checked passes a yes' 0 checked spectacle_owns_print
check_status 'checked passes a no' 1 checked spectacle_defaults_removed
keys=()
load_keys_without keys org.kde.spectacle.desktop _launch "$print_code"
expect 'loaded keys without Print' '218103866 0 0 0' "${keys[*]}"
kglobalaccel_call() { echo '{"type":"a(ai)","data":[[]]}'; }
check_status 'no keys is a real no' 1 action_has_any_key org.kde.spectacle.desktop _launch
check_status 'Print is removed from no keys' 0 spectacle_defaults_removed
load_keys_without keys org.kde.spectacle.desktop _launch "$print_code"
expect 'no keys loads an empty array' 0 "${#keys[@]}"
kglobalaccel_call() { echo '{"type":"a(ai)","data":[[[[301989971,0,0,0]],[[218103866,0,0,0]]]]}'; }
check_status 'Meta+Shift+S is not removed' 1 spectacle_defaults_removed
kglobalaccel_call() { echo '{"type":"a(ai)","data":[[[[301989971,218103866,0,0]],[[218103866,0,0,0]]]]}'; }
check_status 'a chord starting with Meta+Shift+S is not a default' 0 spectacle_defaults_removed
unset -f kglobalaccel_fails kglobalaccel_garbles
restore_lib kglobalaccel_call

# Removing Print removes only the single-key Print sequence; a chord starting with Print and Meta+Print stay.
stub_action_keys=
action_keys() { [[ -n $stub_action_keys ]] && printf '%s\n' "$stub_action_keys"; return 0; }
stub_action_keys=$'16777225 0 0 0\n16777225 218103865 0 0\n285212681 0 0 0'
[[ $(action_keys_without org.kde.spectacle.desktop _launch "$print_code") == $'16777225 218103865 0 0\n285212681 0 0 0' ]] ||
    { echo 'FAIL: Print removal keeps chords'; failures=$((failures + 1)); }
check 'Print is held' 0 action_has_key org.kde.spectacle.desktop _launch "$print_code"
stub_action_keys=$'301989971 0 0 0\n16777225 218103865 0 0\n218103866 0 0 0'
[[ $(action_keys_without org.kde.spectacle.desktop _launch "$meta_shift_s_code" "$print_code") == $'16777225 218103865 0 0\n218103866 0 0 0' ]] ||
    { echo 'FAIL: rollback key removal'; failures=$((failures + 1)); }
check 'a chord starting with Print is not Print' 1 action_has_key org.kde.spectacle.desktop _launch "$print_code"
stub_action_keys=; check 'no keys' 1 action_has_any_key org.kde.spectacle.desktop _launch
[[ -z $(action_keys_without org.kde.spectacle.desktop _launch "$print_code") ]] || { echo 'FAIL: removal from no keys'; failures=$((failures + 1)); }
restore_lib action_keys

check 'active is running' 0 state_running active
for state in activating reloading deactivating failed inactive; do check "$state is not running" 1 state_running "$state"; done
for state in active activating reloading deactivating refreshing; do check "$state needs stopping" 0 state_needs_stop "$state"; done
check 'failed needs no stop' 1 state_needs_stop failed
check 'inactive needs no stop' 1 state_needs_stop inactive

# Fresh Spectacle Meta+Shift+S must trigger a handover of both defaults, including after an interruption.
expect 'fresh Spectacle defaults' pending-print "$(handover_kind '' 0 1 1)"
expect 'fresh Spectacle Meta alone' pending-meta "$(handover_kind '' 0 0 1)"
expect 'fresh pending defaults survive retirement' pending-print "$(handover_kind pending-print 0 0 0)"
fresh_profile_test() {
    local state_dir fresh_file
    state_dir="$HOME/fresh-state"
    fresh_file="$state_dir/fresh-spectacle-keys"
    mkdir -p "$state_dir"
    check 'an empty profile is fresh' 0 fresh_install_needed '' ''
    check 'a legacy complete handover is not fresh' 1 fresh_install_needed '' complete
    check 'a legacy pending handover is not fresh' 1 fresh_install_needed '' pending-meta
    check 'a previous launcher is not fresh' 1 fresh_install_needed originals ''
    mkdir -p "$HOME/.local/libexec"
    touch "$HOME/.local/libexec/quick-region-overlay"
    check 'an existing overlay is not fresh' 1 fresh_install_needed '' ''
    rm "$HOME/.local/libexec/quick-region-overlay"
    spectacle_action_present() { return 0; }
    action_keys() { cat "$state_dir/current"; }
    set_action_key() { shift 2; printf '%s\n' "$@" >"$state_dir/current"; }
    set_foreign_shortcut_keys_empty() { : >"$state_dir/current"; }
    printf '%s\n' '16777225 0 0 0' '301989971 0 0 0' '218103866 218103865 0 0' >"$fresh_file"
    load_fresh_keys
    : >"$state_dir/current"
    restore_fresh_keys
    expect 'fresh rollback restores exact original keys including chords' "$(cat "$fresh_file")" "$(cat "$state_dir/current")"
    restore_fresh_keys
    expect 'fresh rollback is repeatable' "$(cat "$fresh_file")" "$(cat "$state_dir/current")"
    : >"$fresh_file"
    load_fresh_keys
    restore_fresh_keys
    expect 'fresh rollback restores originally cleared keys' '' "$(cat "$state_dir/current")"
    echo 'not a key' >"$fresh_file"
    check 'corrupt fresh records are refused' 1 load_fresh_keys
    echo '2147483648 0 0 0' >"$fresh_file"
    check 'out of range fresh keys are refused' 1 load_fresh_keys
    rm "$fresh_file"
    check 'missing fresh records are refused' 1 load_fresh_keys
    exit $((failures > 0))
}
(fresh_profile_test) || { echo 'FAIL: fresh install and rollback helpers'; failures=$((failures + 1)); }

mkdir -p "$(dirname -- "$launcher_entry")"
[[ -z $(launcher_mode) ]] || { echo 'FAIL: no launcher has no mode'; failures=$((failures + 1)); }
cp -- "$spectacle_fast_backup/$launcher_relative" "$launcher_entry"
[[ $(launcher_mode) == spectacle-fast ]] || { echo 'FAIL: the fast launcher'; failures=$((failures + 1)); }
cp -- "$originals_backup/$launcher_relative" "$launcher_entry"
[[ $(launcher_mode) == originals ]] || { echo 'FAIL: the originals launcher'; failures=$((failures + 1)); }
echo '# edited' >>"$launcher_entry"
check 'an edited launcher is refused' 1 launcher_mode

check 'default key' 0 has_default_key 'registered:Meta+Shift+S'
check 'default key among others' 0 has_default_key 'registered:Meta+F12; Meta+Shift+S'
check 'Print among several keys' 0 has_key 'registered:Meta+Shift+S; Print' Print
check 'another key only' 1 has_default_key 'registered:Meta+Shift+F12'
check 'a longer chord' 1 has_default_key 'registered:Ctrl+Meta+Shift+S'
check 'a chord starting with Meta+Shift+S' 1 has_default_key 'registered:Meta+Shift+S, Ctrl+C; Print'
check 'Print after such a chord' 0 has_key 'registered:Meta+Shift+S, Ctrl+C; Print' Print
check 'a chord starting with Print' 1 has_key 'registered:Meta+Shift+S; Print, Ctrl+C' Print
check 'no keys' 1 has_default_key 'none:quick-region-screenshot-v3.desktop/_launch'
check 'pending-meta required keys' 0 handover_keys_present 'registered:Meta+Shift+S' pending-meta
check 'pending-print missing Print' 1 handover_keys_present 'registered:Meta+Shift+S' pending-print
check 'pending-print required keys' 0 handover_keys_present 'registered:Meta+Shift+S; Print' pending-print
check 'pending-print-add needs only Print' 0 handover_keys_present 'registered:Meta+Shift+F12; Print' pending-print-add
check 'pending-print-add missing Print' 1 handover_keys_present 'registered:Meta+Shift+F12' pending-print-add

# Handover decisions: marker × launcher installed × Spectacle holds Print → kind ("-" is empty).
for marker in '' complete pending-meta pending-print pending-print-add bogus; do
    case $marker in ''|complete|pending-*) want=0 ;; *) want=1 ;; esac
    check "marker '$marker' validity" "$want" valid_marker "$marker"
done
while read -r marker launcher spectacle_print want; do
    [[ $marker == - ]] && marker=
    [[ $want == - ]] && want=
    expect "handover for marker '$marker', launcher $launcher, Spectacle Print $spectacle_print" "$want" "$(handover_kind "$marker" "$launcher" "$spectacle_print")"
done <<'TABLE'
- 0 0 -
- 0 1 pending-print-add
- 1 0 pending-meta
- 1 1 pending-print
complete 0 0 -
complete 0 1 pending-print-add
complete 1 0 pending-meta
complete 1 1 pending-print
pending-meta 0 0 pending-meta
pending-meta 0 1 pending-print
pending-meta 1 0 pending-meta
pending-meta 1 1 pending-print
pending-print 0 0 pending-print
pending-print 0 1 pending-print
pending-print 1 0 pending-print
pending-print 1 1 pending-print
pending-print-add 0 0 pending-print-add
pending-print-add 0 1 pending-print-add
pending-print-add 1 0 pending-print
pending-print-add 1 1 pending-print
TABLE
# Kind × daemon status → reset (unregister and re-register: 0 yes), add Print (0 yes). Only a launcher handover whose
# daemon lacks Meta+Shift+S resets, so custom and cleared keys survive Spectacle holding Print.
while IFS='|' read -r kind status want_reset want_print; do
    check "reset for '$kind' with '$status'" "$want_reset" handover_needs_reset "$kind" "$status"
    check "Print added for '$kind' with '$status'" "$want_print" handover_needs_print "$kind" "$status"
done <<'TABLE'
|registered:Meta+Shift+S; Print|1|1
|registered:Meta+Shift+F12|1|1
|none|1|1
pending-meta|registered:Meta+Shift+S; Print|1|1
pending-meta|registered:Meta+Shift+S|1|1
pending-meta|registered:Meta+Shift+F12|0|1
pending-meta|none:quick-region-screenshot-v3.desktop/_launch|0|1
pending-print|registered:Meta+Shift+S; Print|1|1
pending-print|registered:Meta+Shift+S|1|0
pending-print|registered:Print|0|1
pending-print|none:quick-region-screenshot-v3.desktop/_launch, org.kde.spectacle.desktop/_launch|0|0
pending-print|none|0|0
pending-print-add|registered:Meta+Shift+S; Print|1|1
pending-print-add|registered:Meta+Shift+S|1|0
pending-print-add|registered:Meta+Shift+F12|1|0
pending-print-add|registered:Meta+Shift+S, Print|1|0
pending-print-add|none|1|0
pending-print-add|none:org.kde.spectacle.desktop/_launch|1|0
TABLE

# The rollback mode for the recovery command survives the launcher's removal only while its handover is pending.
while read -r launcher marker recorded want; do
    [[ $launcher == - ]] && launcher=
    [[ $marker == - ]] && marker=
    [[ $recorded == - ]] && recorded=
    expect "recovery mode for launcher '$launcher', marker '$marker', recorded '$recorded'" "$want" "$(recovery_mode "$launcher" "$marker" "$recorded")"
done <<'TABLE'
- - - spectacle-fast
spectacle-fast - - spectacle-fast
originals complete - originals
originals pending-meta spectacle-fast originals
- pending-meta originals originals
- pending-print originals originals
- pending-meta - spectacle-fast
- pending-print-add originals spectacle-fast
- complete originals spectacle-fast
- - originals spectacle-fast
TABLE
check 'spectacle-fast is a mode' 0 valid_mode spectacle-fast
check 'originals is a mode' 0 valid_mode originals
check 'an empty mode is invalid' 1 valid_mode ''
check 'another mode is invalid' 1 valid_mode --originals

state_dir="$HOME/.local/state/quick-region-overlay"
manifest_file="$state_dir/manifest"
mkdir -p "$HOME/.local/libexec"
printf 'original' >"$HOME/.local/libexec/managed"
record_managed_file .local/libexec/managed
printf 'edited' >"$HOME/.local/libexec/managed"
saved_message=$(protect_managed_file .local/libexec/managed)
saved_file=$(find "$state_dir/saved" -type f -path '*/.local/libexec/managed' -print -quit)
[[ ! -e "$HOME/.local/libexec/managed" && -n $saved_file && $(cat "$saved_file") == edited && $saved_message == *'Saved existing'* ]] || {
    echo 'FAIL: edited managed file is saved'; failures=$((failures + 1))
}
printf 'original' >"$HOME/.local/libexec/managed"
check 'matching managed file is retained' 0 protect_managed_file .local/libexec/managed
printf 'untracked' >"$HOME/.local/libexec/untracked"
check 'untracked managed file is saved' 0 protect_managed_file .local/libexec/untracked
[[ ! -e "$HOME/.local/libexec/untracked" ]] || { echo 'FAIL: untracked file was not moved'; failures=$((failures + 1)); }

mkdir -p "$(dirname -- "$HOME/.config/kglobalshortcutsrc")"
cat >"$HOME/.config/kglobalshortcutsrc" <<'EOF'
[services][org.kde.spectacle.desktop]
CurrentMonitorScreenShot=
_launch=none

EOF
check 'Spectacle none config' 0 spectacle_config_empty
sed -i 's/_launch=none/_launch=Print/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle non-empty config' 1 spectacle_config_empty
check 'Spectacle Print config' 1 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Meta+Ctrl+S\\tPrint/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with Print after another key' 1 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Meta+Ctrl+S\\tMeta+Print/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with other keys only' 0 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Print, Ctrl+Alt+F10\\tMeta+Ctrl+S/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with a chord starting with Print' 0 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Ctrl+Alt+F10, Print/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with a chord ending in Print' 0 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Print, Ctrl+Alt+F10\\tPrint/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with Print after a Print chord' 1 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Print,Print,Spectacle/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with Print before the default field' 1 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Meta+Ctrl+S\\tMeta+Shift+S/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with Meta+Shift+S' 1 spectacle_config_no_print
sed -i 's/^_launch=.*/_launch=Meta+Shift+S, Ctrl+C\\tCtrl+Meta+Shift+S/' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle config with a Meta+Shift+S chord' 0 spectacle_config_no_print
sed -i '/^_launch=/d' "$HOME/.config/kglobalshortcutsrc"
check 'Spectacle missing config key' 1 spectacle_config_empty
check 'Spectacle missing config key is not Print-free' 1 spectacle_config_no_print

# A fresh registration takes Spectacle's stock defaults, Print and Meta+Shift+S; the fix strips both and keeps other keys.
(
    spectacle_action_present() { return 0; }
    action_keys() { cat -- "$HOME/spectacle-keys"; }
    set_action_key() { shift 2; printf '%s\n' "$@" >"$HOME/spectacle-keys"; sed -i 's/^_launch=.*/_launch=Meta+Ctrl+S/' "$HOME/.config/kglobalshortcutsrc"; }
    set_foreign_shortcut_keys_empty() { : >"$HOME/spectacle-keys"; sed -i 's/^_launch=.*/_launch=none/' "$HOME/.config/kglobalshortcutsrc"; }
    sleep() { :; }
    printf '%s\n' '16777225 0 0 0' '301989971 0 0 0' '335544403 0 0 0' >"$HOME/spectacle-keys"
    printf '_launch=Print\\tMeta+Shift+S\\tMeta+Ctrl+S\n' >>"$HOME/.config/kglobalshortcutsrc"
    ensure_spectacle_action_print_removed
    expect 'the defaults are stripped, other keys kept' '335544403 0 0 0' "$(cat -- "$HOME/spectacle-keys")"
    # Meta+Shift+S alone still needs the fix, which then empties the action.
    printf '%s\n' '301989971 0 0 0' >"$HOME/spectacle-keys"
    sed -i 's/^_launch=.*/_launch=Meta+Shift+S/' "$HOME/.config/kglobalshortcutsrc"
    ensure_spectacle_action_print_removed
    expect 'Meta+Shift+S alone is stripped' '' "$(cat -- "$HOME/spectacle-keys")"
    exit $((failures > 0))
) || { echo 'FAIL: the Spectacle defaults fix'; failures=$((failures + 1)); }

# One lock for both scripts; a second holder fails fast.
exec {held_lock}>"$state_dir/lock"
flock -n "$held_lock"
check 'a held lock is refused' 1 take_lock
message=$(take_lock 2>&1) || true
[[ $message == *'Another install.sh or rollback.sh is running'* ]] || { echo "FAIL: the held lock is named ($message)"; failures=$((failures + 1)); }
exec {held_lock}>&-
check 'a free lock is taken' 0 take_lock
rm -rf -- "${state_dir:?}"
check 'the lock creates the state directory' 0 take_lock
[[ -f $state_dir/lock ]] || { echo 'FAIL: the lock file was not created'; failures=$((failures + 1)); }

# The Spectacle helper closes the lock's descriptor, so a killed script never leaves it holding the lock; without a lock
# it starts as well.
mkdir -p -- "$HOME/bin"
cat >"$HOME/bin/spectacle" <<'STUB'
#!/bin/bash
readlink /proc/$$/fd/* >"$HOME/helper-fds.tmp"; mv -- "$HOME/helper-fds.tmp" "$HOME/helper-fds"
exec sleep 30
STUB
chmod +x -- "$HOME/bin/spectacle"
for take in take_lock true; do
    rm -f -- "$HOME/helper-fds"
    message=$(
        PATH=$HOME/bin:$PATH
        spectacle_action_present() { [[ -e $HOME/helper-fds ]]; }
        "$take"
        start_spectacle_action
        stop_spectacle_action
        cat -- "$HOME/helper-fds"
    ) || true
    [[ -n $message && $message != *"$state_dir/lock"* ]] || { echo "FAIL: the Spectacle helper's descriptors ($take): $message"; failures=$((failures + 1)); }
done

# The shared exit handler kills the Spectacle helper this run started and names the recovery command after a failure.
sleep 30 &
helper=$!
message=$( (set +e; spectacle_pid=$helper; recover='rollback.sh'; work=$HOME/work; mkdir -p "$work"; (exit 1); on_exit) 2>&1 )
if kill -0 "$helper" 2>/dev/null; then echo 'FAIL: the exit handler left the Spectacle helper running'; failures=$((failures + 1)); kill "$helper"; fi
wait "$helper" 2>/dev/null || true
[[ $message == *'To recover, run: rollback.sh'* && ! -e $HOME/work ]] || { echo "FAIL: the exit handler's cleanup and message ($message)"; failures=$((failures + 1)); }
expect 'a successful exit prints nothing' '' "$( (set +e; recover='rollback.sh'; true; on_exit) 2>&1 )"

# The re-login note is printed once, by whichever run finishes after the override changed.
expect 'no note without a change' '' "$(print_relogin_note 'Log out')"
note_relogin
expect 'the note after a change' 'Log out' "$(print_relogin_note 'Log out')"
expect 'the note only once' '' "$(print_relogin_note 'Log out')"

[[ $(rollback_command originals) == "$source_dir/rollback.sh --originals" && $(rollback_command spectacle-fast) == "$source_dir/rollback.sh" ]] ||
    { echo 'FAIL: rollback commands'; failures=$((failures + 1)); }
(
    mkdir -p "$HOME/.local/libexec"
    install -m755 /usr/bin/python3 "$HOME/.local/libexec/quick-region-overlay"
    "$HOME/.local/libexec/quick-region-overlay" -c 'import time; time.sleep(30)' & probe_pid=$!
    trap 'kill "$probe_pid" 2>/dev/null || true; wait "$probe_pid" 2>/dev/null || true' EXIT
    systemctl() { printf '%s\n' "$probe_pid"; }
    wait_for 30 daemon_executable_current || exit 1
    kill -0 "$probe_pid" || exit 1
    install -m755 /usr/bin/python3 "$HOME/.local/libexec/replacement"
    mv "$HOME/.local/libexec/replacement" "$HOME/.local/libexec/quick-region-overlay"
    kill -0 "$probe_pid" || exit 1
    if daemon_executable_current; then echo 'FAIL: a deleted executable was considered current'; exit 1; fi
) || { echo 'FAIL: detecting an interrupted binary replacement'; failures=$((failures + 1)); }
check 'the fast backup is complete' 0 check_backup spectacle-fast
spectacle_fast_backup=$HOME/missing; check 'a missing backup is refused' 1 check_backup spectacle-fast

QUICK_REGION_TEST_FAIL=after-files; check 'the failure hook fires at its step' 1 fail_point after-files
check 'and only there' 0 fail_point after-handover
QUICK_REGION_TEST_FAIL=after-spectacle-empty; check 'the empty-action failure hook fires' 1 fail_point after-spectacle-empty
unset QUICK_REGION_TEST_FAIL; check 'unset, it does nothing' 0 fail_point after-files

[[ $failures == 0 ]] && echo 'script helpers: all passed'
exit $((failures > 0))

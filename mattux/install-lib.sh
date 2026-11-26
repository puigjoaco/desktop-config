# shellcheck shell=bash
state_dir="$HOME/.local/state/mattux"
manifest="$state_dir/manifest"
files=(.local/libexec/mattux .local/share/icons/hicolor/22x22/apps/mattux.svg .local/share/icons/hicolor/16x16/apps/mattux.svg .local/share/applications/mattux.desktop .config/systemd/user/mattux.service)
fail() { echo "$*" >&2; exit 1; }
take_lock() {
    mkdir -p -- "$state_dir"
    exec {lock_fd}>"$state_dir/lock"
    flock -n "$lock_fd" || fail 'Another Mattux install or rollback is running.'
}
check_files() {
    local relative expected actual
    for relative in "${files[@]}"; do
        [[ ! -L "$HOME/$relative" ]] || fail "Refusing to replace a symlink: $HOME/$relative"
        [[ -e "$HOME/$relative" ]] || continue
        [[ -f "$HOME/$relative" && -f "$manifest" ]] || fail "Unmanaged file: $HOME/$relative"
        expected=$(awk -v path="$relative" '$2 == path {print $1}' "$manifest")
        actual=$(sha256sum -- "$HOME/$relative"); actual=${actual%% *}
        [[ -n $expected && $actual == "$expected" ]] || fail "File changed since install: $HOME/$relative. Preserve it elsewhere before rerunning."
    done
}
record_files() {
    local relative digest
    : >"$manifest.tmp"
    for relative in "${files[@]}"; do
        digest=$(sha256sum -- "$HOME/$relative")
        printf '%s  %s\n' "${digest%% *}" "$relative" >>"$manifest.tmp"
    done
    mv -- "$manifest.tmp" "$manifest"
}

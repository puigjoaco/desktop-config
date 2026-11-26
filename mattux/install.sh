#!/bin/bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=install-lib.sh
source "$source_dir/install-lib.sh"
apt_line='sudo apt install build-essential cmake qt6-base-dev libkf6statusnotifieritem-dev libkf6globalaccel-dev libkf6xmlgui-dev libkf6config-dev libkf6widgetsaddons-dev libkf6guiaddons-dev systemd util-linux coreutils'
for tool in cmake c++ make dpkg-query systemctl flock sha256sum awk install mktemp sed cmp; do
    command -v "$tool" >/dev/null || fail "Missing $tool. Install dependencies with: $apt_line"
done
for package in qt6-base-dev libkf6statusnotifieritem-dev libkf6globalaccel-dev libkf6xmlgui-dev libkf6config-dev libkf6widgetsaddons-dev libkf6guiaddons-dev; do
    [[ $(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null) == installed ]] || fail "Missing $package. Install dependencies with: $apt_line"
done
take_lock
check_files
scratch="${XDG_CACHE_HOME:-$HOME/.cache}/mattux"
mkdir -p -- "$scratch"
work=$(mktemp -d "$scratch/mattux-install.XXXXXX")
trap 'rm -rf -- "${work:?}"' EXIT
cmake -S "$source_dir" -B "$work/build" -DCMAKE_BUILD_TYPE=Release || fail "Could not configure Mattux. Install dependencies with: $apt_line"
# heavy (optional) queues builds behind other heavy jobs on machines that have it.
if command -v heavy >/dev/null; then
    heavy cmake --build "$work/build" --parallel 2 --target mattux
else
    cmake --build "$work/build" --parallel 2 --target mattux
fi
check_files
systemctl --user stop mattux.service 2>/dev/null || true
sed "s|@HOME@|$HOME|g" "$source_dir/mattux.desktop" >"$work/mattux.desktop"
install -Dm755 "$work/build/mattux" "$HOME/${files[0]}"
install -Dm644 "$source_dir/icons/mattux.svg" "$HOME/${files[1]}"
install -Dm644 "$source_dir/icons/mattux-16.svg" "$HOME/${files[2]}"
install -Dm644 "$work/mattux.desktop" "$HOME/${files[3]}"
install -Dm644 "$source_dir/mattux.service" "$HOME/${files[4]}"
record_files
systemctl --user daemon-reload
systemctl --user enable --now mattux.service
printf 'Installed Mattux (PID %s). Roll back with: %s/rollback.sh\n' "$(systemctl --user show -P MainPID mattux.service)" "$source_dir"

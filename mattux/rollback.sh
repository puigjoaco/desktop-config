#!/bin/bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=install-lib.sh
source "$source_dir/install-lib.sh"
take_lock
check_files
if [[ -e "$HOME/.config/systemd/user/mattux.service" ]]; then systemctl --user disable --now mattux.service; fi
for relative in "${files[@]}"; do rm -f -- "$HOME/$relative"; done
systemctl --user daemon-reload
rm -f -- "$manifest"
echo 'Removed Mattux. Screenshot shortcuts, stack and settings are unchanged.'

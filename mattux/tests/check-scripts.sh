#!/bin/bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
for script in install.sh rollback.sh install-lib.sh; do bash -n "$source_dir/$script"; done
if command -v shellcheck >/dev/null; then shellcheck -x -P SCRIPTDIR "$source_dir/install.sh" "$source_dir/rollback.sh" "$source_dir/install-lib.sh"; fi

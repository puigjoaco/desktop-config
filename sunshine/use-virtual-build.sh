#!/usr/bin/env bash
# Install the already-tested executable alongside the official package.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $(id -u) == 1000 && $(id -un) == joaquin ]] || exit 1
binary=${1:-"$source_dir/../../Sunshine/cmake-build-kwin/sunshine"}
[[ -x $binary ]] || { echo "Missing built Sunshine: $binary" >&2; exit 1; }
guardian="$source_dir/../../Sunshine/src_assets/linux/assets/kwin-exclusive.py"
output_tool="$(dirname -- "$binary")/sunshine-kwin-output"
[[ -f $guardian ]] || { echo 'Missing exclusive-output guardian' >&2; exit 1; }
[[ -x $output_tool ]] || { echo 'Missing built KWin replication helper' >&2; exit 1; }
built_web="$(dirname -- "$binary")/assets/web"
[[ -f $built_web/config.html ]] || { echo 'Missing built Web UI; build target web-ui first' >&2; exit 1; }
# Fail before activation if the independent recovery helper cannot start.
/usr/bin/python3 -c 'import dbus; from gi.repository import GLib'
command -v kscreen-doctor >/dev/null
command -v qdbus6 >/dev/null
sudo -n install -d -m 0755 /opt/sunshine-puig/bin /opt/sunshine-puig/share/sunshine
# Assets from the official package match the source tag used for this build.
# Keep a private copy so rollback never relies on modified package files.
[[ $(dpkg-query -W -f='${Version}' sunshine) == 2026.914.233613-1+ubuntu26.04 ]] || { echo 'Expected pinned official assets' >&2; exit 1; }
sudo -n cp -a -- /usr/share/sunshine/. /opt/sunshine-puig/share/sunshine/
# Overlay the tested fork UI, including its Linux virtual-display settings.
sudo -n cp -a -- "$built_web/." /opt/sunshine-puig/share/sunshine/web/
sudo -n install -m 0755 "$binary" /opt/sunshine-puig/bin/sunshine
sudo -n install -m 0644 "$guardian" /opt/sunshine-puig/share/sunshine/kwin-exclusive.py
sudo -n install -m 0755 "$output_tool" /opt/sunshine-puig/share/sunshine/sunshine-kwin-output
sudo -n setcap cap_sys_admin,cap_sys_nice+p /opt/sunshine-puig/bin/sunshine
install -d -m 0755 "$HOME/.local/share/applications"
install -m 0644 "$source_dir/sunshine-puig.kwin.desktop" "$HOME/.local/share/applications/dev.lizardbyte.app.Sunshine.kwin.puig.desktop"
# Preserve the official configuration and pairing state for a one-command rollback.
python3 - <<'PY'
from pathlib import Path
p=Path.home()/'.config/sunshine'
s=p.joinpath('sunshine.conf').read_text()
s='\n'.join(line for line in s.splitlines() if not line.lstrip().startswith(('virtual_display =','virtual_display_scale =','virtual_display_mode =','virtual_display_restore_delay =')))
p.joinpath('sunshine-virtual.conf').write_text(s+'\nvirtual_display = enabled\nvirtual_display_scale = 150\nvirtual_display_mode = exclusive\nvirtual_display_restore_delay = 5\n')
p.joinpath('sunshine-virtual.conf').chmod(0o600)
PY
sudo -n install -d -m 0755 /etc/systemd/system/sunshine-host.service.d
sudo -n install -m 0644 "$source_dir/sunshine-virtual-build.conf" /etc/systemd/system/sunshine-host.service.d/90-virtual-build.conf
sudo -n systemctl daemon-reload
sudo -n systemctl restart sunshine-host.service
systemctl --no-pager --full status sunshine-host.service

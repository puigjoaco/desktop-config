#!/usr/bin/env bash
# Reproduce this host's configuration without touching SDDM or the desktop.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $(id -un) == joaquin && $(id -u) == 1000 ]] || { echo 'This configuration targets joaquin (UID 1000).' >&2; exit 1; }
source /etc/os-release
[[ $ID == ubuntu && $VERSION_ID == 26.04 && $(dpkg --print-architecture) == amd64 ]] || { echo 'Expected Ubuntu 26.04 amd64.' >&2; exit 1; }
sudo -n true
umask 077
scratch="$HOME/.cache/agent-scratch/sunshine"
mkdir -p "$scratch/downloads"
download=$(mktemp -d "$scratch/downloads/install.XXXXXXXX")
trap 'rm -rf -- "$download"' EXIT
package="$download/sunshine.deb"
curl --fail --location --proto '=https' --tlsv1.2 \
  'https://github.com/SunshineStream/Sunshine/releases/download/v2026.914.233613/sunshine_2026.914.233613-1%2Bubuntu26.04_amd64.deb' \
  --output "$package"
printf '%s  %s\n' b8a8ceeeb2b3f30a18a994ad268c94e0c91170f33a864c7aad31eed927ad7d47 "$package" | sha256sum --check
sudo -n apt-get install -y "$package"
install -d -m 0700 "$HOME/.config/sunshine" "$HOME/.config/sunshine-admin"
install -m 0600 "$root/sunshine.conf" "$HOME/.config/sunshine/sunshine.conf"
install -m 0600 "$root/apps.json" "$HOME/.config/sunshine/apps.json"
python3 - <<'PY'
import hashlib, json, os, pathlib, secrets, string
os.umask(0o077)
home = pathlib.Path.home()
plain = home / '.config/sunshine-admin/admin.json'
hashed = home / '.config/sunshine/web-credentials.json'
if plain.exists() != hashed.exists():
    raise SystemExit('Incomplete credential setup; refusing to overwrite either file.')
if not plain.exists():
    password = secrets.token_urlsafe(36)
    salt = ''.join(secrets.choice(string.ascii_letters + string.digits) for _ in range(16))
    plain.write_text(json.dumps({'username': 'admin', 'password': password}) + '\n')
    # Matches the pinned upstream httpcommon.cpp and util::hex implementation.
    digest = hashlib.sha256((password + salt).encode()).digest()[::-1].hex().upper()
    hashed.write_text(json.dumps({'username': 'admin', 'salt': salt, 'password': digest}) + '\n')
for path in [plain, hashed]:
    path.chmod(0o600)
PY
install -d -m 0755 "$HOME/.local/libexec/sunshine"
install -m 0755 "$root/admin.py" "$HOME/.local/libexec/sunshine/admin.py"
install -m 0755 "$root/display-mode.py" "$HOME/.local/libexec/sunshine/display-mode.py"
# Linger already existed on this host; preserve it and ensure boot-time audio.
sudo -n loginctl enable-linger joaquin
systemctl --user mask --now app-dev.lizardbyte.app.Sunshine.service
sudo -n install -m 0644 "$root/sunshine-host.service" /etc/systemd/system/sunshine-host.service
sudo -n systemctl daemon-reload
sudo -n systemctl enable sunshine-host.service
sudo -n systemctl restart sunshine-host.service
echo 'Sunshine installed. Admin password: ~/.config/sunshine-admin/admin.json (0600; never print it in chat).'

#!/usr/bin/env bash
# Keep the original binary, config and pairing state intact.
set -euo pipefail
sudo -n rm -f -- /etc/systemd/system/sunshine-host.service.d/90-virtual-build.conf
sudo -n systemctl daemon-reload
sudo -n systemctl restart sunshine-host.service
systemctl --no-pager --full status sunshine-host.service

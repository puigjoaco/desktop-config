#!/usr/bin/env bash
# Keep the original binary, config and pairing state intact.
set -euo pipefail
helper=/opt/sunshine-puig/share/sunshine/kwin-exclusive.py
restore_status=0
# Stop while the drop-in is still loaded so its ExecStopPost recovery hook runs.
sudo -n systemctl stop sunshine-host.service
if [[ -f "$helper" ]]; then
  /usr/bin/python3 "$helper" restore || restore_status=$?
  if (( restore_status != 0 )); then
    echo "warning: monitor restore incomplete (exit $restore_status); recovery state kept, continuing rollback" >&2
  fi
fi
sudo -n rm -f -- /etc/systemd/system/sunshine-host.service.d/90-virtual-build.conf
sudo -n systemctl daemon-reload
sudo -n systemctl reset-failed sunshine-host.service || true
sudo -n systemctl start sunshine-host.service
systemctl --no-pager --full status sunshine-host.service
exit "$restore_status"

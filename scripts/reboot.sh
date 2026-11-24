#!/bin/sh
# reboot.sh - reboot the head unit the documented vendor way:
#   OnOffDSICommander appreset   (application reset: the app/HMI layer restarts,
#   SSH drops, and the most-shim autostart brings evo_launcher back on the way up).
# Gentler than a full kernel reboot (projects/evo_reboot = QNX sysmgr_reboot).
#
#   ./scripts/reboot.sh            # appreset (default)
#   ./scripts/reboot.sh --kernel   # full kernel reboot via evo_reboot instead
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/evo_env.sh"

if [ "${1:-}" = "--kernel" ]; then
  echo "firing full kernel reboot (evo_reboot / sysmgr_reboot) on $EVO_USER@$EVO_HOST ..."
  evo_ssh '[ -x /fs/sda1/evo/evo_reboot ] && /fs/sda1/evo/evo_reboot || echo "evo_reboot not deployed"' || true
else
  echo "firing OnOffDSICommander appreset on $EVO_USER@$EVO_HOST (SSH will drop)..."
  evo_ssh '
    for b in /opt/core/bin/OnOffDSICommander /opt/car/bin/OnOffDSICommander /fs/sda0/opt/core/bin/OnOffDSICommander; do
      [ -x "$b" ] && { LD_LIBRARY_PATH=/opt/sys/lib:/opt/core/lib:/opt/car/lib:/opt/os/lib:/usr/lib:/lib "$b" appreset; exit 0; }
    done
    echo "OnOffDSICommander not found on the unit" >&2; exit 1
  ' || true
fi
echo "restart requested; the unit comes back in ~1-2 min (autostart relaunches the launcher)."

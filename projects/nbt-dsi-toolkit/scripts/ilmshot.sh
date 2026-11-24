#!/bin/bash
# ilmshot.sh - capture the TRUE panel composite (our ILM overlay layers OVER the live HMI) and
# fetch it to the host as a PNG. Unlike navshot.sh/evo_screenshot (QNX screen_read_display, which
# reads ONLY the OEM HMI framebuffer and is BLIND to our ILM layers 9000/9001), this uses the GENIVI
# LayerManager's own screenshot (ilm_takeScreenshot via LayerManagerControl `dump`), which reads the
# real ILM/panel composite = exactly what the user sees on the screen.
#   usage: ilmshot.sh <out.png> [screen 0 | layer <id> | surface <id>]
#     default target = "screen 0" (full panel). "layer 9001"=launcher card, "layer 9000"=a launched app.
# Writes <out.png> on the host. Needs connect.sh sourced (HU/hu/hus env), like navshot.sh.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$HERE/../../doom_evo/tools/connect.sh" >/dev/null 2>&1
OUTPNG="$1"; shift
TARGET="${*:-screen 0}"
TMP="$(dirname "$OUTPNG")/.ilmshot.bmp"
REMOTE=/fs/sda1/evo/.ilmshot.bmp
hu "LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib /opt/sys/bin/LayerManagerControl dump $TARGET to $REMOTE 2>&1 | tail -1" 2>&1 | grep -v "Permanently added"
for i in 1 2 3 4 5; do
  hus "root@$HU:$REMOTE" "$TMP" >/dev/null 2>&1
  [ "$(stat -c%s "$TMP" 2>/dev/null || echo 0)" -gt 3000000 ] && break
  sleep 1
done
hu "rm -f $REMOTE" >/dev/null 2>&1
python3 - "$TMP" "$OUTPNG" <<'PY'
import sys
from PIL import Image
# LayerManagerControl dump = BMP (54-byte header, 1440x540 BGRA, bottom-up). PIL reads it directly.
Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])
print("saved %s" % sys.argv[2])
PY

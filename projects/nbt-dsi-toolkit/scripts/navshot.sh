#!/bin/bash
# navshot.sh - run an evo_navctl command on the unit, then grab + fetch an HMI screenshot.
#   usage: navshot.sh <out.png> [evo_navctl args...]
# Writes <out.png> on the host. Needs connect.sh sourced (HU/hu/hus) env.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$HERE/../../doom_evo/tools/connect.sh" >/dev/null 2>&1
OUTPNG="$1"; shift
TMP="$(dirname "$OUTPNG")/.navshot.raw"
if [ $# -gt 0 ]; then
  hu "NAVCTL_IFVER=31 LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib /fs/sda1/evo/evo_navctl $* 2>&1 | tail -2" 2>&1 | grep -v "Permanently added"
fi
hu 'sleep 1; /fs/sda1/evo/evo_screenshot /fs/sda1/evo/hmi.raw 0 2>&1 | tail -1' 2>&1 | grep -v "Permanently added"
for i in 1 2 3 4 5; do
  hus "root@$HU:/fs/sda1/evo/hmi.raw" "$TMP" >/dev/null 2>&1
  [ "$(stat -c%s "$TMP" 2>/dev/null || echo 0)" = "3110416" ] && break
done
python3 - "$TMP" "$OUTPNG" <<'PY'
import struct, sys
d=open(sys.argv[1],"rb").read(); m,w,h,bpp=struct.unpack_from('<4sIII',d,0)
from PIL import Image
im=Image.frombytes('RGBA',(w,h),d[16:16+w*h*4]); b,g,r,a=im.split()  # framebuffer is BGRX
Image.merge('RGB',(r,g,b)).save(sys.argv[2]); print("saved %s %dx%d"%(sys.argv[2],w,h))
PY

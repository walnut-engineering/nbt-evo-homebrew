#!/bin/sh
# Launch the EVO cursor overlay on the head unit.
# Usage: sh cursor_launch.sh            # live mouse mode (move the USB mouse)
#        sh cursor_launch.sh -anim 20   # self-animated demo, no mouse needed
#        sh cursor_launch.sh -probe 15  # compositing test pattern
cd /var/run/screen_tests
export HOME=/dev/shmem
export LD_LIBRARY_PATH=/opt/sys/lib:/opt/mm/lib:/usr/lib:/lib

# Make sure the USB mouse driver is up (so /dev/usbmouse0 exists).
pidin -p devu-mouse 2>/dev/null | grep -q devu-mouse || devu-mouse 2>/dev/null &

./evo_cursor "$@"

# The overlay never hides the HMI, so there is nothing to restore; just report.
echo "[cursor_launch] evo_cursor exited"

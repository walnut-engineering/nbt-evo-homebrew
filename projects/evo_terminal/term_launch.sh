#!/bin/sh
# Launch the EVO terminal emulator on the head unit.
# Usage: sh term_launch.sh [-demo]
cd /var/run/screen_tests
export HOME=/dev/shmem
export LD_LIBRARY_PATH=/opt/sys/lib:/opt/mm/lib:/usr/lib:/lib

# Make sure the USB keyboard driver is up (so /dev/usbkbd0 exists).
pidin -p devu-kbd 2>/dev/null | grep -q devu-kbd || devu-kbd 2>/dev/null &

./evo_terminal "$@"

# Safety net: if the terminal exited without restoring the car HMI, put it back.
export LD_LIBRARY_PATH=/opt/sys/lib
LayerManagerControl set surface 2000 visibility 0 >/dev/null 2>&1
LayerManagerControl set layer 2147483648 visibility 1 >/dev/null 2>&1
echo "[term_launch] terminal exited; HMI visibility restored"

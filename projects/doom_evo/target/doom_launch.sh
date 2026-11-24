#!/bin/sh
# Launch doom_evo on the EVO head unit and return to the car HMI when it exits.
# Copy next to doom_evo (deploy dir /var/run/screen_tests) and run:
#   sh doom_launch.sh -warp 1 1
# The game itself hides the HMI layers while it runs and restores them on a
# clean exit (menu quit, SIGTERM, SIGINT). The lines after the game are a
# safety net for a crash or SIGKILL.
DIR=/var/run/screen_tests
HMI_LAYER=2147483648
DOOM_LAYER=9000
LMC=/opt/sys/bin/LayerManagerControl
export LD_LIBRARY_PATH="/opt/mm/lib/libasound:/opt/mm/lib:/fs/sda0/opt/hmi/ID5/lib:/fs/sda0/opt/conn/lib/browser:/fs/sda0/lib/graphics/omap5430_ES2_0:/fs/sda0/opt/sys/lib:/opt/sys/lib:/usr/lib:/lib"
export PATH="/opt/sys/bin:/usr/bin:/bin:/sbin:/usr/sbin:$PATH"
cd "$DIR" || exit 1

# USB keyboard and mouse drivers create /dev/usbkbd0 and /dev/usbmouse0
if [ "$(pidin 2>/dev/null | grep -c devu-kbd)" -eq 0 ]; then devu-kbd & fi
if [ "$(pidin 2>/dev/null | grep -c devu-mouse)" -eq 0 ]; then devu-mouse & fi

# A previous instance that was killed (not quit) never released its audio route;
# a fresh build on a stale route is silent. Release it before the game builds it.
# Generation lock: a launcher that was superseded (its game slain by a newer launch)
# must NOT run the safety net below, or it destroys the NEW instance's surface/layer.
GEN=$$; echo $GEN > doom_launch.gen
if slay -f doom_evo >/dev/null 2>&1; then sleep 2; fi
/opt/mm/bin/audioconn -r 0xb8 >/dev/null 2>&1
./doom_evo "$@" > doom_launch_stdout.log 2> doom_launch_stderr.log
RC=$?
if [ "$(cat doom_launch.gen 2>/dev/null)" != "$GEN" ]; then echo "superseded by a newer launch; leaving the display alone"; exit $RC; fi

# Safety net: make sure nothing of ours is left and the HMI is back
$LMC destroy surface 2000 >/dev/null 2>&1
$LMC destroy layer $DOOM_LAYER >/dev/null 2>&1
$LMC set layer $HMI_LAYER visibility 1 >/dev/null 2>&1
echo "doom_evo exited with status $RC"
exit $RC

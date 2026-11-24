#!/bin/sh
# doom_launch_sda1.sh - persistent DOOM launcher for the homebrew launcher tile.
#
# Deployed to /fs/sda1/evo/doom_launch.sh and referenced by launcher.manifest
# (DOOM|/fs/sda1/evo/doom_launch.sh|...).  The original doom_launch.sh lived in
# /var/run/screen_tests (tmpfs) and vanished on every reboot, so the DOOM tile
# no-op'd after a power cycle.  This copy, its doom_evo binary and doom1.wad all
# live on /fs/sda1 so the tile works from a cold boot.
#
# The launcher spawns us with no args; DOOM finds doom1.wad (shareware E1) in cwd.
# DOOM itself hides the HMI while running and restores it on a clean exit; it
# quits to the launcher on an iDrive MENU hold (~1 s).  The LayerManagerControl
# lines are a crash/SIGKILL safety net.  Logs go to /dev/shmem (RAM, not flash).
DIR=/fs/sda1/evo
HMI_LAYER=2147483648
DOOM_LAYER=9000
LMC=/opt/sys/bin/LayerManagerControl
export LD_LIBRARY_PATH="/opt/mm/lib/libasound:/opt/mm/lib:/fs/sda0/opt/hmi/ID5/lib:/fs/sda0/opt/conn/lib/browser:/fs/sda0/lib/graphics/omap5430_ES2_0:/fs/sda0/opt/sys/lib:/opt/sys/lib:/usr/lib:/lib"
export PATH="/opt/sys/bin:/usr/bin:/bin:/sbin:/usr/sbin:$PATH"
# Audio master-SFX gain (percent, see i_sound_evo.c DOOM_AUDIO_GAIN; 32x cap). Tunable WITHOUT a
# rebuild: write a new value to /fs/sda1/evo/doom_gain and relaunch.  Default 1200 = 12x.
export DOOM_AUDIO_GAIN="$(cat /fs/sda1/evo/doom_gain 2>/dev/null || echo 600)"   # 6x, mic-calibrated to OEM@50%
cd "$DIR" || exit 1

# USB keyboard is optional (DOOM is driven by the iDrive knob); start it if idle.
if [ "$(pidin 2>/dev/null | grep -c devu-kbd)" -eq 0 ]; then devu-kbd & fi

# Generation lock: a superseded launch must not run the safety net (it would
# destroy a newer instance's surface). A killed previous instance never released
# its audio route, so release it before this build grabs it (else silent).
GEN=$$; echo $GEN > doom_launch.gen
if slay -f doom_evo >/dev/null 2>&1; then sleep 2; fi
/opt/mm/bin/audioconn -r 0xb8 >/dev/null 2>&1

./doom_evo "$@" > /dev/shmem/doom_launch_stdout.log 2> /dev/shmem/doom_launch_stderr.log
RC=$?
if [ "$(cat doom_launch.gen 2>/dev/null)" != "$GEN" ]; then echo "superseded by a newer launch"; exit $RC; fi

# Safety net for a crash: make sure nothing of ours is left and the HMI is back.
$LMC destroy surface 2000 >/dev/null 2>&1
$LMC destroy layer $DOOM_LAYER >/dev/null 2>&1
$LMC set layer $HMI_LAYER visibility 1 >/dev/null 2>&1
echo "doom_evo exited with status $RC"
exit $RC

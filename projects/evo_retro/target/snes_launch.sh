#!/bin/sh
# snes_launch.sh - homebrew launcher tile entry for the SNES emulator (evo_snes, see
# projects/evo_retro).
#
# Deployed to /fs/sda1/evo/snes_launch.sh and referenced by launcher.manifest. The launcher
# spawns us with no args; the binary is /fs/sda1/evo/evo_snes and your own ROM files
# go in /fs/sda1/evo/snes/.
#
# Audio: LD_LIBRARY_PATH must list /opt/mm/lib/libasound FIRST - the HARMAN
# libasound that resolves the preferred PLAYBACK device; the generic
# /lib/libasound.so.2 only sees the capture node (same lesson as DOOM / Quake).
# The LayerManagerControl lines are a crash safety net (restore the HMI layer).
E=/fs/sda1/evo
HMI_LAYER=2147483648
LMC=/opt/sys/bin/LayerManagerControl
export LD_LIBRARY_PATH="/opt/mm/lib/libasound:/opt/mm/lib:/fs/sda0/opt/hmi/ID5/lib:/fs/sda0/opt/conn/lib/browser:/fs/sda0/lib/graphics/omap5430_ES2_0:/fs/sda0/opt/sys/lib:/opt/sys/lib:/usr/lib:/lib"
export PATH="/opt/sys/bin:/usr/bin:/bin:/sbin:/usr/sbin:$PATH"
# Master audio gain (percent). The unit attenuates the 0xb8 browser source, so a
# boost keeps it audible. Tunable WITHOUT a rebuild: echo 400 > /fs/sda1/evo/snes_gain
export EVO_SNES_AUDIO_GAIN="$(cat $E/snes_gain 2>/dev/null || echo 300)"
cd "$E" || exit 1
mkdir -p "$E/snes" "$E/snes/saves"

# USB keyboard is optional (a gamepad or the iDrive knob drive it); start the driver if idle.
if [ "$(pidin 2>/dev/null | grep -c devu-kbd)" -eq 0 ]; then devu-kbd & fi

# One instance; a killed previous run never released its audio route.
if slay -f evo_snes >/dev/null 2>&1; then sleep 1; fi
/opt/mm/bin/audioconn -r 0xb8 >/dev/null 2>&1

./evo_snes "$@" > /dev/shmem/snes_launch_stdout.log 2> /dev/shmem/snes_launch_stderr.log
RC=$?

# Safety net for a crash: HMI and the launcher's own overlay (9001, hidden by our
# presentation mode) visible again, audio route released. (Only ever SET visibility
# on 9001 - never destroy it: it is the running launcher's permanent layer.)
$LMC set layer $HMI_LAYER visibility 1 >/dev/null 2>&1
$LMC set layer 9001 visibility 1 >/dev/null 2>&1
/opt/mm/bin/audioconn -r 0xb8 >/dev/null 2>&1
echo "evo_snes exited with status $RC"
exit $RC

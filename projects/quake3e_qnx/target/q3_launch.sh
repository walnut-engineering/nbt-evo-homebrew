#!/bin/sh
# q3_launch.sh - homebrew launcher tile entry for Quake 3 (quake3e) on the CID.
#
# Deployed to /fs/sda1/evo/q3_launch.sh and referenced by launcher.manifest
# (Quake 3|/fs/sda1/evo/q3_launch.sh|.../quake.rgba). Modeled on DOOM's
# doom_launch_sda1.sh: everything the game needs lives on /fs/sda1 so the tile
# works from a cold boot; the launcher spawns us with no args.
#
# The binary + game data live in /fs/sda1/evo/q3/ (quake3e.arm + demoq3/pak0.pk3
# + the native cgame/qagame/ui .so's). quake3e opens its own GLES2 window on CID
# layer 9400 (over the live HMI) and removes it on a clean exit; the
# LayerManagerControl lines below are a crash/SIGKILL safety net. iDrive BACK
# quits the engine to the launcher. Logs go to /dev/shmem (RAM, not flash).
Q3DIR=/fs/sda1/evo/q3
HMI_LAYER=2147483648
Q3_LAYER=9400
Q3_SURFACE=2400
LMC=/opt/sys/bin/LayerManagerControl

# GPU: GRAPHICS_ROOT must be the dir with graphics.conf (docs/gpu-gles2-breakthrough.md).
# Audio: LD_LIBRARY_PATH must include /opt/mm/lib/libasound FIRST - the HARMAN libasound
# that resolves the preferred PLAYBACK device /dev/libasound/dev1-1; the generic
# /lib/libasound.so.2 only sees the capture node => snd_pcm_open_preferred ENOENT.
export GRAPHICS_ROOT=/lib/graphics/omap5430
export LD_LIBRARY_PATH="/lib/graphics/omap5430:/opt/mm/lib/libasound:/opt/mm/lib:/opt/sys/lib:/lib:/usr/lib:/fs/sda0/lib"
export PATH="/opt/sys/bin:/usr/bin:/bin:/sbin:/usr/sbin:$PATH"

# Master audio gain (percent; qnx_snd.c EVO_Q3_AUDIO_GAIN, 0.1x..8x). The HU
# attenuates the 0xb8 browser source, so a boost keeps us audible. Tunable
# WITHOUT a rebuild: write a new value to /fs/sda1/evo/q3_gain and relaunch.
export EVO_Q3_AUDIO_GAIN="$(cat /fs/sda1/evo/q3_gain 2>/dev/null || echo 300)"

# Optional per-tile launch args (map/bots/cvars). Override WITHOUT editing this
# script: put a line in /fs/sda1/evo/q3_args. Default = an immediately-playable
# q3dm7 deathmatch vs two bots (the verified render+input+sound path).
Q3_ARGS="$(cat /fs/sda1/evo/q3_args 2>/dev/null)"
[ -n "$Q3_ARGS" ] || Q3_ARGS="+set bot_enable 1 +devmap q3dm7 +addbot sarge 2 +addbot grunt 2"

cd "$Q3DIR" || exit 1

# USB keyboard is optional (Quake is driven by the iDrive knob); start it if idle.
if [ "$(pidin 2>/dev/null | grep -c devu-kbd)" -eq 0 ]; then devu-kbd & fi

# Generation lock: a superseded launch must not run the safety net (it would
# tear down a newer instance's surface). A killed previous instance never
# released its audio route, so release it before this build grabs it.
GEN=$$; echo $GEN > q3_launch.gen
if slay -f quake3e.arm >/dev/null 2>&1; then sleep 2; fi
/opt/mm/bin/audioconn -r 0xb8 >/dev/null 2>&1

./quake3e.arm +set fs_basepath "$Q3DIR" +set fs_game demoq3 +set sv_pure 0 \
  +set com_hunkMegs 64 +set s_volume 1 +set s_musicvolume 0.5 $Q3_ARGS \
  > /dev/shmem/q3_launch_stdout.log 2> /dev/shmem/q3_launch_stderr.log
RC=$?
if [ "$(cat q3_launch.gen 2>/dev/null)" != "$GEN" ]; then echo "superseded by a newer launch"; exit $RC; fi

# Safety net for a crash: make sure nothing of ours is left on screen and the
# HMI layer is visible again.
$LMC destroy surface $Q3_SURFACE >/dev/null 2>&1
$LMC destroy layer $Q3_LAYER >/dev/null 2>&1
$LMC set layer $HMI_LAYER visibility 1 >/dev/null 2>&1
/opt/mm/bin/audioconn -r 0xb8 >/dev/null 2>&1
echo "quake3e.arm exited with status $RC"
exit $RC

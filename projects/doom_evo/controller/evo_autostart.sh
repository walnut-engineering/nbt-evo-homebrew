#!/bin/sh
# evo_autostart.sh - boot supervisor for the homebrew launcher.
#
# Started at boot by the most shim (/opt/sys/bin/most = evo_most_shim), which
# forks + setsid()s + execs `sh /fs/sda1/evo/evo_autostart.sh` before it execs the
# real most. Also runnable by hand:  sh /fs/sda1/evo/evo_autostart.sh
#
#   evo_launcher -daemon  resident homebrew launcher: watches the iDrive knob feed,
#                   summons a full-screen tile page on an up/down tilt, and spawns the
#                   selected app directly (no CarLife MOST device, so no 0x2471
#                   NWM duplicate-registration - the fault the old evo_doom_tile caused).
#
# Kill switch: touch /fs/sda1/evo/autostart.off  (checked at boot; also stops the
# supervisor loops at their next restart). Stop a running set with:
#   touch /fs/sda1/evo/autostart.off; slay -f evo_doom_tile; slay -f evo_applaunchd
# Logs: /fs/sda1/evo/autostart.log (small, truncated per boot) and the daemon
# stdout in RAM at /dev/shmem/evo_doom_tile.log, /dev/shmem/evo_applaunchd.log.
# QNX sh notes: no nohup/setsid/timeout/printf/seq; pidin + grep + sleep + date ok.
E=/fs/sda1/evo
LOG=$E/autostart.log
OFF=$E/autostart.off
[ -f $OFF ] && exit 0
: > $LOG
echo "`date` autostart: begin (pid $$)" >> $LOG

# One supervisor only: a second copy (e.g. run by hand) just exits.
if [ -f /dev/shmem/evo_autostart.pid ]; then
  OLD=`cat /dev/shmem/evo_autostart.pid`
  if [ -n "$OLD" ] && [ -n "`pidin -p $OLD -F %N 2>/dev/null | grep sh`" ]; then
    echo "`date` autostart: already running as pid $OLD, exiting" >> $LOG
    exit 0
  fi
fi
echo $$ > /dev/shmem/evo_autostart.pid

# The daemons are proven against a running HMI: wait for HmiMain3D (<= 5 min),
# then a settle delay for its MOST subsystem bring-up.
n=0
while [ -z "`pidin -F %N 2>/dev/null | grep HmiMain3D`" ]; do
  sleep 2
  n=$((n+1))
  if [ $n -gt 150 ]; then echo "`date` autostart: HMI not seen in 5 min, starting anyway" >> $LOG; break; fi
done
echo "`date` autostart: HMI up after ${n}x2s, settling" >> $LOG
sleep 10
[ -f $OFF ] && { echo "`date` autostart: kill switch appeared, exit" >> $LOG; exit 0; }

export LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib
unset LD_PRELOAD

# Supervise: restart on exit, 5 s apart, at most 20 times (no crash loops).
supervise() {   # $1 = name, $2.. = command
  name=$1; shift
  r=0
  while [ $r -lt 20 ]; do
    [ -f $OFF ] && { echo "`date` $name: kill switch, stop" >> $LOG; return; }
    [ -n "`pidin -F %N 2>/dev/null | grep $name`" ] && { echo "`date` $name: already running, not starting another" >> $LOG; return; }
    echo "`date` $name: start (crash #$r)" >> $LOG
    "$@" >/dev/shmem/$name.log 2>&1 </dev/null
    rc=$?
    echo "`date` $name: exited rc=$rc" >> $LOG
    # Clean exits (rc=0) are intentional - evo_launcher exits after launching an app
    # so it can restart with a fresh/settled ILM state - so they do NOT count toward
    # the crash cap; only non-zero exits (real crashes) do. The 5 s sleep bounds the
    # restart rate either way.
    [ $rc -ne 0 ] && r=$((r+1))
    sleep 5
  done
  echo "`date` $name: gave up after $r crashes" >> $LOG
}

# Summon-gate daemon: tells the launcher whether the OEM HMI is on the main Startmenu, so the
# page only opens from there (not a notification/submenu). The launcher fail-opens if it is absent.
supervise evo_hmistate $E/evo_hmistate -daemon &
supervise evo_launcher $E/evo_launcher -daemon &
wait
echo "`date` autostart: end" >> $LOG

#!/bin/sh
# install.sh - install the NBT EVO Homebrew launcher + apps onto a head unit.
#
# Run this ON THE UNIT (you need a root shell - e.g. ssh in, or run it straight
# off a USB stick that mounts as /fs/sda1). FOR A BENCH / TEST UNIT YOU OWN.
#
#   sh install.sh                 # stage launcher + apps into /fs/sda1/evo (NO system changes)
#   sh install.sh --enable-knob   # ALSO install the iDrive-feed 'most' shim (see below), then reboot
#   sh install.sh --extras        # also copy the extra (non-tile) tools to /fs/sda1/evo/extras
#   sh install.sh --start         # start the launcher now (test; knob needs --enable-knob + reboot)
#
# --enable-knob replaces the system MOST client /opt/sys/bin/most with this
# project's FAIL-SAFE shim (the real binary is preserved as most.real; the shim
# dlopen-tests the tee and execs the clean real 'most' if anything is off). This
# is what delivers iDrive-knob input to the launcher and auto-starts it at boot.
# Reversible: run uninstall.sh, or `touch /fs/sda1/evo/most_tee.off` to disable.
#
# Your own files are never touched: games, saves, settings and the Xbox adapter's
# firmware live in folders this script only creates (nes/ snes/ genesis/ xbox/).
set -eu
SRC=$(cd "$(dirname "$0")" && pwd)
E=/fs/sda1/evo
ICON=$E/icons
MOST=/opt/sys/bin/most

ENABLE_KNOB=0; EXTRAS=0; START=0
for a in "$@"; do
  case "$a" in
    --enable-knob) ENABLE_KNOB=1 ;;
    --extras)      EXTRAS=1 ;;
    --start)       START=1 ;;
    -h|--help)     sed -n '2,19p' "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

echo "== NBT EVO Homebrew installer =="
[ -d "$SRC/payload" ] || { echo "! can't find payload/ next to install.sh (SRC=$SRC)"; exit 1; }

# 1. stage launcher + apps into /fs/sda1/evo (writable data partition) --------
echo "-- staging into $E"
mkdir -p "$E" "$ICON"
# A running program's file cannot be written over, so an older copy of the daemons
# is stopped first and their files go in before anything else. If the boot
# supervisor (evo_autostart.sh) is running it starts them again by itself a few
# seconds later; otherwise use --start, or reboot.
STOPPED=0
for d in evo_launcher evo_hmistate evo_padd; do
  if slay -f $d >/dev/null 2>&1; then STOPPED=1; fi
done
[ "$STOPPED" = 1 ] && { echo "   stopped the running daemons to replace them"; sleep 1; }
BINS="evo_launcher evo_hmistate evo_padd evo_terminal evo_sysmon evo_gps doom_evo evo_nes evo_snes evo_genesis evo_feedtap"
SCRIPTS="doom_launch.sh q3_launch.sh nes_launch.sh snes_launch.sh genesis_launch.sh evo_autostart.sh"
for f in $BINS $SCRIPTS evo_tee_most.so; do
  cp -f "$SRC/payload/$f" "$E/$f"
done
for f in $BINS $SCRIPTS; do chmod +x "$E/$f"; done
for i in cpu gps terminal nes snes genesis; do cp -f "$SRC/payload/icons/$i.rgba" "$ICON/$i.rgba"; done
cp -f "$SRC/payload/launcher.manifest" "$E/launcher.manifest"
# Quake 3: engine + native game modules under q3/ (demo data pak0.pk3 is bring-your-own)
mkdir -p "$E/q3/demoq3"
cp -f "$SRC/payload/q3/quake3e.arm" "$E/q3/quake3e.arm"; chmod +x "$E/q3/quake3e.arm"
for so in qagamearm cgamearm uiarm; do cp -f "$SRC/payload/q3/demoq3/$so.so" "$E/q3/demoq3/$so.so"; done
# the folders your own files go in (left alone if they exist)
mkdir -p "$E/nes" "$E/snes" "$E/genesis" "$E/xbox"
echo "   staged: launcher, hmistate, DOOM, Quake 3, NES, SNES, Genesis, Terminal, System, GPS,"
echo "           Xbox controller daemon, tee, feedtap, manifest, icons"

if [ "$EXTRAS" = 1 ]; then
  mkdir -p "$E/extras"
  for f in "$SRC"/extras/*; do
    [ -f "$f" ] && cp -f "$f" "$E/extras/" && chmod +x "$E/extras/$(basename "$f")"
  done
  echo "   staged extras -> $E/extras"
fi

# 2. optional: install the iDrive-feed 'most' shim ----------------------------
if [ "$ENABLE_KNOB" = 1 ]; then
  echo "-- enabling iDrive-knob feed (installing the fail-safe 'most' shim)"
  [ -f "$MOST" ] || { echo "! $MOST not found - unexpected unit layout; shim NOT installed"; exit 1; }
  if ( : > "$(dirname "$MOST")/.evo_wtest" ) 2>/dev/null; then
    rm -f "$(dirname "$MOST")/.evo_wtest"
  else
    echo "! $(dirname "$MOST") is not writable. Remount it read-write and re-run with --enable-knob."
    exit 1
  fi
  if [ ! -f "$MOST.real" ]; then
    cp -f "$MOST" "$MOST.real"
    echo "   backed up the real MOST client -> $MOST.real"
  else
    echo "   $MOST.real already present (keeping the existing backup)"
  fi
  cp -f "$SRC/most" "$MOST"; chmod +x "$MOST"
  rm -f "$E/most_tee.off" "$E/autostart.off"
  echo "   installed shim as $MOST  (tee: $E/evo_tee_most.so)"
  echo "   >>> REBOOT the unit to activate the knob feed + boot autostart."
else
  echo "-- iDrive-knob feed NOT enabled (no system files were changed)."
  echo "   To summon the launcher with the knob you must enable it:"
  echo "     sh install.sh --enable-knob     (then reboot)"
fi

# 3. optional: start now for testing ------------------------------------------
if [ "$START" = 1 ]; then
  echo "-- starting launcher daemons now (test only; the knob needs the shim + reboot)"
  sleep 2
  LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib; export LD_LIBRARY_PATH
  unset LD_PRELOAD || true
  ( cd "$E" && ./evo_hmistate -daemon >/dev/shmem/evo_hmistate.log 2>&1 & )
  sleep 1
  ( cd "$E" && ./evo_launcher -daemon >/dev/shmem/evo_launcher.log 2>&1 & )
  if [ -f "$E/xbox/FW_ACC_00U.bin" ]; then ( cd "$E" && ./evo_padd -w >/dev/shmem/evo_padd.log 2>&1 & ); fi
  sleep 2
  echo "   running: launcher=$(pidin -F %N 2>/dev/null | grep -c evo_launcher) hmistate=$(pidin -F %N 2>/dev/null | grep -c evo_hmistate) padd=$(pidin -F %N 2>/dev/null | grep -c evo_padd)"
fi

echo
echo "Done. What you add yourself:"
echo "  DOOM      your own doom1.wad (shareware is fine)   -> $E/doom1.wad"
echo "  Quake 3   the free demo's pak0.pk3                 -> $E/q3/demoq3/pak0.pk3"
echo "  NES       your own .nes files                      -> $E/nes/"
echo "  SNES      your own .sfc / .smc files (or .zip)     -> $E/snes/"
echo "  Genesis   your own .md / .gen / .bin / .sms / .gg  -> $E/genesis/"
echo "  Xbox pads the adapter's firmware FW_ACC_00U.bin    -> $E/xbox/   (see INSTALL.md)"
echo "Disable/uninstall: sh uninstall.sh   (quick off switch: touch $E/most_tee.off)"

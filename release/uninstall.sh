#!/bin/sh
# uninstall.sh - disable / remove the NBT EVO Homebrew launcher. Run ON THE UNIT.
#
#   sh uninstall.sh            # restore the real MOST client, stop daemons; keep staged files
#   sh uninstall.sh --purge    # also delete /fs/sda1/evo (your games and saves in it too!)
#
# A reboot afterwards returns the unit fully to stock behaviour.
set -u
E=/fs/sda1/evo
MOST=/opt/sys/bin/most
PURGE=0
for a in "$@"; do case "$a" in --purge) PURGE=1 ;; -h|--help) sed -n '2,8p' "$0"; exit 0 ;; *) echo "unknown: $a" >&2; exit 2 ;; esac; done

echo "== NBT EVO Homebrew uninstall =="

# 1. disarm the tee immediately (fail-safe), then restore the real MOST client
touch "$E/most_tee.off" 2>/dev/null || true
if [ -f "$MOST.real" ]; then
  if ( : > "$(dirname "$MOST")/.evo_wtest" ) 2>/dev/null; then
    rm -f "$(dirname "$MOST")/.evo_wtest"
    mv -f "$MOST.real" "$MOST" && chmod +x "$MOST"
    echo "   restored the real MOST client (shim removed)"
  else
    echo "   ! $(dirname "$MOST") not writable - remount read-write and re-run to restore $MOST"
  fi
else
  echo "   no shim backup found (the shim was not installed) - nothing to restore"
fi

# 2. stop the daemons (the Xbox controller daemon switches its controllers off as it
#    leaves). autostart.off tells the boot supervisor not to start them again;
#    install.sh --enable-knob removes it.
touch "$E/autostart.off" 2>/dev/null || true
slay -f evo_launcher >/dev/null 2>&1 || true
slay -f evo_hmistate >/dev/null 2>&1 || true
slay -f evo_padd >/dev/null 2>&1 || true
echo "   stopped the launcher daemons"

# 3. optional purge of staged files
if [ "$PURGE" = 1 ]; then
  rm -rf "$E" && echo "   removed $E (including any games, saves and firmware you had put there)"
else
  echo "   kept staged files in $E (pass --purge to delete them)"
fi

echo "Done. Reboot to return the unit fully to stock."

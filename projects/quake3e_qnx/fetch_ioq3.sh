#!/bin/sh
# fetch_ioq3.sh - clone ioquake3 (into ../ioq3, gitignored) and apply our ABI
# patches. ioq3 supplies the GPL baseq3 game logic (game/cgame/q3_ui) that
# quake3e omits; build_game.sh compiles it into native QNX modules (M5).
# Re-runnable: re-applies patches to a fresh clone.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../ioq3"
REPO="${IOQ3_REPO:-https://github.com/ioquake/ioq3}"

if [ ! -d "$SRC/.git" ]; then
  echo "cloning ioquake3 -> $SRC"
  git clone --depth 1 "$REPO" "$SRC"
else
  echo "ioquake3 already present at $SRC"
fi

for p in "$HERE"/ioq3_patches/*.patch; do
  [ -f "$p" ] || continue
  if git -C "$SRC" apply --check "$p" 2>/dev/null; then
    git -C "$SRC" apply "$p" && echo "applied $(basename "$p")"
  else
    echo "skip $(basename "$p") (already applied or conflicts)"
  fi
done
echo "done. ioq3 source ready at $SRC"

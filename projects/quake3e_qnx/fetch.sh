#!/bin/sh
# fetch.sh - clone quake3e upstream (into ../quake3e, gitignored) and apply our
# QNX port patches. Re-runnable: re-applies patches to a fresh clone.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../quake3e"
REPO="${QUAKE3E_REPO:-https://github.com/ec-/Quake3e}"

if [ ! -d "$SRC/.git" ]; then
  echo "cloning quake3e -> $SRC"
  git clone --depth 1 "$REPO" "$SRC"
else
  echo "quake3e already present at $SRC"
fi

for p in "$HERE"/patches/*.patch; do
  [ -f "$p" ] || continue
  if git -C "$SRC" apply --check "$p" 2>/dev/null; then
    git -C "$SRC" apply "$p" && echo "applied $(basename "$p")"
  else
    echo "skip $(basename "$p") (already applied or conflicts)"
  fi
done
echo "done. source ready at $SRC"

#!/bin/sh
# fetch.sh - clone quake3e upstream (into ../quake3e, gitignored) and apply our
# QNX port patches. Re-runnable: re-applies patches to a fresh clone.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../quake3e"
REPO="${QUAKE3E_REPO:-https://github.com/ec-/Quake3e}"
# Pinned to the exact upstream commit the QNX patches were built against, so the
# build is reproducible and the patches always apply. Override to track a newer
# upstream (the patches may then need a refresh): QUAKE3E_COMMIT=<sha> ./fetch.sh
PIN="${QUAKE3E_COMMIT:-dec793de9677bc9c16c7027e8905c2d4f4643a19}"

if [ ! -d "$SRC/.git" ]; then
  echo "cloning quake3e @ $PIN -> $SRC"
  mkdir -p "$SRC"
  git -C "$SRC" init -q
  git -C "$SRC" remote add origin "$REPO" 2>/dev/null || true
  git -C "$SRC" fetch -q --depth 1 origin "$PIN"
  git -C "$SRC" checkout -q FETCH_HEAD
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

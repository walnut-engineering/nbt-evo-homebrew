#!/bin/sh
# build.sh - everything for the two tiles: fetch + build the emulator cores (once),
# then the frontend against each.  ->  ../../bin/evo_genesis, ../../bin/evo_snes
set -e
cd "$(dirname "$0")"
./cores.sh genesis snes
make -B SYSTEM=genesis
make -B SYSTEM=snes

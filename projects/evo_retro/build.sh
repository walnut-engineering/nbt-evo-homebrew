#!/bin/sh
# build.sh - both tiles that use this frontend, cores included.
#   ->  ../../bin/evo_snes, ../../bin/evo_genesis
# Each tile also builds by itself: make -C ../evo_snes, make -C ../evo_genesis.
set -e
cd "$(dirname "$0")"
for t in evo_genesis evo_snes; do
  ( cd ../$t && ./core.sh && make )
done

#!/bin/sh
# build_bench.sh - build the headless benchmark (retro_bench.c) against each core, with
# the head unit's toolchain. This is the feasibility spike that came before the real
# frontend: it answers "does this core run on the unit, and what does a frame cost?"
# with nothing else in the way.
#
#   ./build_bench.sh        -> ../../../bin/retro_bench_{genesis,snes,snes2005}
#
# Run one on the unit:  ./retro_bench_genesis game.bin 600 frame.raw
#
# The cores come from the tiles' core.sh scripts (fetched at pinned commits, never
# committed).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../../.." && pwd)"
"$HERE/../../evo_genesis/core.sh"
"$HERE/../../evo_snes/core.sh" snes snes2005
QNX_SDP="${QNX_SDP:-$HOME/qnx650_sdp/qnx650}"
BU="$R/build/sdp-binutils"
export QNX_HOST="$QNX_SDP/host/linux/x86" QNX_TARGET="$QNX_SDP/target/qnx6"
export PATH="$BU:$PATH"
CC="$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2 -B$BU"
L="$QNX_TARGET/armle-v7/lib"; LU="$QNX_TARGET/armle-v7/usr/lib"
mkdir -p "$R/bin"
for name in genesis snes snes2005; do
  $CC -marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp -std=gnu99 -O2 -I"$QNX_TARGET/usr/include" -I"$HERE/.." \
    -D__QNX__ -D__QNXNTO__ -D_QNX_SOURCE "$L/crt1.o" "$L/crti.o" "$HERE/retro_bench.c" "$R/build/evo_retro/libretro_$name.a" "$L/crtn.o" \
    -L"$L" -L"$LU" -nostdlib -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 -Wl,-rpath-link,"$L" -Wl,-rpath-link,"$LU" \
    -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc -lm -o "$R/bin/retro_bench_$name"
  echo "built $R/bin/retro_bench_$name"
done

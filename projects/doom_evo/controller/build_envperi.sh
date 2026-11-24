#!/bin/sh
# build + deploy + run evo_envperi. args passed to the on-unit run.
#
# Toolchain: the QNX 6.5.0 SDP's own g++ 4.4.2, driven directly (ntoarmv7 binutils
# + -B, PATH) - the real QNX toolchain for the unit (armv7-a, VFPv3-D16, softfp).
# Replaced the retired vendored Linaro g++ 4.7.3 + toolchain/qnx650. gcc 4.4.2 does
# NOT accept -mno-unaligned-access (dropped; armv7-a handles unaligned in hardware).
set -e
mkdir -p build
R=../../..
SDP=${SDP:-$HOME/qnx650_sdp/qnx650}
export QNX_HOST=$SDP/host/linux/x86 QNX_TARGET=$SDP/target/qnx6
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
for t in as ld ar nm; do ln -sf "$QNX_HOST/usr/bin/ntoarmv7-$t-2.19" "$W/$t"; done
ln -sf "$QNX_HOST/usr/bin/ntoarmv7-ld-2.19" "$W/real-ld"
export PATH="$W:$QNX_HOST/usr/bin:$PATH"
CXX="arm-unknown-nto-qnx6.5.0eabi-g++-4.4.2 -B$W"
QNX_LIB=$QNX_TARGET/armle-v7/lib
# DSI SDK moved to the standalone nbt-dsi-toolkit sibling project (was ../dsi_sdk).
DSI=$R/projects/nbt-dsi-toolkit/dsi_sdk; DSI_INC=$DSI/include; DSI_LIB=$DSI/lib
CPU="-marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
CXXFLAGS="$CPU -O0 -g -nostdinc++ -fno-strict-aliasing -fno-exceptions -fno-rtti -w -I$QNX_TARGET/usr/include -I$DSI_INC -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ -D_QNX_SOURCE"
$CXX $QNX_LIB/crt1.o $QNX_LIB/crti.o $CXXFLAGS evo_envperi.cpp $QNX_LIB/crtn.o \
  -L$QNX_LIB -L$DSI_LIB -nostdlib -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 \
  -Wl,-rpath-link,$QNX_LIB -Wl,-rpath-link,$DSI_LIB -Wl,--allow-shlib-undefined \
  -l:libsys_dsi_colibry.cpp-ne.so.8 -l:libsys_dsi_servicebroker.cpp-ne.so.4 -l:libc.so.3 -lgcc \
  -o build/evo_envperi 2>/dev/null
echo "built $(ls -l build/evo_envperi | awk '{print $5}')B -> build/evo_envperi"
# Deploy + run it on the unit with the repo's ssh helpers, e.g.:
#   . ../../../scripts/evo_env.sh                       # sets EVO_HOST/EVO_USER/EVO_PASS
#   evo_push build/evo_envperi /fs/sda1/evo/evo_envperi
#   evo_ssh "chmod +x /fs/sda1/evo/evo_envperi; export LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib; /fs/sda1/evo/evo_envperi $*"

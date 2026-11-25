#!/bin/sh
# cores.sh - fetch the emulator cores at pinned commits and build each one into a
# static library with the head unit's toolchain (QNX 6.5 SDP, gcc 4.4.2).
#
#   ./cores.sh                  # genesis + snes (what the two tiles need)
#   ./cores.sh genesis          # just one: genesis | snes | snes2005
#   ./cores.sh -f snes          # -f = rebuild even if the library is up to date
#
# Output: <repo>/build/evo_retro/libretro_<name>.a  (linked by the Makefile here).
#
# The cores are NOT part of this repository: Snes9x and Genesis Plus GX have
# non-commercial licences, so they are fetched into ./cores/ (gitignored) and
# patched there (./patches/). See PLAN.md.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SELF="$HERE/$(basename "$0")"
R="$(cd "$HERE/../.." && pwd)"
QNX_SDP="${QNX_SDP:-$HOME/qnx650_sdp/qnx650}"
BU="$R/build/sdp-binutils"
export QNX_HOST="$QNX_SDP/host/linux/x86" QNX_TARGET="$QNX_SDP/target/qnx6"
[ -x "$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2" ] || { echo "FATAL: QNX SDP gcc not found under $QNX_SDP (set QNX_SDP)"; exit 1; }
mkdir -p "$BU"
for t in as ld ar nm; do ln -sf "$QNX_HOST/usr/bin/ntoarmv7-$t-2.19" "$BU/$t"; done
ln -sf "$QNX_HOST/usr/bin/ntoarmv7-ld-2.19" "$BU/real-ld"
export PATH="$BU:$PATH"
CC="$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2 -B$BU"
ARCH="-marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
BASE="$ARCH -std=gnu99 -O2 -fsigned-char -fno-strict-aliasing -DNDEBUG -D__LIBRETRO__ -I$QNX_TARGET/usr/include -D__QNX__ -D__QNXNTO__ -D_QNX_SOURCE"
CORES="$HERE/cores"; OUT="$R/build/evo_retro"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
mkdir -p "$CORES" "$OUT"

FORCE=0; WANT=""
for a in "$@"; do
  case "$a" in
    -f) FORCE=1 ;;
    genesis|snes|snes2005) WANT="$WANT $a" ;;
    *) echo "usage: $0 [-f] [genesis] [snes] [snes2005]"; exit 2 ;;
  esac
done
[ -n "$WANT" ] || WANT="genesis snes"

fetch() {   # <dir> <repo> <commit>
  if [ ! -d "$CORES/$1/.git" ]; then
    mkdir -p "$CORES/$1"; git -C "$CORES/$1" init -q
    git -C "$CORES/$1" remote add origin "https://github.com/$2.git"
  fi
  if [ "$(git -C "$CORES/$1" rev-parse -q --verify HEAD 2>/dev/null)" != "$3" ]; then
    echo "fetch $2 @ $3"
    git -C "$CORES/$1" fetch -q --depth 1 origin "$3"
  fi
  git -C "$CORES/$1" checkout -q -f "$3"       # also drops a previously applied patch
}

compile() { # <objdir> <flags> <sources...>   (run from the core's directory)
  od="$1"; fl="$2"; shift; shift
  rm -rf "$od"; mkdir -p "$od"
  n=0
  for src in "$@"; do
    ( $CC $BASE $fl -c "$src" -o "$od/$(echo "$src" | sed 's#[/.]#_#g').o" 2>>"$od/build.log" || echo "$src" >> "$od/FAILED" ) &
    n=$((n + 1)); [ $((n % JOBS)) -eq 0 ] && wait
  done
  wait
  if [ -f "$od/FAILED" ]; then echo "FAILED to compile (see $od/build.log):"; cat "$od/FAILED"; exit 1; fi
}

archive() { # <name> <objdir>...
  lib="$OUT/libretro_$1.a"; shift
  rm -f "$lib"
  objs=""; for d in "$@"; do objs="$objs $(ls "$d"/*.o)"; done
  ar rcs "$lib" $objs
  echo "built $lib ($(echo $objs | wc -w) objects)"
}

# up to date = the library exists and was built from this commit with this script
fresh() {   # <name> <commit>
  [ "$FORCE" = 0 ] && [ -f "$OUT/libretro_$1.a" ] && [ "$(cat "$OUT/$1.stamp" 2>/dev/null)" = "$2 $(cksum < "$SELF")" ]
}
stamp() { echo "$2 $(cksum < "$SELF")" > "$OUT/$1.stamp"; }

# ---- Genesis Plus GX (Genesis / Mega Drive, Master System, Game Gear, Sega CD): builds unmodified
build_genesis() {
  commit=099c962e7374eb84cae9bba180c9e0e49e767b40
  if fresh genesis $commit; then echo "genesis: up to date"; return; fi
  fetch gpgx libretro/Genesis-Plus-GX $commit
  cd "$CORES/gpgx"
  C=core; LC=libretro/libretro-common; Z=libretro/deps/zlib-1.2.11
  echo '#define INLINE static inline' > "$OUT/gpgx_defs.h"
  compile "$OUT/obj_genesis" "-DUSE_16BPP_RENDERING -DFRONTEND_SUPPORTS_RGB565 -DLSB_FIRST -DBYTE_ORDER=LITTLE_ENDIAN -DALIGN_LONG -DUSE_LIBRETRO_VFS -DM68K_OVERCLOCK_SHIFT=20 -DZ80_OVERCLOCK_SHIFT=20 -DHAVE_YM3438_CORE -DHAVE_OPLL_CORE -DMAXROMSIZE=10485760 -include $OUT/gpgx_defs.h -I$C -I$C/z80 -I$C/m68k -I$C/ntsc -I$C/sound -I$C/sound/minimp3 -I$C/input_hw -I$C/cd_hw -I$C/cart_hw -I$C/cart_hw/svp -Ilibretro -I$LC/include -I$Z" \
    $C/*.c $C/z80/*.c $C/m68k/*.c $C/ntsc/*.c $C/sound/*.c $C/input_hw/*.c $C/cd_hw/*.c $C/cart_hw/*.c $C/cart_hw/svp/*.c \
    $LC/streams/file_stream.c $LC/streams/file_stream_transforms.c $LC/compat/fopen_utf8.c $LC/compat/compat_snprintf.c $LC/compat/compat_strl.c \
    $LC/compat/compat_strcasestr.c $LC/compat/compat_posix_string.c $LC/encodings/encoding_utf.c $LC/file/file_path.c $LC/file/retro_dirent.c \
    $LC/lists/string_list.c $LC/lists/dir_list.c $LC/memmap/memalign.c $LC/string/stdstring.c $LC/vfs/vfs_implementation.c \
    $Z/adler32.c $Z/crc32.c $Z/inffast.c $Z/inflate.c $Z/inftrees.c $Z/zutil.c libretro/libretro.c
  archive genesis "$OUT/obj_genesis"
  stamp genesis $commit
}

# ---- Snes9x 2010 (Snes9x 1.52): needs the unaligned-access patch and the atomic helper
build_snes() {
  commit=82904a95e9bab2c3e63395e4b9ece8fdca8b785d
  if fresh snes $commit; then echo "snes: up to date"; return; fi
  fetch snes9x2010 libretro/snes9x2010 $commit
  cd "$CORES/snes9x2010"
  git apply "$HERE/patches/snes9x2010-unaligned-apu.patch"
  LC=libretro/libretro-common
  S10=""; for f in apu hdpack bsx bsflash c4emu cheats controls cpu cpuexec dsp fxemu globals memmap obc1 msu1 msu1_pack ppu sa1 sdd1 seta snapshot spc7110 srtc tile hwregisters; do S10="$S10 src/$f.c"; done
  compile "$OUT/obj_snes" "-DHAVE_RPNG -DHAVE_RWEBP -DHAVE_RDDS -DINLINE=inline -Ilibretro -Isrc -I$LC/include" $S10 filter/snes_ntsc.c libretro/libretro.c \
    $LC/streams/memory_stream.c $LC/formats/png/rpng.c $LC/formats/png/rpng_encode.c $LC/formats/png/rpng_apng.c $LC/file/rpng_file.c \
    $LC/features/features_cpu.c $LC/formats/image_transfer.c $LC/formats/webp/rwebp.c $LC/formats/vp8/rvp8.c $LC/formats/dds/rdds.c \
    $LC/streams/trans_stream.c $LC/streams/trans_stream_pipe.c $LC/streams/trans_stream_deflate.c $LC/encodings/encoding_deflate.c \
    $LC/encodings/encoding_crc32.c $LC/streams/interface_stream.c $LC/streams/rzip_stream.c $LC/compat/compat_posix_string.c \
    $LC/compat/compat_strcasestr.c $LC/compat/compat_snprintf.c $LC/compat/compat_strl.c $LC/compat/fopen_utf8.c $LC/encodings/encoding_utf.c \
    $LC/file/file_path.c $LC/file/file_path_io.c $LC/memmap/memalign.c $LC/streams/file_stream.c $LC/streams/file_stream_transforms.c \
    $LC/string/stdstring.c $LC/string/rstrtod.c $LC/time/rtime.c $LC/vfs/vfs_implementation.c $LC/vfs/vfs_hybrid.c $LC/file/retro_dirent.c
  $CC $BASE -c "$HERE/patches/sync_shim.c" -o "$OUT/obj_snes/sync_shim.o"
  archive snes "$OUT/obj_snes"
  stamp snes $commit
}

# ---- Snes9x 2005 (Snes9x 1.43): builds unmodified; the light fallback (not used by a tile)
build_snes2005() {
  commit=a79dfe9047e7fec58808aefe48ad2bf499c7af11
  if fresh snes2005 $commit; then echo "snes2005: up to date"; return; fi
  fetch snes9x2005 libretro/snes9x2005 $commit
  cd "$CORES/snes9x2005"
  S5=""; for f in c4 c4emu cheats2 cheats clip cpu cpuexec cpuops data dma dsp1 fxemu fxinst gfx getset globals memmap obc1 ppu sa1 sa1cpu sdd1 sdd1emu seta010 seta011 seta018 seta spc7110 spc7110dec srtc tile apu soundux spc700; do S5="$S5 source/$f.c"; done
  compile "$OUT/obj_snes2005" "-DLAGFIX -DLOAD_FROM_MEMORY -Isource -I. -Ilibretro-common/include" $S5 libretro.c
  archive snes2005 "$OUT/obj_snes2005"
  stamp snes2005 $commit
}

for w in $WANT; do build_$w; done

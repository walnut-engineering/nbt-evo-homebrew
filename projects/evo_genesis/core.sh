#!/bin/sh
# core.sh - fetch the Sega Genesis emulator core at a pinned commit and build it into
# a static library with the head unit's toolchain (QNX 6.5 SDP, gcc 4.4.2).
#
#   ./core.sh                # Genesis Plus GX, the core the tile uses
#   ./core.sh -f             # -f = rebuild even if the library is up to date
#
# Output: <repo>/build/evo_retro/libretro_genesis.a  (linked by the Makefile here;
# `make` runs this script by itself when the library is missing).
#
# The core is NOT part of this repository: Genesis Plus GX has a non-commercial
# licence, so it is fetched into ./core/ (gitignored). It builds unmodified. See
# README.md.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; SELF="$HERE/$(basename "$0")"
. "$HERE/../evo_retro/core_common.sh"

FORCE=0
for a in "$@"; do
  case "$a" in
    -f) FORCE=1 ;;
    genesis) ;;
    *) echo "usage: $0 [-f]"; exit 2 ;;
  esac
done

# ---- Genesis Plus GX (Genesis / Mega Drive, Master System, Game Gear, Sega CD)
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

build_genesis

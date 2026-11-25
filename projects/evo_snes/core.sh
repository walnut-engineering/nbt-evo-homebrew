#!/bin/sh
# core.sh - fetch the SNES emulator core at a pinned commit, patch it and build it
# into a static library with the head unit's toolchain (QNX 6.5 SDP, gcc 4.4.2).
#
#   ./core.sh                # Snes9x 2010, the core the tile uses
#   ./core.sh snes2005       # Snes9x 2005, the lighter fallback (link: make CORE=snes2005)
#   ./core.sh -f             # -f = rebuild even if the library is up to date
#
# Output: <repo>/build/evo_retro/libretro_<name>.a  (linked by the Makefile here;
# `make` runs this script by itself when the library is missing).
#
# The core is NOT part of this repository: Snes9x has a non-commercial licence, so it
# is fetched into ./core/ (gitignored) and patched there (./patches/). See README.md.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; SELF="$HERE/$(basename "$0")"
. "$HERE/../evo_retro/core_common.sh"

FORCE=0; WANT=""
for a in "$@"; do
  case "$a" in
    -f) FORCE=1 ;;
    snes|snes2005) WANT="$WANT $a" ;;
    *) echo "usage: $0 [-f] [snes] [snes2005]"; exit 2 ;;
  esac
done
[ -n "$WANT" ] || WANT="snes"

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

# ---- Snes9x 2005 (Snes9x 1.43): builds unmodified; the light fallback (not used by default)
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

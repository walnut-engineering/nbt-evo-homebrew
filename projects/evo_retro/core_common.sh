# core_common.sh - shared by the tiles' core.sh scripts (../evo_snes/core.sh,
# ../evo_genesis/core.sh): the head unit's toolchain (QNX 6.5 SDP, gcc 4.4.2) and the
# fetch / compile / archive steps. Sourced, not run:
#
#     HERE="$(cd "$(dirname "$0")" && pwd)"; SELF="$HERE/$(basename "$0")"
#     . "$HERE/../evo_retro/core_common.sh"
#
# A core is fetched at a pinned commit into the tile's own ./core/ (gitignored) and
# built into <repo>/build/evo_retro/libretro_<name>.a, which the tile's Makefile
# links. The cores are NOT part of this repository: Snes9x and Genesis Plus GX have
# non-commercial licences. See PLAN.md.
COMMON="$(cd "$HERE/../evo_retro" && pwd)/core_common.sh"
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
CORES="$HERE/core"; OUT="$R/build/evo_retro"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
mkdir -p "$CORES" "$OUT"

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

# up to date = the library exists and was built from this commit with these scripts
sig() { echo "$1 $(cat "$SELF" "$COMMON" | cksum)"; }
fresh() {   # <name> <commit>
  [ "$FORCE" = 0 ] && [ -f "$OUT/libretro_$1.a" ] && [ "$(cat "$OUT/$1.stamp" 2>/dev/null)" = "$(sig "$2")" ]
}
stamp() { sig "$2" > "$OUT/$1.stamp"; }

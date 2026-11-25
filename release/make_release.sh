#!/bin/sh
# make_release.sh - build every binary and pack the prebuilt-binary release.
#
#   ./release/make_release.sh 2026.11.25              build, stage, check, pack
#   ./release/make_release.sh --no-build 2026.11.25   pack what is already built
#
# Output, in build/release/:
#   nbt-evo-homebrew-bin-<ver>.tar.gz         binaries + on-unit installer + licences
#   nbt-evo-homebrew-cores-src-<ver>.tar.gz   the emulator cores' source, as built
#   install.sh  INSTALL.md                    copies of the two files people read first
#
# Run it in a checkout of the public repository: the archive may only contain what
# that repository's source builds. It needs what any build here needs (see the
# top-level README: the QNX 6.5 SDP and the on-unit libraries) and, the first time,
# network access - Quake 3 and the emulator cores are fetched at pinned commits.
#
# What it refuses to ship: a binary that still names the build machine (paths are
# removed from the binaries; anything left over stops the script).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$HERE/.." && pwd)"
BUILD=1
[ "${1:-}" = "--no-build" ] && { BUILD=0; shift; }
VER="${1:?usage: make_release.sh [--no-build] <version>}"
QNX_SDP="${QNX_SDP:-$HOME/qnx650_sdp/qnx650}"
STRIP="$QNX_SDP/host/linux/x86/usr/bin/ntoarmv7-strip-2.19"
[ -x "$STRIP" ] || { echo "FATAL: QNX SDP not found under $QNX_SDP (set QNX_SDP)"; exit 1; }
NAME="nbt-evo-homebrew-bin-$VER"; CS="nbt-evo-homebrew-cores-src-$VER"
OUT="$R/build/release"; PKG="$OUT/$NAME"; P="$PKG/payload"
cd "$R"
mkdir -p "$OUT"

need() { for f in "$@"; do [ -f "$f" ] || { echo "FATAL: missing $f"; exit 1; }; done; }

# ---- 1. build -----------------------------------------------------------------
APPS="evo_launcher evo_hmistate evo_terminal evo_sysmon evo_gps evo_nes evo_snes evo_genesis evo_padd"
EXTRAS="evo_reboot evo_screenshot evo_cursor evo_gltest"
if [ "$BUILD" = 1 ]; then
  for p in $APPS $EXTRAS; do
    echo "== build $p"
    make -C projects/$p >"$R/build/release-build.log" 2>&1 || { tail -20 "$R/build/release-build.log"; exit 1; }
  done
  echo "== build evo_padfake";  make -C projects/evo_padd fake >/dev/null 2>&1
  echo "== build doom_evo";     make -C projects/doom_evo/source >/dev/null 2>&1
  echo "== build knob feed (tee, shim, feedtap)"
  make -C projects/doom_evo/controller evo_feedtap evo_most_shim evo_tee.so >/dev/null 2>&1
  echo "== build Quake 3 (fetches quake3e + ioquake3 the first time)"
  ( cd projects/quake3e_qnx && ./fetch.sh && ./fetch_ioq3.sh && ./build_client.sh && ./build_game.sh all ) >"$R/build/release-q3.log" 2>&1 \
    || { tail -20 "$R/build/release-q3.log"; exit 1; }
fi

# ---- 2. stage -----------------------------------------------------------------
CTL=projects/doom_evo/controller; Q3=projects/quake3e_qnx/build
for b in $APPS doom_evo $EXTRAS evo_padfake; do need bin/$b; done
need $CTL/build/evo_feedtap $CTL/build/evo_tee.so $CTL/build/evo_most_shim \
     $Q3/quake3e.arm $Q3/game/qagamearm.so $Q3/game/cgamearm.so $Q3/game/uiarm.so \
     LICENSE projects/doom_evo/source/COPYING projects/quake3e_qnx/COPYING projects/evo_padd/COPYING \
     projects/evo_nes/nofrendo/COPYING.LGPL projects/evo_snes/core/snes9x2010/LICENSE.txt \
     projects/evo_genesis/core/gpgx/LICENSE.txt

rm -rf "$PKG" "$OUT/$CS"; mkdir -p "$P/icons" "$P/q3/demoq3" "$PKG/extras" "$PKG/LICENSES"
for b in $APPS doom_evo; do cp bin/$b "$P/$b"; done
for b in $EXTRAS evo_padfake; do cp bin/$b "$PKG/extras/$b"; done
cp $CTL/build/evo_feedtap "$P/evo_feedtap"
cp $CTL/build/evo_tee.so "$P/evo_tee_most.so"            # the name the shim loads
cp $CTL/build/evo_most_shim "$PKG/most"                 # installed as /opt/sys/bin/most by --enable-knob
cp $Q3/quake3e.arm "$P/q3/quake3e.arm"
for m in qagamearm cgamearm uiarm; do cp $Q3/game/$m.so "$P/q3/demoq3/$m.so"; done
cp projects/doom_evo/target/doom_launch_sda1.sh "$P/doom_launch.sh"
cp projects/quake3e_qnx/target/q3_launch.sh projects/evo_nes/target/nes_launch.sh \
   projects/evo_snes/target/snes_launch.sh projects/evo_genesis/target/genesis_launch.sh \
   $CTL/evo_autostart.sh "$P/"
# icons that are this project's own drawings (DOOM's and Quake's are the games' logos: not shipped)
cp projects/evo_launcher/icons/cpu.rgba projects/evo_launcher/icons/gps.rgba projects/evo_launcher/icons/terminal.rgba \
   projects/evo_nes/nes.rgba projects/evo_snes/snes.rgba projects/evo_genesis/genesis.rgba "$P/icons/"
cp release/install.sh release/uninstall.sh release/INSTALL.md "$PKG/"
chmod 755 "$PKG"/*.sh "$P"/*.sh "$PKG/most" "$PKG"/extras/* "$P/q3/quake3e.arm" "$P"/q3/demoq3/*.so
for b in $APPS doom_evo evo_feedtap evo_tee_most.so; do chmod 755 "$P/$b"; done
chmod 644 "$P"/icons/* "$PKG/INSTALL.md"

# the tile list: generated from the app.desc files, minus any tile whose program is not in the payload
sh scripts/gen_manifest.sh >/dev/null 2>&1
{
  echo "# Generated for the binary release from projects/*/app.desc - do not edit by hand."
  echo "# One tile per line:  Name|command|icon"
  grep -v '^#' build/launcher.manifest | while IFS='|' read -r n c i; do
    if [ -e "$P/${c#/fs/sda1/evo/}" ]; then echo "$n|$c|$i"; else echo "   (tile '$n' is not in the release - left out)" >&2; fi
  done
} > "$P/launcher.manifest"

# ---- 3. strip, remove build paths, check ---------------------------------------
find "$PKG" -type f | while read -r f; do
  if file "$f" | grep -q 'ELF'; then "$STRIP" "$f"; fi
done
python3 "$HERE/scrub_paths.py" "$PKG" "$R" "$QNX_SDP" "$HOME"

# ---- 4. licences ---------------------------------------------------------------
L="$PKG/LICENSES"
cp LICENSE "$L/MIT.txt"
cp projects/evo_padd/COPYING "$L/GPL-2.0.txt"
cp projects/evo_nes/nofrendo/COPYING.LGPL "$L/LGPL-2.0-Nofrendo.txt"
cp projects/evo_snes/core/snes9x2010/LICENSE.txt "$L/Snes9x.txt"
cp projects/evo_genesis/core/gpgx/LICENSE.txt "$L/Genesis-Plus-GX.txt"
# the ISC notice that travels with the radio constants in evo_padd
sed -n '/^ \* Most of the defines and enums were copied/,/^ \*\//p' projects/evo_padd/mt76_regs.h | sed 's/^ \*\/$//; s/^ \* \{0,1\}//' > "$L/ISC-mt76.txt"
[ -s "$L/ISC-mt76.txt" ] || { echo "FATAL: could not extract the mt76 ISC notice"; exit 1; }
SNES_COMMIT="$(git -C projects/evo_snes/core/snes9x2010 rev-parse HEAD)"
GEN_COMMIT="$(git -C projects/evo_genesis/core/gpgx rev-parse HEAD)"
sed -e "s/@VER@/$VER/g" -e "s/@SNES_COMMIT@/$SNES_COMMIT/g" -e "s/@GEN_COMMIT@/$GEN_COMMIT/g" release/THIRD_PARTY.md > "$L/README.md"
chmod 644 "$L"/*

# ---- 5. checksums + archive -----------------------------------------------------
( cd "$PKG" && find . -type f ! -name SHA256SUMS | sed 's#^\./##' | LC_ALL=C sort | xargs sha256sum > SHA256SUMS )
tar czf "$OUT/$NAME.tar.gz" -C "$OUT" --owner=0 --group=0 --numeric-owner --sort=name "$NAME"
cp release/install.sh release/INSTALL.md "$OUT/"

# ---- 6. the emulator cores' source, exactly as built ----------------------------
# Snes9x and Genesis Plus GX are passed on under their own non-commercial licences;
# Genesis Plus GX asks that a binary come with its complete source. The frontend's
# source is this repository.
mkdir -p "$OUT/$CS"
git -C projects/evo_snes/core/snes9x2010 archive --prefix=snes9x2010/ HEAD | tar x -C "$OUT/$CS"
( cd "$OUT/$CS/snes9x2010" && git apply "$R/projects/evo_snes/patches/snes9x2010-unaligned-apu.patch" )
mkdir -p "$OUT/$CS/evo_snes-patches"; cp projects/evo_snes/patches/* "$OUT/$CS/evo_snes-patches/"
git -C projects/evo_genesis/core/gpgx archive --prefix=Genesis-Plus-GX/ HEAD | tar x -C "$OUT/$CS"
cat > "$OUT/$CS/README.txt" <<EOF
Source of the two emulator cores linked into evo_snes and evo_genesis in
nbt-evo-homebrew-bin-$VER, exactly as they were built.

  snes9x2010/        libretro/snes9x2010 at commit $SNES_COMMIT
                     with evo_snes-patches/snes9x2010-unaligned-apu.patch applied;
                     evo_snes-patches/sync_shim.c is compiled in beside it.
                     Licence: snes9x2010/LICENSE.txt (non-commercial use only).
  Genesis-Plus-GX/   libretro/Genesis-Plus-GX at commit $GEN_COMMIT
                     unmodified. Licence: Genesis-Plus-GX/LICENSE.txt
                     (redistribution may not be sold or used commercially).

How they are compiled (source lists and flags): projects/evo_snes/core.sh and
projects/evo_genesis/core.sh in the nbt-evo-homebrew repository, which also holds
the frontend they are linked with (projects/evo_retro).
EOF
tar czf "$OUT/$CS.tar.gz" -C "$OUT" --owner=0 --group=0 --numeric-owner --sort=name "$CS"
rm -rf "$OUT/$CS"

echo
echo "== release $VER"
( cd "$OUT" && sha256sum "$NAME.tar.gz" "$CS.tar.gz" && ls -l "$NAME.tar.gz" "$CS.tar.gz" )
echo "tiles:"; grep -v '^#' "$P/launcher.manifest" | sed 's/|.*//; s/^/  /'

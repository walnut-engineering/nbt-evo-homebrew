#!/bin/sh
# build_game.sh - M5: build the Quake3 game VM modules as NATIVE QNX shared
# libraries (qagamearm.so / cgamearm.so / uiarm.so) for the NBT EVO, so quake3e
# loads them with vm_game/vm_cgame/vm_ui = 0 (VMI_NATIVE) instead of the
# incompatible 1999-demo QVMs ("UI is version 3, expected 6").
#
# Source = ioquake3's GPL baseq3 game logic (projects/ioq3, gitignored), built
# with ioq3's OWN headers so it is self-consistent, then made ABI-compatible
# with the quake3e engine we built via a few targeted patches (ioq3_patches/):
#  - q_platform.h: a QNX block (OS_STRING "qnx", ARCH "arm", DLL_EXT ".so",
#    little-endian; QNX has no glibc <endian.h>).
#  - cg_public.h / ui_public.h: drop the QVM-only math/mem syscalls ioq3 inserts
#    mid-enum (CG_/UI_ MEMSET/MEMCPY/STRNCPY/SIN/COS/ATAN2/SQRT) so the remaining
#    trap numbers realign with quake3e's. Native code never calls them.
# The frozen network/VM structs (playerState_t/entityState_t/usercmd_t) are
# byte-identical between ioq3 and quake3e, and the game G_ import enum matches
# (quake3e only appended new traps at the tail), so native modules interoperate.
#
#   ./build_game.sh            # build all modules
#   ./build_game.sh qagame     # just one (qagame|cgame|ui)
#   ./build_game.sh -v qagame  # verbose
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
IO="$HERE/../ioq3/code"
OUT="$HERE/build/game"
LOG="$HERE/build/last-game-build.log"

VERBOSE=0
[ "${1:-}" = "-v" ] && { VERBOSE=1; shift; }
WANT="${1:-all}"

[ -d "$IO/game" ] || { echo "FATAL: ioq3 source not at $IO (clone github.com/ioquake/ioq3 -> projects/ioq3)"; exit 1; }

QNX_SDP="${QNX_SDP:-$HOME/qnx650_sdp/qnx650}"
QNX_HOST="$QNX_SDP/host/linux/x86"
QNX_TARGET="$QNX_SDP/target/qnx6"
SDP_GCC="$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2"
SDP_STRIP="$QNX_HOST/usr/bin/ntoarmv7-strip-2.19"
SDP_NM="$QNX_HOST/usr/bin/ntoarmv7-nm-2.19"
BWRAP="$QNX_SDP/bwrap"
[ -x "$SDP_GCC" ] || { echo "FATAL: SDP gcc not at $SDP_GCC"; exit 1; }
mkdir -p "$BWRAP"
for t in as ld ar nm; do ln -sf "$QNX_HOST/usr/bin/ntoarmv7-$t-2.19" "$BWRAP/$t"; done
export QNX_HOST QNX_TARGET
export PATH="$BWRAP:$PATH"
CC="$SDP_GCC -B$BWRAP"

SDP_CPU="-marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
# no -fvisibility=hidden (vmMain/dllEntry must export); no -DQ3_VM (native build:
# the .c files' native branch provides dllEntry + links libc/libm directly).
CFLAGS="$SDP_CPU -O2 -fPIC -DNDEBUG -ffunction-sections -fdata-sections -pipe \
  -Wno-unused-result -fno-strict-aliasing"
[ "${DEBUG:-0}" = "1" ] && CFLAGS="$CFLAGS -g"
# --no-undefined: fail the link (not at dlopen on the unit) if the module has
# any unresolved symbol. The only legit externals are libc/libm; the engine
# syscall pointer arrives via dllEntry, not a link symbol. So any complaint is a
# missing source file in the module's set.
LDFLAGS="-shared -lm -Wl,--gc-sections -Wl,--no-undefined"

mkdir -p "$OUT"
: > "$LOG"

# ---- module source sets (ioq3 baseq3; dirs relative to $IO) ----------------
# game (qagamearm.so): server gameplay + bot AI + shared bg_* (no bg_lib = the
# QVM libc shim; native links the real libc). q_math/q_shared from ioq3 qcommon.
QAGAME="game/ai_chat game/ai_cmd game/ai_dmnet game/ai_dmq3 game/ai_main \
  game/ai_team game/ai_vcmd game/bg_misc game/bg_pmove game/bg_slidemove \
  game/g_active game/g_arenas game/g_bot game/g_client game/g_cmds game/g_combat \
  game/g_items game/g_main game/g_mem game/g_misc game/g_missile game/g_mover \
  game/g_session game/g_spawn game/g_svcmds game/g_syscalls \
  game/g_target game/g_team game/g_trigger game/g_utils game/g_weapon \
  qcommon/q_math qcommon/q_shared"
# cgame (cgamearm.so)
CGAME="cgame/cg_consolecmds cgame/cg_draw cgame/cg_drawtools cgame/cg_effects \
  cgame/cg_ents cgame/cg_event cgame/cg_info cgame/cg_localents cgame/cg_main \
  cgame/cg_marks cgame/cg_particles cgame/cg_players \
  cgame/cg_playerstate cgame/cg_predict cgame/cg_scoreboard cgame/cg_servercmds \
  cgame/cg_snapshot cgame/cg_syscalls cgame/cg_view cgame/cg_weapons \
  game/bg_misc game/bg_pmove game/bg_slidemove qcommon/q_math qcommon/q_shared"
# ui (uiarm.so): baseq3 menu (q3_ui) + ui/ syscalls + shared bg_misc
UI="q3_ui/ui_addbots q3_ui/ui_atoms q3_ui/ui_cdkey q3_ui/ui_cinematics \
  q3_ui/ui_confirm q3_ui/ui_connect q3_ui/ui_controls2 q3_ui/ui_credits \
  q3_ui/ui_demo2 q3_ui/ui_display q3_ui/ui_gameinfo q3_ui/ui_ingame \
  q3_ui/ui_loadconfig q3_ui/ui_menu q3_ui/ui_mfield q3_ui/ui_mods \
  q3_ui/ui_network q3_ui/ui_options q3_ui/ui_playermodel q3_ui/ui_players \
  q3_ui/ui_playersettings q3_ui/ui_preferences q3_ui/ui_qmenu q3_ui/ui_removebots \
  q3_ui/ui_saveconfig q3_ui/ui_serverinfo q3_ui/ui_servers2 q3_ui/ui_setup \
  q3_ui/ui_sound q3_ui/ui_sparena q3_ui/ui_specifyserver q3_ui/ui_splevel \
  q3_ui/ui_sppostgame q3_ui/ui_spskill q3_ui/ui_startserver q3_ui/ui_team q3_ui/ui_teamorders \
  q3_ui/ui_video q3_ui/ui_main ui/ui_syscalls game/bg_misc qcommon/q_math qcommon/q_shared"

OBJ=""; OBJS=""; FAILED=""; n=0; ok=0
compile() { # $1=relpath under $IO (no .c)
  src="$IO/$1.c"; base="$(echo "$1" | tr '/' '_')"
  out="$OBJDIR/$base.o"; n=$((n+1))
  [ "$VERBOSE" = 1 ] && echo "  CC $1"
  echo "=== $1" >> "$LOG"
  if $CC $CFLAGS -I"$IO/$(dirname "$1")" -o "$out" -c "$src" >> "$LOG" 2>&1; then
    ok=$((ok+1)); OBJS="$OBJS $out"
  else
    echo "    FAIL: $base"; FAILED="$FAILED $base"
  fi
}

build_module() { # $1=module name  $2=output so  $3=source list
  echo ">> ==== $1 (native QNX .so) ===="
  OBJDIR="$OUT/obj_$1"; rm -rf "$OBJDIR"; mkdir -p "$OBJDIR"
  OBJS=""; FAILED=""; n=0; ok=0
  for s in $3; do compile "$s"; done
  echo ">> $1 compiled $ok/$n"
  if [ -n "$FAILED" ]; then echo ">> FAILED:$FAILED"; echo ">> errors: grep -nE 'error:' $LOG"; return 2; fi
  echo ">> linking $2"
  echo "=== LINK $2" >> "$LOG"
  if $CC -o "$2" $OBJS $LDFLAGS >> "$LOG" 2>&1; then
    [ "${DEBUG:-0}" = "1" ] || "$SDP_STRIP" "$2" 2>/dev/null
    echo ">> OK: $2"; ls -l "$2"
    "$SDP_NM" -D "$2" 2>/dev/null | grep -iE " T (vmMain|dllEntry)$" || echo "  !! vmMain/dllEntry NOT exported"
  else
    echo ">> LINK FAILED:"; grep -nE "undefined reference|multiple definition" "$LOG" | tail -40; return 3
  fi
}

case "$WANT" in
  qagame) build_module qagame "$OUT/qagamearm.so" "$QAGAME" ;;
  cgame)  build_module cgame  "$OUT/cgamearm.so"  "$CGAME" ;;
  ui)     build_module ui     "$OUT/uiarm.so"     "$UI" ;;
  all)    build_module qagame "$OUT/qagamearm.so" "$QAGAME" && \
          build_module cgame  "$OUT/cgamearm.so"  "$CGAME" && \
          build_module ui     "$OUT/uiarm.so"     "$UI" ;;
  *) echo "usage: $0 [-v] qagame|cgame|ui|all"; exit 1 ;;
esac
echo ">> done."

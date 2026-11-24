#!/bin/sh
# build.sh - M2: build the quake3e DEDICATED SERVER for the NBT EVO (QNX 6.5,
# armle-v7) with the QNX 6.5 SDP gcc 4.4.2. Bypasses quake3e's uname-based
# Makefile (which has no QNX platform) and compiles the exact dedicated-server
# source set (Q3DOBJ) directly, mirroring the Makefile's per-group defines:
#   server/ qcommon/ unix/  -> -DDEDICATED            (DO_DED_CC)
#   qcommon/vm*             -> -DDEDICATED -fno-fast-math (DO_DED_CC_QVM)
#   botlib/                 -> -DBOTLIB                (DO_BOT_CC)
# Interpreted VM only (-DNO_VM_COMPILED); the armv7 JIT is a later milestone.
#
# Re-runnable. Compiles all sources, CONTINUES past per-file errors, and prints
# a summary of what failed so one pass surfaces every QNX-compat issue.
#   ./build.sh            # compile + link
#   ./build.sh -v         # also echo each compile command
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../quake3e"
Q="$SRC/code"
OBJ="$HERE/build/ded"
BIN="$HERE/build/quake3e.ded.arm"
LOG="$HERE/build/last-build.log"
VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

# --- SDP toolchain (mirrors ../../shared/sdp.mk) --------------------------
QNX_SDP="${QNX_SDP:-$HOME/qnx650_sdp/qnx650}"
QNX_HOST="$QNX_SDP/host/linux/x86"
QNX_TARGET="$QNX_SDP/target/qnx6"
SDP_GCC="$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2"
SDP_STRIP="$QNX_HOST/usr/bin/ntoarmv7-strip-2.19"
BWRAP="$QNX_SDP/bwrap"

if [ ! -x "$SDP_GCC" ]; then echo "FATAL: SDP gcc not found at $SDP_GCC"; exit 1; fi
if [ ! -d "$Q" ]; then echo "FATAL: quake3e source not at $SRC (run ./fetch.sh)"; exit 1; fi

# binutils symlinks so gcc's collect2 finds as/ld/ar/nm bare on PATH
mkdir -p "$BWRAP"
for t in as ld ar nm; do ln -sf "$QNX_HOST/usr/bin/ntoarmv7-$t-2.19" "$BWRAP/$t"; done

export QNX_HOST QNX_TARGET
export PATH="$BWRAP:$PATH"
CC="$SDP_GCC -B$BWRAP"

SDP_CPU="-marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
# -include qnx_prefix.h FIRST so QNX's <sys/types.h> msg_t is renamed aside
# before any engine header defines quake3e's own msg_t (see qnx_prefix.h).
CFLAGS="$SDP_CPU -O2 -DNDEBUG -fvisibility=hidden -ffunction-sections -fdata-sections \
  -pipe -Wall -Wno-unused-result -DUSE_ICON -DNO_VM_COMPILED -include $HERE/qnx_prefix.h"
LDFLAGS="-lm -lsocket -Wl,--gc-sections"

# DEBUG=1 : keep symbols (-g, no strip) for addr2line on crashes.
if [ "${DEBUG:-0}" = "1" ]; then CFLAGS="$CFLAGS -g"; fi

# --- dedicated-server source set (Q3DOBJ) --------------------------------
SRV="sv_bot sv_client sv_ccmds sv_filter sv_game sv_init sv_main sv_net_chan sv_snapshot sv_world"
CMN="cm_load cm_patch cm_polylib cm_test cm_trace cmd common cvar files history keys md4 md5 msg net_chan net_ip huffman huffman_static q_math q_shared unzip"
UNX="linux_signals unix_main unix_shared"
VM="vm vm_interpreted"
BOT="be_aas_bspq3 be_aas_cluster be_aas_debug be_aas_entity be_aas_file be_aas_main be_aas_move be_aas_optimize be_aas_reach be_aas_route be_aas_routealt be_aas_sample be_ai_char be_ai_chat be_ai_gen be_ai_goal be_ai_move be_ai_weap be_ai_weight be_ea be_interface l_crc l_libvar l_log l_memory l_precomp l_script l_struct"

rm -rf "$OBJ"; mkdir -p "$OBJ"
: > "$LOG"
OBJS=""; FAILED=""; n=0; ok=0

compile() { # $1=src path  $2=extra defines
  src="$1"; extra="$2"
  base="$(basename "$src" .c)"
  out="$OBJ/$base.o"
  n=$((n+1))
  cmd="$CC $CFLAGS $extra -o $out -c $src"
  [ "$VERBOSE" = 1 ] && echo "CC $src $extra"
  echo "=== $src $extra" >> "$LOG"
  if $cmd >> "$LOG" 2>&1; then
    ok=$((ok+1)); OBJS="$OBJS $out"
  else
    echo "  FAIL: $base" ; FAILED="$FAILED $base"
  fi
}

echo ">> compiling quake3e dedicated server (QNX armle-v7, SDP gcc 4.4.2)"
for b in $SRV; do compile "$Q/server/$b.c"   "-DDEDICATED"; done
for b in $CMN; do compile "$Q/qcommon/$b.c"  "-DDEDICATED"; done
for b in $UNX; do compile "$Q/unix/$b.c"     "-DDEDICATED"; done
for b in $VM;  do compile "$Q/qcommon/$b.c"  "-DDEDICATED -fno-fast-math"; done
for b in $BOT; do compile "$Q/botlib/$b.c"   "-DBOTLIB"; done

echo ">> compiled $ok/$n objects"
if [ -n "$FAILED" ]; then
  echo ">> FAILED:$FAILED"
  echo ">> see $LOG for errors (grep -n 'error:' )"
  exit 2
fi

echo ">> linking $BIN"
echo "=== LINK" >> "$LOG"
if $CC -o "$BIN" $OBJS $LDFLAGS >> "$LOG" 2>&1; then
  [ "${DEBUG:-0}" = "1" ] || "$SDP_STRIP" "$BIN" 2>/dev/null
  echo ">> OK: $BIN"
  ls -l "$BIN"
  file "$BIN" 2>/dev/null || true
else
  echo ">> LINK FAILED - see $LOG (grep -n 'undefined\\|error')"
  grep -nE "undefined reference|error" "$LOG" | tail -40
  exit 3
fi

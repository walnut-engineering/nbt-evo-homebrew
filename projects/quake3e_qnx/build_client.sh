#!/bin/sh
# build_client.sh - M3+: build the quake3e CLIENT for the NBT EVO (QNX 6.5,
# armle-v7) with the SDP gcc 4.4.2. Static renderer (USE_RENDERER_DLOPEN=0),
# renderer2 (opengl2), USE_SDL=0 -> our QNX platform layer (qnx/qnx_glimp.c etc.)
# replaces the SDL glue. One binary, no .so dlopen.
#
# The renderer resolves GL through ri.GL_GetProcAddress into its qgl table, so
# the client+renderer link with no GL lib; desktop-GL-only names resolve to NULL
# at runtime (the renderer2->GLES2 porting surface, M4). GLimp_Init opens the
# EGL/GLES2 window (M3).
#
# Re-runnable; continues past per-file errors and summarises failures.
#   ./build_client.sh         # build
#   ./build_client.sh -v      # echo each compile
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../quake3e"
Q="$SRC/code"
OBJ="$HERE/build/cl"
GEN="$HERE/build/gen/glsl"      # generated GLSL .c
BIN="$HERE/build/quake3e.arm"
LOG="$HERE/build/last-client-build.log"
VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

QNX_SDP="${QNX_SDP:-$HOME/qnx650_sdp/qnx650}"
QNX_HOST="$QNX_SDP/host/linux/x86"
QNX_TARGET="$QNX_SDP/target/qnx6"
SDP_GCC="$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2"
SDP_STRIP="$QNX_HOST/usr/bin/ntoarmv7-strip-2.19"
BWRAP="$QNX_SDP/bwrap"
[ -x "$SDP_GCC" ] || { echo "FATAL: SDP gcc not at $SDP_GCC"; exit 1; }
[ -d "$Q" ] || { echo "FATAL: quake3e source not at $SRC (run ./fetch.sh)"; exit 1; }

mkdir -p "$BWRAP"
for t in as ld ar nm; do ln -sf "$QNX_HOST/usr/bin/ntoarmv7-$t-2.19" "$BWRAP/$t"; done
export QNX_HOST QNX_TARGET
export PATH="$BWRAP:$PATH"
CC="$SDP_GCC -B$BWRAP"

SDP_CPU="-marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
CFLAGS="$SDP_CPU -O2 -DNDEBUG -fvisibility=hidden -ffunction-sections -fdata-sections \
  -pipe -Wno-unused-result -DNO_VM_COMPILED -DUSE_OPENGL_API -include $HERE/qnx_prefix.h"
[ "${DEBUG:-0}" = "1" ] && CFLAGS="$CFLAGS -g"
# our QNX layer lives outside the tree -> needs explicit include dirs + jpeg hdrs
QNXINC="-I$Q/client -I$Q/renderercommon -I$Q/qcommon -I$HERE/qnx -I$HERE/../../shared/include"
JPGINC="-I$Q/libjpeg"
# renderer2 (rend2) include set: our GL2->GLES2 compat header lives in qnx/
REND2INC="-I$Q/renderer2 -I$Q/renderercommon -I$Q/qcommon -I$Q/libjpeg -I$HERE/qnx"
LDFLAGS="-lm -lsocket -lscreen -l:libEGL.so.1 -l:libGLESv2.so.1 -l:libasound.so.2 -Wl,--gc-sections"

rm -rf "$OBJ" "$GEN"; mkdir -p "$OBJ" "$GEN"
: > "$LOG"
OBJS=""; FAILED=""; n=0; ok=0

compile() { # $1=src  $2=extra flags  $3=tag(optional, for unique obj name)
  src="$1"; extra="$2"
  base="$(basename "$src" .c)"; [ -n "${3:-}" ] && base="$3_$base"
  out="$OBJ/$base.o"
  n=$((n+1))
  [ "$VERBOSE" = 1 ] && echo "CC $src $extra"
  echo "=== $src $extra" >> "$LOG"
  if $CC $CFLAGS $extra -o "$out" -c "$src" >> "$LOG" 2>&1; then
    ok=$((ok+1)); OBJS="$OBJS $out"
  else
    echo "  FAIL: $base"; FAILED="$FAILED $base"
  fi
}

# M4: link against the real renderer2 (rend2) ported to GLES2 via the qnx/
# GL2->GLES2 compat shim (qnx_gl2compat.h). The M3 stub (qnx_ref_stub.c) is kept
# in the tree for reference but is no longer compiled.

# ---- source groups --------------------------------------------------------
CLIENT="cl_cgame cl_cin cl_console cl_input cl_keys cl_main cl_net_chan cl_parse cl_scrn cl_ui cl_avi cl_jpeg \
  snd_adpcm snd_dma snd_mem snd_mix snd_wavelet snd_main snd_codec snd_codec_wav"
QCMN="cm_load cm_patch cm_polylib cm_test cm_trace cmd common cvar files history keys md4 md5 msg net_chan net_ip huffman huffman_static q_math q_shared unzip puff"
VM="vm vm_interpreted"
SRV="sv_bot sv_ccmds sv_client sv_filter sv_game sv_init sv_main sv_net_chan sv_snapshot sv_world"
BOT="be_aas_bspq3 be_aas_cluster be_aas_debug be_aas_entity be_aas_file be_aas_main be_aas_move be_aas_optimize be_aas_reach be_aas_route be_aas_routealt be_aas_sample be_ai_char be_ai_chat be_ai_gen be_ai_goal be_ai_move be_ai_weap be_ai_weight be_ea be_interface l_crc l_libvar l_log l_memory l_precomp l_script l_struct"
UNX="unix_main unix_shared linux_signals"
# renderer2 sources that live in code/renderer2/
REND2_R2="tr_animation tr_backend tr_bsp tr_cmds tr_curve tr_dsa tr_extramath tr_extensions tr_fbo tr_flares tr_glsl tr_image tr_image_dds tr_init tr_light tr_main tr_marks tr_mesh tr_model tr_model_iqm tr_postprocess tr_scene tr_shade tr_shade_calc tr_shader tr_shadows tr_sky tr_surface tr_vbo tr_world"
# renderer2 sources that live in code/renderercommon/ (shared image/font/noise)
REND2_RC="tr_font tr_image_bmp tr_image_jpg tr_image_pcx tr_image_png tr_image_tga tr_noise"
# GLSL fallback shaders (glsl/*.glsl -> .c via the host stringify tool)
GLSL_SHADERS="bokeh_fp bokeh_vp calclevels4x_fp calclevels4x_vp depthblur_fp depthblur_vp dlight_fp dlight_vp down4x_fp down4x_vp fogpass_fp fogpass_vp generic_fp generic_vp lightall_fp lightall_vp pshadow_fp pshadow_vp shadowfill_fp shadowfill_vp shadowmask_fp shadowmask_vp ssao_fp ssao_vp texturecolor_fp texturecolor_vp tonemap_fp tonemap_vp"

# ---- generate GLSL fallback-shader .c files with the host stringify tool -----
STRINGIFY="$HERE/build/stringify"
if [ ! -x "$STRINGIFY" ] || [ "$Q/renderer2/stringify.c" -nt "$STRINGIFY" ]; then
  echo ">> building host stringify tool"
  # build with the HOST compiler on a clean PATH (BWRAP shadows as/ld with the
  # cross binutils, which would break a host link)
  env PATH=/usr/bin:/bin cc -O2 -o "$STRINGIFY" "$Q/renderer2/stringify.c" \
    >> "$LOG" 2>&1 || { echo "FATAL: could not build host stringify"; exit 1; }
fi
echo ">> stringify GLSL ($(echo $GLSL_SHADERS | wc -w) shaders)"
for s in $GLSL_SHADERS; do
  "$STRINGIFY" "$Q/renderer2/glsl/$s.glsl" "$GEN/$s.c" \
    || { echo "FATAL: stringify failed on $s.glsl"; exit 1; }
done

echo ">> compiling client (QNX armle-v7, SDP gcc 4.4.2)"
for b in $CLIENT; do compile "$Q/client/$b.c" "$JPGINC"; done
for b in $QCMN;   do compile "$Q/qcommon/$b.c" ""; done
for b in $VM;     do compile "$Q/qcommon/$b.c" "-fno-fast-math" vm; done
for b in $SRV;    do compile "$Q/server/$b.c" ""; done
for b in $BOT;    do compile "$Q/botlib/$b.c" "-DBOTLIB"; done
# bundled libjpeg (client cl_jpeg.c needs it)
for f in "$Q"/libjpeg/*.c; do compile "$f" "$JPGINC" jpg; done
# renderer2 (rend2) -> GLES2 via the qnx_gl2compat.h shim
for b in $REND2_R2; do compile "$Q/renderer2/$b.c" "$REND2INC"; done
for b in $REND2_RC; do compile "$Q/renderercommon/$b.c" "$REND2INC" rc; done
# generated GLSL fallback-shader strings
for s in $GLSL_SHADERS; do compile "$GEN/$s.c" "" glsl; done
# QNX platform layer (glimp = real EGL/GLES2; input + sound stubs)
for b in qnx_glimp qnx_input qnx_snd qnx_ilm; do compile "$HERE/qnx/$b.c" "$QNXINC"; done
# unix sys layer
for b in $UNX; do compile "$Q/unix/$b.c" ""; done

echo ">> compiled $ok/$n objects"
if [ -n "$FAILED" ]; then
  echo ">> FAILED:$FAILED"
  echo ">> errors: grep -nE 'error:' $LOG"
  exit 2
fi

echo ">> linking $BIN"
echo "=== LINK" >> "$LOG"
if $CC -o "$BIN" $OBJS $LDFLAGS >> "$LOG" 2>&1; then
  [ "${DEBUG:-0}" = "1" ] || "$SDP_STRIP" "$BIN" 2>/dev/null
  echo ">> OK: $BIN"; ls -l "$BIN"; file "$BIN" 2>/dev/null || true
else
  echo ">> LINK FAILED - undefined/multiple symbols:"
  grep -nE "undefined reference|multiple definition|In function" "$LOG" | tail -50
  exit 3
fi

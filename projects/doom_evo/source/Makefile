# EVO DOOM - Shared Library Version  
# Enhanced version that uses the shared EVO display library
# Based on proven EVO buiALL_SRCS := $(DOOM_CORE_SRCS) $(DOOM_RENDERER_SRCS) $(DOOM_GAME_SRCS) \
            $(DOOM_INTERFACE_SRCS) $(DOOM_SYSTEM_SRCS) $(EVO_SRCS) $(LEGACY_SRCS)

# VPATH tells make where to find source files
VPATH = .:$(SHARED_DIR)/legacy_graphics_lib/srcnfiguration

# Build configuration (can be overridden)
BUILD_TYPE ?= release

# Toolchain: the QNX 6.5 SDP gcc 4.4.2 (shared/sdp.mk) - provides CC, SDP_CPU,
# QNX_INCLUDE, QNX_LIB, QNX_LIB_USR, CRT1/CRTI/CRTN, SDP_STRIP + the sdp_binutils
# prep target. (Was the vendored Linaro 4.7.3 + toolchain/qnx660.)
include ../../../shared/sdp.mk
STRIP := $(SDP_STRIP)

# EVO Graphics Library v2.0 paths (NEW - Hardware accelerated)
EVO_GRAPHICS_DIR := ../../../shared/evo_graphics_lib
EVO_GRAPHICS_INCLUDE := $(EVO_GRAPHICS_DIR)/include
EVO_GRAPHICS_SRC := $(EVO_GRAPHICS_DIR)/src

# Legacy shared library paths (for compatibility during transition)
SHARED_DIR := ../../../shared
SHARED_INCLUDE := $(SHARED_DIR)/include
SHARED_SRC := $(SHARED_DIR)/src

# Build directories
BUILD_DIR := build
BIN_DIR := ../../../bin

# EVO-specific CFLAGS (conservative ARM7TDMI with fixed point math)
CFLAGS_COMMON := $(SDP_CPU) -fsigned-char -O0 \
          -fno-strict-aliasing -fno-builtin -fno-inline -fno-common \
          -fno-omit-frame-pointer -DDOOM_FIXED_POINT \
          -I$(QNX_INCLUDE) -I. -Icompat -I$(SHARED_INCLUDE) -I$(EVO_GRAPHICS_INCLUDE) \
          -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ \
          -D_QNX_SOURCE -DNORMALUNIX -DQNX -DNO_X11 -DHAVE_UNISTD_H \
          -Wno-implicit-int -Wno-implicit-function-declaration -Wno-unused-variable \
          -Wall -std=c99

# Debug CFLAGS (conservative ARM7TDMI with fixed point math)
DEBUG_CFLAGS := $(SDP_CPU) -fsigned-char -g3 -O0 -DDEBUG \
                -fno-omit-frame-pointer \
                -fno-strict-aliasing -fno-builtin -fno-inline -fno-common \
                -DDOOM_FIXED_POINT \
                -I$(QNX_INCLUDE) -I. -Icompat -I$(SHARED_INCLUDE) -I$(EVO_GRAPHICS_INCLUDE) \
                -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ \
                -D_QNX_SOURCE -DNORMALUNIX -DQNX -DNO_X11 -DHAVE_UNISTD_H \
                -Wno-implicit-int -Wno-implicit-function-declaration -Wno-unused-variable \
                -Wall -std=c99

# Select CFLAGS based on build type
ifeq ($(BUILD_TYPE),debug)
    CFLAGS := $(DEBUG_CFLAGS)
else
    CFLAGS := $(CFLAGS_COMMON)
endif

# Target library paths (proven EVO configuration)
TARGET_LIB := ../../../toolchain/target/lib
TARGET_LIB_OPT := ../../../toolchain/target/opt/sys/lib

# EVO dynamic linking (proven configuration with runtime paths)
LDFLAGS := -L$(QNX_LIB) -L$(QNX_LIB_USR) -L$(TARGET_LIB) -L$(TARGET_LIB_OPT) -nostdlib \
           -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 \
           -Wl,-rpath-link,$(QNX_LIB) -Wl,-rpath-link,$(QNX_LIB_USR) \
           -Wl,-rpath-link,$(TARGET_LIB) -Wl,-rpath-link,$(TARGET_LIB_OPT) \
           -Wl,-rpath,/fs/sda0/opt/sys/lib -Wl,-rpath,/opt/sys/lib

# Basic QNX libraries (proven configuration)
LIBS := $(TARGET_LIB)/libscreen.so.1 -lsocket -l:libasound.so.2 -l:libm.so.2 -l:libc.so.3 -lgcc

# CRT objects (required for proper QNX linking) - from the SDP (shared/sdp.mk)
CRT_START := $(CRT1) $(CRTI)
CRT_END := $(CRTN)

# Source files (organize by component)
DOOM_CORE_SRCS := \
    doomdef.c doomstat.c dstrings.c tables.c \
    d_main.c d_net.c d_items.c g_game.c

DOOM_RENDERER_SRCS := \
    r_bsp.c r_data.c r_draw.c r_main.c \
    r_plane.c r_segs.c r_sky.c r_things.c

DOOM_GAME_SRCS := \
    p_ceilng.c p_doors.c p_enemy.c p_floor.c \
    p_inter.c p_lights.c p_map.c p_maputl.c \
    p_plats.c p_pspr.c p_setup.c p_sight.c \
    p_spec.c p_switch.c p_mobj.c p_telept.c \
    p_tick.c p_saveg.c p_user.c

DOOM_INTERFACE_SRCS := \
    m_menu.c m_misc.c m_argv.c m_bbox.c \
    m_fixed.c m_swap.c m_cheat.c m_random.c \
    am_map.c st_lib.c st_stuff.c hu_stuff.c \
    hu_lib.c wi_stuff.c f_finale.c f_wipe.c

DOOM_SYSTEM_SRCS := \
    i_main.c i_net_evo.c s_sound.c v_video.c \
    w_wad.c z_zone.c info.c sounds.c

# Platform-specific sources - EVO device implementation (input, audio, video)
EVO_SRCS := i_system_evo.c i_sound_evo.c i_oplmusic_evo.c opl3.c i_video_evo_v2.c i_input_evo.c sndserver_filename.c evo_compat.c

# EVO Graphics Library v2.0 sources (Hardware accelerated, unified API)
EVO_GRAPHICS_SRCS := $(EVO_GRAPHICS_SRC)/evo_graphics_api.c \
                     $(EVO_GRAPHICS_SRC)/evo_core_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_display_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_backend_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_cairo_text_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_logging.c

ALL_SRCS := $(DOOM_CORE_SRCS) $(DOOM_RENDERER_SRCS) $(DOOM_GAME_SRCS) \
            $(DOOM_INTERFACE_SRCS) $(DOOM_SYSTEM_SRCS) $(EVO_SRCS) $(EVO_GRAPHICS_SRCS)

# VPATH tells make where to find source files
VPATH = .:$(EVO_GRAPHICS_SRC)

# Create necessary directories
$(shell mkdir -p $(BIN_DIR))

# Build targets
.PHONY: all clean install help debug release strip deploy

all: $(BIN_DIR)/doom_evo

# Main build target (using proven EVO linking pattern)
$(BIN_DIR)/doom_evo: $(ALL_SRCS) | sdp_binutils
	@echo "Building DOOM with shared EVO display library..."
	@mkdir -p $(BIN_DIR)
	$(CC) $(CRT_START) $(CFLAGS) $(ALL_SRCS) $(CRT_END) $(LDFLAGS) $(LIBS) -lm -o $@
	@echo "Build complete: $@"
	@echo "Size: `du -h $@ | cut -f1`"

# Strip target to reduce binary size for deployment
strip: $(BIN_DIR)/doom_evo
	@echo "Stripping binary for deployment..."
	@cp $(BIN_DIR)/doom_evo $(BIN_DIR)/doom_evo.unstripped
	@$(STRIP) --strip-unneeded $(BIN_DIR)/doom_evo
	@echo "Original size: `du -h $(BIN_DIR)/doom_evo.unstripped | cut -f1`"
	@echo "Stripped size: `du -h $(BIN_DIR)/doom_evo | cut -f1`"
	@echo "Backup saved as: $(BIN_DIR)/doom_evo.unstripped"

# Deployment target for EVO device
deploy_doom_evo:
	@echo "📦 Deploying doom_evo test..."
	../../../scripts/evo_deploy_test.sh test doom_evo




clean:
	rm -rf build/


help:
	@echo "EVO DOOM - Shared Library Build System"
	@echo "======================================"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build release version with shared library"
	@echo "  debug    - Build debug version"
	@echo "  release  - Build release version"
	@echo "  info     - Build and show binary info"
	@echo "  strip    - Strip the binary for optimized deployment"
	@echo "  deploy   - Deploy to EVO target (set EVO_TARGET_IP env var)"
	@echo "  debug-symbols - Build with enhanced debug symbols for GDB"
	@echo "  clean    - Clean all build files"
	@echo "  install  - Show installation instructions"
	@echo "  help     - Show this help"
	@echo ""
	@echo "Features:"
	@echo "  - Uses shared EVO display library for simplified graphics"
	@echo "  - Automatic Screen API + ILM integration"
	@echo "  - Production surface/layer ID management"
	@echo "  - Robust error handling and fallback"
	@echo "  - Based on proven EVO build system"
	@echo ""

# Variant builds
debug:
	$(MAKE) BUILD_TYPE=debug all

release:
	$(MAKE) BUILD_TYPE=release all

# Test target to verify the build
test:
	@if [ ! -f "$(BIN_DIR)/doom_evo" ]; then \
		echo "Binary not found. Building first..."; \
		$(MAKE) all; \
	fi
	@echo ""
	@echo "============================================="
	@echo "DOOM EVO Build Results"
	@echo "============================================="
	@echo "Binary: $(BIN_DIR)/doom_evo"
	@echo "Size: `du -h $(BIN_DIR)/doom_evo | cut -f1`"
	@echo ""
	@echo "Runtime libraries required:"
	@echo "  • libscreen.so.1"
	@echo "  • libm.so.2"  
	@echo "  • libc.so.3"
	@echo "  • libsocket.so"
	@echo ""
	@echo "Binary type:"
	@file $(BIN_DIR)/doom_evo | grep -q "ARM" && echo "✅ ARM binary (correct)" || echo "❌ Not an ARM binary"
	@echo ""
	@echo "SIGILL diagnostic options:"
	@echo "  • Use 'make debug-symbols' for better crash diagnostics"
	@echo "  • See DEBUG_GUIDE.md for more information"
	@echo "============================================="

# ============================================================================
# app.mk - shared build rules for NBT EVO head-unit homebrew apps.
#
# Every on-unit binary in this repo is meant to be a launcher app, built the
# same way and launched through evo_launcher. This include captures the build
# profile so a new app's Makefile is three lines:
#
#     APP  := evo_foo
#     SRCS := evo_foo.c
#     include ../../shared/app.mk
#
# Then:  make        -> builds ../../bin/evo_foo (ARM QNX binary)
#        make clean  -> removes it
#
# ---------------------------------------------------------------------------
# Compiled with the QNX 6.5.0 SDP's own gcc 4.4.2, driven directly
# (arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2 + ntoarmv7 binutils) - the real QNX
# toolchain for the unit's QNX 6.5.0. Point QNX_SDP at your SDP install.
#
# ---------------------------------------------------------------------------
# Knobs a project may set BEFORE the include (all optional except APP + SRCS):
#   APP            output binary name (required), lands in $(BIN_DIR)
#   SRCS           app-specific source files (required), e.g. "evo_foo.c"
#   GRAPHICS       1 (default) = compile in evo_graphics_lib (full-screen apps);
#                  0 = no graphics lib (small utilities, e.g. evo_screenshot)
#   USE_DSI        1 = also link the DSI servicebroker (apps that read unit
#                  services over DSI, e.g. evo_gps)
#   OPT            optimisation level, default -O0
#   FAST_SRCS      sources compiled on their own at FAST_OPT (default -O2) and
#                  linked in - for hot rendering code in an otherwise -O0 app.
#                  (gcc 4.4.2 crashes on "#pragma GCC optimize", so per-file
#                  optimisation has to be done here.)
#   STRIP          1 = strip the output binary (default 0)
#   KIND           exe (default) | so  (LD_PRELOAD / shared object)
#   QNX_SDP        path to the QNX 6.5 SDP (default $(HOME)/qnx650_sdp/qnx650)
#   EXTRA_CFLAGS / EXTRA_INCLUDES / EXTRA_LIBS / EXTRA_LDFLAGS
#   R              repo root relative to the project (default ../..)
# ============================================================================

# ---- required knobs -------------------------------------------------------
ifndef APP
$(error app.mk: set APP (output binary name) before including app.mk)
endif
ifndef SRCS
$(error app.mk: set SRCS (app source files) before including app.mk)
endif

# ---- defaults -------------------------------------------------------------
R         ?= ../..
GRAPHICS  ?= 1
USE_DSI   ?= 0
OPT       ?= -O0
FAST_SRCS ?=
FAST_OPT  ?= -O2
STRIP     ?= 0
KIND      ?= exe
QNX_SDP   ?= $(HOME)/qnx650_sdp/qnx650

# ---- shared code paths (toolchain-independent) ----------------------------
EVO_GRAPHICS_DIR     := $(R)/shared/evo_graphics_lib
EVO_GRAPHICS_INCLUDE := $(EVO_GRAPHICS_DIR)/include
EVO_GRAPHICS_SRC     := $(EVO_GRAPHICS_DIR)/src
SHARED_INCLUDE       := $(R)/shared/include
# TARGET_LIB / TARGET_LIB_OPT: libraries pulled off the unit (libscreen, ILM, ...)
TARGET_LIB           := $(R)/toolchain/target/lib
TARGET_LIB_OPT       := $(R)/toolchain/target/opt/sys/lib
BIN_DIR              := $(R)/bin
BUILD                := $(R)/build

# ---- toolchain: the QNX 6.5 SDP's gcc 4.4.2, driven directly --------------
QNX_HOST        := $(QNX_SDP)/host/linux/x86
QNX_TARGET      := $(QNX_SDP)/target/qnx6
CC_BIN          := $(QNX_HOST)/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2
STRIP_BIN       := $(QNX_HOST)/usr/bin/ntoarmv7-strip-2.19
QNX_INCLUDE     := $(QNX_TARGET)/usr/include
QNX_INCLUDE_USR := $(QNX_TARGET)/usr/include
QNX_LIB         := $(QNX_TARGET)/armle-v7/lib
QNX_LIB_USR     := $(QNX_TARGET)/armle-v7/usr/lib
CPU_FLAGS       := -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp
# The SDP gcc's collect2 looks for as/ld/ar/nm by bare name; provide them as
# symlinks to the ntoarmv7 binutils and point gcc there with -B + PATH.
SDP_BINUTILS    := $(BUILD)/sdp-binutils
CC              := QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) PATH="$(SDP_BINUTILS):$$PATH" $(CC_BIN) -B$(SDP_BINUTILS)

# evo_graphics_lib core sources (the unified Screen+ILM backend).
EVO_GRAPHICS_SRCS := $(EVO_GRAPHICS_SRC)/evo_graphics_api.c \
                     $(EVO_GRAPHICS_SRC)/evo_core_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_display_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_backend_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_cairo_text_unified.c \
                     $(EVO_GRAPHICS_SRC)/evo_logging.c

ifeq ($(GRAPHICS),1)
  GRAPHICS_SRCS := $(EVO_GRAPHICS_SRCS)
  GRAPHICS_INC  := -I$(EVO_GRAPHICS_INCLUDE)
else
  GRAPHICS_SRCS :=
  GRAPHICS_INC  :=
endif

# ---- DSI service-broker (apps that talk to unit services, e.g. gps) ---
DSI_SDK := $(R)/projects/nbt-dsi-toolkit/dsi_sdk
ifeq ($(USE_DSI),1)
  DSI_INC     := -I$(DSI_SDK)/include
  DSI_LIB     := -l:libsys_dsi_servicebroker.cpp-ne.so.4
  DSI_LDFLAGS := -L$(DSI_SDK)/lib -Wl,-rpath-link,$(DSI_SDK)/lib
else
  DSI_INC     :=
  DSI_LIB     :=
  DSI_LDFLAGS :=
endif

# ---- flags ----------------------------------------------------------------
# The shared app headers (font8x8_basic.h, evo_feed.h, evo_appinput.h,
# evo_hmistate.h) all live in shared/include, so the canonical -I list reaches
# the font, the iDrive feed ring and the BACK-to-exit helper for every app.
INCLUDES := -I. -I$(QNX_INCLUDE) -I$(QNX_INCLUDE_USR) -I$(SHARED_INCLUDE) \
            $(GRAPHICS_INC) $(DSI_INC) $(EXTRA_INCLUDES)

CFLAGS := -marm $(CPU_FLAGS) $(OPT) \
          -fno-strict-aliasing -fno-builtin -fno-common -fno-omit-frame-pointer \
          $(INCLUDES) \
          -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ \
          -D_QNX_SOURCE -DNORMALUNIX -DQNX -DNO_X11 -DHAVE_UNISTD_H \
          -Wno-implicit-function-declaration -Wno-unused-variable -Wall -std=gnu99 \
          $(EXTRA_CFLAGS)

LDFLAGS := -L$(QNX_LIB) -L$(QNX_LIB_USR) -L$(TARGET_LIB) -L$(TARGET_LIB_OPT) -nostdlib \
           -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 \
           -Wl,-rpath-link,$(QNX_LIB) -Wl,-rpath-link,$(QNX_LIB_USR) \
           -Wl,-rpath-link,$(TARGET_LIB) -Wl,-rpath-link,$(TARGET_LIB_OPT) \
           $(DSI_LDFLAGS) $(EXTRA_LDFLAGS)

LIBS := $(TARGET_LIB)/libscreen.so.1 $(DSI_LIB) -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc $(EXTRA_LIBS)

CRT_START := $(QNX_LIB)/crt1.o $(QNX_LIB)/crti.o
CRT_END   := $(QNX_LIB)/crtn.o

ALL_SRCS := $(SRCS) $(GRAPHICS_SRCS)
OUTPUT   := $(BIN_DIR)/$(APP)
FAST_OBJS := $(patsubst %.c,$(BUILD)/obj/$(APP)/%.o,$(notdir $(FAST_SRCS)))

# ---- toolchain prep (SDP: lay down the binutils symlinks) -----------------
.PHONY: all clean tc_prep FORCE
.DEFAULT_GOAL := all
tc_prep:
	@mkdir -p $(SDP_BINUTILS)
	@for t in as ld ar nm; do ln -sf $(QNX_HOST)/usr/bin/ntoarmv7-$$t-2.19 $(SDP_BINUTILS)/$$t; done
	@ln -sf $(QNX_HOST)/usr/bin/ntoarmv7-ld-2.19 $(SDP_BINUTILS)/real-ld

# ---- rules ----------------------------------------------------------------
all: $(OUTPUT)

# FAST_SRCS objects: always rebuilt (no header dependency tracking in this file).
$(BUILD)/obj/$(APP)/%.o: %.c FORCE | tc_prep
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(FAST_OPT) -c $< -o $@

ifeq ($(KIND),so)
# Shared object (LD_PRELOAD etc.): no crt1.o, -shared -fPIC, soname.
$(OUTPUT): $(ALL_SRCS) | tc_prep
	@mkdir -p $(BIN_DIR)
	$(CC) -shared -fPIC $(CRT_START:$(QNX_LIB)/crt1.o=) $(CFLAGS) $(ALL_SRCS) $(CRT_END) \
	  $(LDFLAGS) -Wl,-soname,$(APP) $(LIBS) -lm -o $@
	@file $@ | grep -q ARM && echo "OK: ARM shared object $@" || echo "WARN: not ARM: $@"
else
# Plain QNX executable.
$(OUTPUT): $(ALL_SRCS) $(FAST_OBJS) | tc_prep
	@mkdir -p $(BIN_DIR)
	$(CC) $(CRT_START) $(CFLAGS) $(ALL_SRCS) $(FAST_OBJS) $(CRT_END) $(LDFLAGS) $(LIBS) -lm -o $@
ifeq ($(STRIP),1)
	$(STRIP_BIN) $@
endif
	@file $@ | grep -q ARM && echo "OK: ARM binary $@" || echo "WARN: not ARM: $@"
endif

clean:
	rm -f $(OUTPUT)

# doom_evo controller tools - cross build for QNX 6.5.0 armle-v7 (NBT EVO OMAP).
#
#   make            build everything into build/
#   make <name>     e.g. make evo_feedtap
#   make clean
#
# These are the DOOM/touch-specific helpers (feed ring, touchComDevice tee/shim,
# io-ipc experiments). The general DSI client + servicebroker tools moved to the
# standalone ../../nbt-dsi-toolkit project (the DSI client runs as its own process
# and feeds DOOM via the /dev/shmem/evo_ctrl_feed ring; see evo_feed.h).
#
# Toolchain: the QNX 6.5.0 SDP's own gcc 4.4.2, driven directly (via shared/sdp.mk)
# - the real QNX toolchain for the unit (armv7-a, VFPv3-D16, softfp). This replaced
# the retired vendored Linaro gcc 4.7.3 + toolchain/qnx650. gcc 4.4.2 does NOT accept
# -mno-unaligned-access (dropped; armv7-a handles unaligned in hardware).
#
# Tool groups:
#   EXES - plain QNX executables (link libc only)
#   SOS  - LD_PRELOAD shared objects

R        := ../../..
include $(R)/shared/sdp.mk      # CC, SDP_CPU, QNX_INCLUDE, QNX_LIB, CRT1/CRTI/CRTN, sdp_binutils

TLIB     := $(R)/toolchain/target/lib
BUILD    := build

CFLAGS := $(SDP_CPU) -O0 -g \
          -fno-strict-aliasing -fno-builtin -fno-common -std=gnu99 -Wall \
          -I$(QNX_INCLUDE) -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ -D_QNX_SOURCE

LDFLAGS := -L$(QNX_LIB) -L$(TLIB) -nostdlib -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 \
           -Wl,-rpath-link,$(QNX_LIB) -Wl,-rpath-link,$(TLIB)
LIBS    := -l:libc.so.3 -lgcc

EXES     := evo_ctrl_probe evo_ctrlmon evo_ipctap evo_ipctap2 evo_ipcinject evo_feedtap \
            evo_tcd_shim evo_most_shim evo_boot_shim evo_trace_shim rdtest evo_inject evo_applaunchd evo_feedpush
SO_NAMES := evo_tee evo_ctrl_inject
SO_TARGETS := $(SO_NAMES:%=$(BUILD)/%.so)

# evo_ctrlfeed: the iDrive-controller DSI subscriber that WRITES the DOOM feed
# (/dev/shmem/evo_ctrl_feed). It uses the shared DSI SDK from the nbt-dsi-toolkit
# sibling project (servicebroker + BSD sockets), like that project's evo_dsitcp.
SDK         := $(R)/projects/nbt-dsi-toolkit/dsi_sdk
DSI_CFLAGS  := $(CFLAGS) -I$(SDK)/include
DSI_LDFLAGS := -L$(QNX_LIB) -L$(TLIB) -L$(SDK)/lib -nostdlib \
               -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 \
               -Wl,-rpath-link,$(QNX_LIB) -Wl,-rpath-link,$(TLIB) -Wl,-rpath-link,$(SDK)/lib \
               -Wl,--allow-shlib-undefined
DSI_LIBS    := -l:libsys_dsi_servicebroker.cpp-ne.so.4 -l:libsocket.so.3 -l:libc.so.3 -lgcc

all: $(EXES:%=$(BUILD)/%) $(SO_TARGETS) $(BUILD)/evo_ctrlfeed

# convenience: `make evo_feedtap`, `make evo_tee.so`, `make evo_ctrlfeed`
$(EXES): %: $(BUILD)/%
evo_ctrlfeed: $(BUILD)/evo_ctrlfeed
$(SO_NAMES:%=%.so): %.so: $(BUILD)/%.so ;

$(BUILD)/evo_ctrlfeed: evo_ctrlfeed.c evo_feed.h | $(BUILD) sdp_binutils
	$(CC) $(CRT1) $(CRTI) $(DSI_CFLAGS) $< $(CRTN) $(DSI_LDFLAGS) $(DSI_LIBS) -o $@
	@echo "built $@ (DSI controller feeder)"

$(EXES:%=$(BUILD)/%): $(BUILD)/%: %.c | $(BUILD) sdp_binutils
	$(CC) $(CRT1) $(CRTI) $(CFLAGS) $< $(CRTN) $(LDFLAGS) $(LIBS) -o $@
	@echo "built $@"

$(SO_TARGETS): $(BUILD)/%.so: %.c | $(BUILD) sdp_binutils
	$(CC) -shared -fPIC -nostdlib $(CFLAGS) $(CRTI) $< $(CRTN) \
	  -L$(QNX_LIB) -L$(TLIB) -Wl,-rpath-link,$(QNX_LIB) -Wl,-rpath-link,$(TLIB) \
	  -Wl,-soname,$(notdir $@) $(LIBS) -o $@
	@echo "built $@ (shared)"

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean evo_ctrlfeed $(EXES) $(SO_NAMES:%=%.so)

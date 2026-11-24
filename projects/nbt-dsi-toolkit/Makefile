# nbt-dsi-toolkit - cross-build DSI client tools for the BMW NBT EVO head unit
# (QNX 6.5.0, armle-v7 / Cortex-A15, little-endian).
#
#   make            build everything into build/
#   make evo_dsitcp build one tool
#   make clean
#
# Requirements:
#   * the QNX 6.5.0 SDP: its gcc 4.4.2 + armle-v7 target libs/headers. Override
#     QNX_SDP if your SDP lives elsewhere (see shared/sdp.mk; defaults to
#     $HOME/qnx650_sdp/qnx650):
#         make QNX_SDP=/path/to/qnx650
#   * dsi_sdk/  : DSI SDK headers (open-source DSIHarman) + the unit's runtime libs.
#     The libs under dsi_sdk/lib/ are PROPRIETARY (pulled from your own unit) - do
#     NOT redistribute them; they already exist on every NBT. See NOTICE.
#
# On the unit, run the tools with:
#   LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib

# QNX 6.5 SDP toolchain: provides $(CC) (gcc 4.4.2 driven directly), $(SDP_CPU),
# $(QNX_INCLUDE), $(QNX_LIB), $(CRT1)/$(CRTI)/$(CRTN) and the sdp_binutils target
# (order-only dep so collect2 finds as/ld). QNX_SDP is overridable there (?=).
include ../../shared/sdp.mk

# unit libraries pulled off the car (libcpp-ne, libbacktrace, ... that the
# proprietary DSI libs below have as NEEDED deps) - these are NOT part of the
# retired Linaro/qnx650 toolchain; target/lib stays.
TLIB    := ../../toolchain/target/lib
SDK     := dsi_sdk
BUILD   := build

CFLAGS := $(SDP_CPU) -O0 -g \
          -fno-strict-aliasing -fno-builtin -fno-common -std=gnu99 -Wall \
          -I$(QNX_INCLUDE) -I$(SDK)/include \
          -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ -D_QNX_SOURCE

LDFLAGS := -L$(QNX_LIB) -L$(TLIB) -L$(SDK)/lib -nostdlib \
           -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 \
           -Wl,-rpath-link,$(QNX_LIB) -Wl,-rpath-link,$(TLIB) -Wl,-rpath-link,$(SDK)/lib \
           -Wl,--allow-shlib-undefined

SB   := -l:libsys_dsi_servicebroker.cpp-ne.so.4
COL  := -l:libsys_dsi_colibry.cpp-ne.so.8
SOCK := -l:libsocket.so.3
LIBC := -l:libc.so.3 -lgcc

# tool -> extra libs it needs beyond libc
#   evo_dsitcp : servicebroker + BSD sockets (the MoCCA-over-TCP client)
#   others     : servicebroker (+ colibry for the convenience SBGetInterfaceList calls)
#   evo_dsiqnx : servicebroker only (the NATIVE QNX message-passing client:
#                ConnectAttach + MsgSend over the services' QNX channels)
NET_TOOLS := evo_dsitcp
QNX_TOOLS := evo_dsiqnx evo_dsicall evo_navctl
SB_TOOLS  := evo_sbtcp evo_sbmap evo_sblist evo_dsi
TOOLS     := $(NET_TOOLS) $(QNX_TOOLS) $(SB_TOOLS)

all: $(TOOLS:%=$(BUILD)/%)
$(TOOLS): %: $(BUILD)/%

$(NET_TOOLS:%=$(BUILD)/%): $(BUILD)/%: src/%.c | $(BUILD) sdp_binutils
	$(CC) $(CRT1) $(CRTI) $(CFLAGS) $< $(CRTN) $(LDFLAGS) $(SB) $(SOCK) $(LIBC) -o $@
	@echo "built $@"

$(QNX_TOOLS:%=$(BUILD)/%): $(BUILD)/%: src/%.c | $(BUILD) sdp_binutils
	$(CC) $(CRT1) $(CRTI) $(CFLAGS) $< $(CRTN) $(LDFLAGS) $(SB) $(LIBC) -o $@
	@echo "built $@ (native QNX MP DSI client)"

$(SB_TOOLS:%=$(BUILD)/%): $(BUILD)/%: src/%.c | $(BUILD) sdp_binutils
	$(CC) $(CRT1) $(CRTI) $(CFLAGS) $< $(CRTN) $(LDFLAGS) $(COL) $(SB) $(LIBC) -o $@
	@echo "built $@"

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean $(TOOLS)

# shared/sdp.mk - QNX 6.5 SDP toolchain vars for Makefiles that are NOT app.mk
# (the DOOM engine, controller tools, DSI toolkit, ...). app.mk has its own copy
# of this for launcher apps; this is for builds with their own flag sets.
#
# Use it:
#   include ../../shared/sdp.mk        # (adjust depth)
#   ...compile/link with $(CC) + $(SDP_CPU) + your own -I/-D/-W flags...
#   ...QNX headers/libs/crt via $(QNX_INCLUDE) $(QNX_LIB) $(CRT1) $(CRTI) $(CRTN)...
#   $(BIN): ... | sdp_binutils       # order-only dep so gcc's collect2 finds as/ld
#
# Override QNX_SDP to point at your SDP install. The SDP gcc (4.4.2) is OLDER
# than the retired Linaro 4.7.3: it does NOT accept -mno-unaligned-access
# (drop it; armv7-a handles unaligned in hardware). Keep -fsigned-char where a
# codebase (e.g. DOOM) relies on signed char.
QNX_SDP      ?= $(HOME)/qnx650_sdp/qnx650
QNX_HOST     := $(QNX_SDP)/host/linux/x86
QNX_TARGET   := $(QNX_SDP)/target/qnx6
SDP_GCC      := $(QNX_HOST)/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2
SDP_STRIP    := $(QNX_HOST)/usr/bin/ntoarmv7-strip-2.19
SDP_BWRAP    := $(QNX_SDP)/bwrap

# Drive the SDP gcc directly: set QNX_HOST/QNX_TARGET,
# put the ntoarmv7 binutils (symlinked as as/ld/ar/nm under SDP_BWRAP) on PATH
# and -B so collect2 finds them.
CC           := QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) PATH="$(SDP_BWRAP):$$PATH" $(SDP_GCC) -B$(SDP_BWRAP)

SDP_CPU      := -marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp
QNX_INCLUDE  := $(QNX_TARGET)/usr/include
QNX_LIB      := $(QNX_TARGET)/armle-v7/lib
QNX_LIB_USR  := $(QNX_TARGET)/armle-v7/usr/lib
CRT1         := $(QNX_LIB)/crt1.o
CRTI         := $(QNX_LIB)/crti.o
CRTN         := $(QNX_LIB)/crtn.o

# sdp.mk is included early, so its target below would otherwise become make's
# default goal. Every consumer here uses `all` as its default, so pin it (an
# explicit `make <target>` still overrides this).
.DEFAULT_GOAL := all

.PHONY: sdp_binutils
sdp_binutils:
	@mkdir -p $(SDP_BWRAP)
	@for t in as ld ar nm; do ln -sf $(QNX_HOST)/usr/bin/ntoarmv7-$$t-2.19 $(SDP_BWRAP)/$$t; done
	@ln -sf $(QNX_HOST)/usr/bin/ntoarmv7-ld-2.19 $(SDP_BWRAP)/real-ld

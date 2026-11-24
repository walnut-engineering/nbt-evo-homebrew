# evo_gltest - EGL/GLES2 probe (direct PowerVR). GRAPHICS=0 (no evo_graphics_lib);
# links the EGL + GLESv2 client libs (from toolchain/target/lib, on the SDP -L path).
APP       := evo_gltest
SRCS      := evo_gltest.c
GRAPHICS  := 0
OPT       := -O2
EXTRA_LIBS := -l:libEGL.so.1 -l:libGLESv2.so.1
include ../../shared/app.mk

# second probe: the forwarding-EGL test (dlopen only, no GPU libs linked)
all: $(BIN_DIR)/evo_glfwd
$(BIN_DIR)/evo_glfwd: evo_glfwd.c | tc_prep
	$(CC) $(CRT_START) $(CFLAGS) evo_glfwd.c $(CRT_END) $(LDFLAGS) -l:libc.so.3 -lgcc -lm -o $@
	@file $@ | grep -q ARM && echo "OK: ARM binary $@" || echo "WARN: not ARM: $@"

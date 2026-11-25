# evo_retro - SNES and Sega Genesis tiles: one libretro frontend (this directory),
# linked statically with one emulator core per binary.
#
#   ./cores.sh              first: fetch + build the cores (not part of this repo)
#   make                    -> ../../bin/evo_genesis   (Genesis Plus GX)
#   make SYSTEM=snes        -> ../../bin/evo_snes      (Snes9x 2010)
#   ./build.sh              all of the above
#
# See ../../shared/app.mk for the build profile and PLAN.md for the design.
SYSTEM ?= genesis
ifeq ($(SYSTEM),snes)
  APP     := evo_snes
  SYS_DEF := -DEVO_RETRO_SNES
else ifeq ($(SYSTEM),genesis)
  APP     := evo_genesis
  SYS_DEF := -DEVO_RETRO_GENESIS
else
  $(error SYSTEM must be genesis or snes)
endif

SRCS := evo_retro.c retro_input.c retro_video.c retro_audio.c retro_ui.c retro_opts.c retro_zip.c
# the scaler runs every frame: optimise the whole (small) frontend
OPT  := -O2
EXTRA_CFLAGS := $(SYS_DEF) -Wno-unused-function
CORE_LIB := ../../build/evo_retro/libretro_$(SYSTEM).a
# the core comes after everything that uses it, and needs libgcc's helpers once more
EXTRA_LIBS := -l:libasound.so.2 $(CORE_LIB) -lgcc
include ../../shared/app.mk

$(OUTPUT): $(CORE_LIB) retro_fe.h libretro.h
# (a test, not a plain failure: `make -B`, which build.sh uses to switch systems, runs this rule too)
$(CORE_LIB):
	@test -f $@ || { echo "missing $@ - run ./cores.sh first"; false; }

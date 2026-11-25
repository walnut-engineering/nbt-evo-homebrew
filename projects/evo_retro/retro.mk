# retro.mk - build rules shared by the tiles made of the evo_retro frontend (this
# directory) and one emulator core. Each such tile is its own project folder whose
# Makefile is three lines:
#
#     APP    := evo_snes        # the binary, ../../bin/$(APP)
#     SYSTEM := snes            # selects -DEVO_RETRO_SNES in the frontend (retro_fe.h)
#     include ../evo_retro/retro.mk
#
# The tile's folder also holds ./core.sh, which fetches and builds its emulator core
# into ../../build/evo_retro/libretro_<name>.a. `make` runs it when that library is
# missing; after that only the frontend is rebuilt.
#
#     make                  frontend + the tile's default core
#     make CORE=<name>      link another library core.sh can build (e.g. snes2005)
#
# See ../../shared/app.mk for the build profile and PLAN.md for the design.
ifndef SYSTEM
$(error retro.mk: set SYSTEM (snes | genesis) before including retro.mk)
endif
FE     := ../evo_retro
CORE   ?= $(SYSTEM)
SYS_UP := $(shell echo $(SYSTEM) | tr a-z A-Z)

SRCS := $(addprefix $(FE)/,evo_retro.c retro_input.c retro_video.c retro_audio.c retro_ui.c retro_opts.c retro_zip.c)
# the scaler runs every frame: optimise the whole (small) frontend
OPT  := -O2
EXTRA_CFLAGS   := -DEVO_RETRO_$(SYS_UP) -Wno-unused-function
EXTRA_INCLUDES := -I$(FE)
CORE_LIB := ../../build/evo_retro/libretro_$(CORE).a
# the core comes after everything that uses it, and needs libgcc's helpers once more
EXTRA_LIBS := -l:libasound.so.2 $(CORE_LIB) -lgcc
include ../../shared/app.mk

# relink when the core library, the frontend's headers or the choice of core changed
CORE_MARK := ../../build/evo_retro/$(APP).core
$(OUTPUT): $(CORE_LIB) $(CORE_MARK) $(FE)/retro_fe.h $(FE)/libretro.h $(FE)/retro.mk
$(CORE_MARK): FORCE
	@mkdir -p $(dir $@)
	@echo "$(CORE)" | cmp -s - $@ || echo "$(CORE)" > $@
# (a test, not a plain recipe: `make -B` runs this rule even when the library is there)
$(CORE_LIB):
	@test -f $@ || { echo "== $(APP): the '$(CORE)' core is not built yet - fetching and building it (./core.sh $(CORE))"; ./core.sh $(CORE); }

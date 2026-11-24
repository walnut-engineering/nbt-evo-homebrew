# evo_launcher - resident homebrew app launcher overlay for the NBT EVO head unit.
# Build profile + rules are shared across every app; see ../../shared/app.mk.
APP  := evo_launcher
SRCS := evo_launcher.c
# the renderer is the hot path (full-frame compositing): build it at -O2
FAST_SRCS := launcher_ui.c
# stb_image / stb_truetype (PNG + TrueType decoding for the HMI's own assets)
EXTRA_INCLUDES := -I../../shared/third_party/stb
include ../../shared/app.mk

# Host-side preview of the launcher UI (no head unit needed): renders PPM frames
# with the same launcher_ui.c. See tools/ui_preview.c for how to point it at a
# copy of the unit's HMI resources.
HOST_BIN := $(R)/bin/host
.PHONY: preview
preview:
	@mkdir -p $(HOST_BIN)
	gcc -O2 -w -std=gnu99 -I. -I$(SHARED_INCLUDE) -I$(R)/shared/third_party/stb \
	    tools/ui_preview.c launcher_ui.c -lm -lpthread -o $(HOST_BIN)/ui_preview
	@echo "OK: $(HOST_BIN)/ui_preview"

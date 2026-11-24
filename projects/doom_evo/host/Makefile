# Host-side (x86, 32-bit) build of doom_evo.
#
# Builds the same DOOM engine sources as ../source/Makefile with the
# platform layers that are plain POSIX (i_system_evo.c, i_sound_evo.c,
# i_net_evo.c) and swaps in headless video + scripted input from this
# directory. The result exercises the game loop on the development machine
# so timing, input and game-logic changes can be checked without the head
# unit. Output goes to host/build/, never to ../../../bin/, so the deploy
# script's binary auto-discovery is not affected.
#
#   make            build host/build/doom_host
#   make run        run E1M1 for 350 tics with scripts/walk_forward.txt
#   make gdb        same run under gdb, batch mode, prints a backtrace on crash
#   make clean

SRC   := ../source
BUILD := build
BIN   := $(BUILD)/doom_host
WADDIR := ..

CC ?= gcc
CFLAGS := -m32 -O0 -g -std=gnu99 -fno-strict-aliasing -include alloca.h \
          -DNORMALUNIX -DHAVE_UNISTD_H -DNO_X11 \
          -I. -I$(SRC) \
          -Wall -Wno-unused -Wno-implicit-function-declaration -Wno-int-conversion \
          -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-format \
          -Wno-parentheses -Wno-builtin-declaration-mismatch
LDFLAGS := -m32
LIBS := -lm -lpthread

DOOM_CORE_SRCS := doomdef.c doomstat.c dstrings.c tables.c \
    d_main.c d_net.c d_items.c g_game.c
DOOM_RENDERER_SRCS := r_bsp.c r_data.c r_draw.c r_main.c \
    r_plane.c r_segs.c r_sky.c r_things.c
DOOM_GAME_SRCS := p_ceilng.c p_doors.c p_enemy.c p_floor.c \
    p_inter.c p_lights.c p_map.c p_maputl.c \
    p_plats.c p_pspr.c p_setup.c p_sight.c \
    p_spec.c p_switch.c p_mobj.c p_telept.c \
    p_tick.c p_saveg.c p_user.c
DOOM_INTERFACE_SRCS := m_menu.c m_misc.c m_argv.c m_bbox.c \
    m_fixed.c m_swap.c m_cheat.c m_random.c \
    am_map.c st_lib.c st_stuff.c hu_stuff.c \
    hu_lib.c wi_stuff.c f_finale.c f_wipe.c
DOOM_SYSTEM_SRCS := i_main.c i_net_evo.c s_sound.c v_video.c \
    w_wad.c z_zone.c info.c sounds.c
EVO_PORTABLE_SRCS := i_system_evo.c i_sound_evo.c sndserver_filename.c evo_compat.c
HOST_SRCS := i_video_host.c i_input_host.c

ENGINE_SRCS := $(DOOM_CORE_SRCS) $(DOOM_RENDERER_SRCS) $(DOOM_GAME_SRCS) \
               $(DOOM_INTERFACE_SRCS) $(DOOM_SYSTEM_SRCS) $(EVO_PORTABLE_SRCS)

OBJS := $(addprefix $(BUILD)/,$(ENGINE_SRCS:.c=.o) $(HOST_SRCS:.c=.o))

.PHONY: all run gdb clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@echo "Host build complete: $@"

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

RUN_ARGS ?= -warp 1 1 -hosttics 350 -hostscript $(abspath scripts/walk_forward.txt) -hostdump 70 -hostdumpdir $(abspath $(BUILD)/frames)

run: $(BIN)
	mkdir -p $(BUILD)/home $(BUILD)/frames
	cd $(WADDIR) && HOME=$(abspath $(BUILD)/home) DOOMWADDIR=. $(abspath $(BIN)) $(RUN_ARGS)

gdb: $(BIN)
	mkdir -p $(BUILD)/home $(BUILD)/frames
	cd $(WADDIR) && HOME=$(abspath $(BUILD)/home) DOOMWADDIR=. gdb -q -batch \
	    -ex run -ex bt -ex "info registers" --args $(abspath $(BIN)) $(RUN_ARGS)

clean:
	rm -rf $(BUILD)

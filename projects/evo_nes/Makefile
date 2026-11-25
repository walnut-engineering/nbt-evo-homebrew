# evo_nes - NES emulator tile: the Nofrendo core (./nofrendo, LGPL) + our QNX/launcher
# port (evo_nes_osd.c) + the launcher-side frontend (evo_nes.c). See ../../shared/app.mk.
APP  := evo_nes
NOF  := nofrendo
SRCS := evo_nes.c evo_nes_osd.c \
        $(wildcard $(NOF)/*.c) $(wildcard $(NOF)/cpu/*.c) $(wildcard $(NOF)/nes/*.c) \
        $(wildcard $(NOF)/sndhrdw/*.c) $(wildcard $(NOF)/mappers/*.c) $(wildcard $(NOF)/libsnss/*.c)
# an emulator wants the whole thing optimised, not just a hot file
OPT  := -O2
# nofrendo includes its own headers as <noftypes.h>, <nes.h>, ... from these dirs
EXTRA_INCLUDES := -I$(NOF) -I$(NOF)/cpu -I$(NOF)/nes -I$(NOF)/sndhrdw -I$(NOF)/mappers -I$(NOF)/libsnss
# -fsigned-char: ARM char is unsigned (the DOOM lesson); quiet the core's old-C warnings
EXTRA_CFLAGS   := -fsigned-char -Wno-char-subscripts -Wno-attributes -Wno-unused-function
# QSA audio
EXTRA_LIBS     := -l:libasound.so.2
include ../../shared/app.mk

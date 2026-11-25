# evo_retro — the frontend behind the SNES and Genesis tiles

A small **libretro frontend** for the head unit: the part of an emulator that is
not emulation — the picture on the 1440×540 panel, sound out of the car's speakers,
gamepad / knob / keyboard input, the ROM list, the in-game menu and its settings,
battery saves and save states. *libretro* is the interface most emulator cores
share (load a game, run one frame, hand over a picture and a block of sound, ask
for buttons), so one frontend serves every core that speaks it.

This folder is the shared code only; it makes no tile by itself. Each system is its
own project folder that links these sources with one emulator core into one binary:

| Tile | Folder | Core | Binary |
|---|---|---|---|
| **SNES** | [`../evo_snes`](../evo_snes/README.md) | Snes9x 2010 | `evo_snes` |
| **Genesis** (also Master System, Game Gear, Sega CD) | [`../evo_genesis`](../evo_genesis/README.md) | Genesis Plus GX | `evo_genesis` |

Controls, settings, where the games go and the limits of each system are in the
tile's own README. This file covers what they share and how it is built.

## Build

```sh
make -C projects/evo_snes         # -> bin/evo_snes
make -C projects/evo_genesis      # -> bin/evo_genesis
./projects/evo_retro/build.sh     # both
```

A tile's Makefile is three lines (`APP`, `SYSTEM`, `include ../evo_retro/retro.mk`).
[`retro.mk`](retro.mk) compiles the frontend with `-DEVO_RETRO_<SYSTEM>` and links
it with the tile's core library, `build/evo_retro/libretro_<name>.a`. That library
comes from the tile's own `core.sh`, which fetches the core at a pinned commit and
compiles it with the unit's toolchain; `make` runs it when the library is missing.
The fetch, compile and archive steps the two `core.sh` scripts share are in
[`core_common.sh`](core_common.sh). No emulator core is in this repository (see
[Licences](#licences)).

## Run and test

A tile's binary takes an optional ROM path (`./evo_snes /fs/sda1/evo/snes/game.sfc`
skips the list). For testing:

| Environment | Does |
|---|---|
| `EVO_RETRO_STATS=1` | frame rate, per-frame cost and sound-buffer figures under the picture |
| `EVO_RETRO_FRAMES=n` | leave after n frames |
| `EVO_RETRO_TURBO=1` | run unpaced and silent, to measure the whole pipeline |
| `EVO_RETRO_AUTOSTART=n` | tap Start every n frames, to get a timing run past title screens |
| `EVO_RETRO_SNAP=1` | screenshots on request: `echo name > /dev/shmem/retro_snap_req` writes the next frame shown to `/dev/shmem/name.ppm` (1440×540; costs a 0.2 s stall) |

With [`evo_padfake`](../evo_padd/README.md) every screen can be driven from a
script without a controller in hand.

## How it is put together

| File | Does |
|---|---|
| [`evo_retro.c`](evo_retro.c) | the libretro side (callbacks, loading, the frame loop and its pacing), battery saves and save states |
| [`retro_video.c`](retro_video.c) | scales the core's picture onto the 1440×540 panel |
| [`retro_audio.c`](retro_audio.c) | resampling, the output thread, keeping sound and picture in step |
| [`retro_input.c`](retro_input.c) | gamepads, knob and keyboard as one set of buttons |
| [`retro_ui.c`](retro_ui.c) | the ROM list, the in-game menu and its settings page |
| [`retro_opts.c`](retro_opts.c) | the settings: their values, `settings.txt`, what the core is told |
| [`retro_zip.c`](retro_zip.c) | one game out of a `.zip` (its own small inflate; no library needed on the unit) |
| [`retro_fe.h`](retro_fe.h) | shared declarations, and each system's names, folder and file extensions |
| [`libretro.h`](libretro.h) | the interface header (MIT, from libretro-common) |
| [`retro.mk`](retro.mk), [`core_common.sh`](core_common.sh) | the build rules and core-building steps the tiles share |
| [`spike/`](spike) | the headless benchmark used to choose the cores |

**Picture.** Scaled by the frontend in one pass per frame, to the fit chosen in
Settings: Sharp 2x, Full height, TV 4:3 or Wide 16:9. Sharp 2x repeats pixels
exactly (the Genesis' narrower 256-pixel mode is stretched to the same 640, as on a
TV); the three full-height fits are not whole-number scales, so they blend the
one-pixel seams between source pixels ("sharp bilinear"): slightly softer, with even
scrolling. Frames are written straight into the display's off-screen buffer — only
the picture area, not the whole panel — through two calls added to
`evo_graphics_lib` (`evo_display_direct_begin` / `_post`). Writing into that buffer
is what costs: about 2 ms per megabyte on this unit, so the cost of a fit is mostly
its size. (A second thread on the other CPU core was tried for the big fits and
gave 15 %; the memory is the limit, so there is none.)

**Sound.** The console's clock and the sound device's clock are different crystals.
The picture is paced by a timer at the console's own frame rate; the sound is
resampled to the device's 44,100 Hz and its ratio nudged by at most half a percent
to keep a small buffer (about 30 ms) from running dry or filling up. A separate
thread feeds the device, so a slow frame cannot stall it.

**Saves.** The cartridge's battery memory is written about a second after the game
changes it (a car can lose power at any moment) and again on pause and quit, through
a temporary file and a rename. Save states are four slots per game.

**Settings.** Kept as plain `key = value` lines in `settings.txt` in the system's
folder. Lines the menu does not know are handed to the core as its own options and
kept when the menu rewrites the file.

**Timing on the bench unit** (2 × Cortex-A15, 1.5 GHz; one 60 Hz frame is 16.7 ms).
Emulation plus picture, per frame:

| | Sharp 2x | Full height | TV 4:3 | Wide 16:9 |
|---|---|---|---|---|
| Genesis, 240p Test Suite | 4.1 ms | 7.3 ms | 6.4 ms | 6.7 ms |
| SNES, 240p Test Suite | 4.3 ms | 7.2 ms | 7.4 ms | 8.6 ms |
| SNES, six commercial games | 4.3 – 5.7 ms | | 7.3 – 8.8 ms | |

Of that, the picture is 1.4 – 1.8 ms at Sharp 2x and 4.1 – 5.8 ms at the
full-height fits; the rest is the emulation. The worst single frame in the
commercial-game runs was 17 ms at Sharp 2x and 19 ms at TV 4:3, with at most two
frames in 1,200 over a frame's time (the first frames after loading among them),
and the sound never ran dry in any run without the screenshot aid.

## Adding a system

Another console is another core, not another port:

1. Copy a tile folder (say `evo_genesis`) to `projects/evo_<system>`; set `APP` and
   `SYSTEM` in its Makefile, the name / script / icon in `app.desc`, and the names in
   `target/<system>_launch.sh`.
2. In its `core.sh`, give the core's repository, a pinned commit and its source list.
3. In [`retro_fe.h`](retro_fe.h), add the system's block (title, ROM folder, file
   extensions); in [`retro_opts.c`](retro_opts.c), the core options worth a line in
   Settings. A system's quirks (the Genesis' Sega CD handling, for example) sit
   behind `#if defined(EVO_RETRO_<SYSTEM>)` in the frontend.

The reasoning behind the design, and what was measured before it was built, is in
[`PLAN.md`](PLAN.md).

## Limits

- Tested from scripts and screenshots, with measured sound statistics. How it feels
  to play and how it sounds have not yet been judged by a person.
- The settings page is reached from the in-game menu only, not from the ROM list.
- A `.zip` is read whole into memory; that suits cartridges, not CD images.
- The in-game menu's panel is as wide as the Sharp 2x picture, so in that fit the
  dimmed game behind it is hidden.
- What each system cannot do yet (SNES special chips, Sega CD) is in the tiles'
  READMEs.

## Licences

The frontend in this directory is this project's own code (MIT). `libretro.h` is MIT.

The cores are **not** free software in the usual sense: **Snes9x** and **Genesis
Plus GX** may be used and redistributed free of charge but **not commercially**,
and modified versions of Genesis Plus GX must come with their source. That is why
they are not in this repository — each tile's `core.sh` fetches its core from the
upstream repository at a pinned commit — and why a binary of `evo_snes` or
`evo_genesis` may only be passed on non-commercially, with that notice and the
cores' own licence files. Commercial ROMs and BIOS images are never part of this
project. The frontend itself contains no GPL code.

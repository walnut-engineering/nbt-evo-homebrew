# evo_retro — SNES and Sega Genesis

Two launcher tiles, **SNES** and **Genesis**, that play your own ROM files on the
head unit with an Xbox controller (through [evo_padd](../evo_padd/README.md)), the
iDrive knob or a USB keyboard, with sound out of the car's speakers, battery saves,
save states and a choice of how the picture fills the wide panel.

They are one program built twice: a small frontend of our own (this directory)
linked with an existing emulator core — Snes9x 2010 for the SNES, Genesis Plus GX
for the Genesis (which also plays Master System and Game Gear ROMs). The frontend
speaks *libretro*, the interface most emulator cores share, so another system is
another core, not another port.

No games, no BIOS files and no emulator cores are in this repository: ROMs are
yours to supply, and the cores are fetched when you build (see [Licences](#licences)).

## Controls

| | SNES | Genesis |
|---|---|---|
| D-pad or left stick | D-pad, with diagonals | D-pad, with diagonals |
| A / B / X / Y | B / A / Y / X — the same positions as on an SNES pad | B / C / A / Y |
| LB / RB | L / R | X / Z |
| Menu / View | Start / Select | Start / Mode |
| **Xbox button** | the in-game menu | the in-game menu |
| second pad | player 2 | player 2 |

The in-game menu pauses the game: **Resume**, **Save state**, **Load state** (four
slots per game, left / right or LB / RB to choose), **Settings**, **Reset the
console**, **Change game** (back to the list) and **Quit** (back to the launcher).
In the menu and the ROM list, A chooses and B goes back; LB / RB page through a
long list.

Without a gamepad the basics still work:

| | iDrive | USB keyboard |
|---|---|---|
| D-pad (one direction at a time on the knob) | tilt | arrows |
| bottom / right / left / top button | press / OPTION / MEDIA / – | Z / X / A / S |
| L / R | – | Q / W |
| Start / Select | NAV / TEL | Enter / Tab |
| menu | BACK | Esc |
| move in a list or menu | rotate or tilt | arrows |

MENU on the iDrive controller is left alone, as in the other tiles.

## Settings

**Settings** in the in-game menu; left / right change a value and it applies at
once (the page sits at the left edge so the picture stays in view while you change
its fit).

| Setting | Values (the first is the default) |
|---|---|
| Picture | **Sharp 2x** — whole-number scale, every pixel the same size (SNES 512×448, Genesis 640×448) · **Full height** — all 540 rows, square pixels (617 / 771 wide) · **TV 4:3** — all 540 rows, 720 wide, the shape of the TV these consoles were made for · **Wide 16:9** — 960 wide, stretched |
| Sound level | 300 %, in steps from 50 % to 600 % |
| Controller (Genesis) | Auto (what each game was made for) · 3 buttons · 6 buttons |
| Region | Auto · USA / Europe / Japan (Genesis) or NTSC 60 Hz / PAL 50 Hz (SNES). Takes effect after **Reset the console** |
| Sprite limit | On (as the console) · Off (no flicker when many sprites share a line) |
| Less slowdown (SNES) | Off · Light · Medium · Most — a faster console processor; can upset some games |
| Super FX speed (SNES) | Normal · 1.5x · 2x · 4x — for Super FX cartridges |

They are kept in `settings.txt` in the system's folder (`/fs/sda1/evo/snes/` or
`.../genesis/`), plain `key = value` lines. A line the menu does not know is handed
to the emulator core as one of its own options and kept when the menu rewrites the
file, so any other core option can be set by hand — for example
`genesis_plus_gx_audio_filter = low-pass`. The sound level lives in
`/fs/sda1/evo/snes_gain` (or `genesis_gain`), the file the launch script reads.

The three full-height fits are not whole-number scales, so they blend the one-pixel
seams between source pixels ("sharp bilinear"): slightly softer than Sharp 2x, with
even scrolling.

## Games, saves, sound

- Put ROM files in **`/fs/sda1/evo/snes/`** (`.sfc .smc .swc .fig`) and
  **`/fs/sda1/evo/genesis/`** (`.md .smd .gen .bin`, plus `.sms .gg .sg` for the
  8-bit Sega systems). Plain files or zipped, one game per `.zip` (the first game
  found in the archive is the one played).
- **Sega CD** (in the Genesis tile): a `.cue` file with its track files beside it,
  or an `.iso`. It needs the console's BIOS, which you supply: `bios_CD_U.bin`,
  `bios_CD_E.bin` or `bios_CD_J.bin` in `/fs/sda1/evo/genesis/system/`. Track files
  named by a `.cue` are left off the list, so a disc shows once. See
  [Limits](#limits): this path is wired up but has not been run with a BIOS.
- Saves live in a `saves` folder beside the games. The cartridge's battery memory
  is `<game>.srm`, written about a second after the game changes it (a car can lose
  power at any moment) and again when you pause or quit. Save states are
  `<game>.st1` … `.st4`.
- Sound level: in Settings. The default is 300 % because the unit attenuates this
  sound source; a soft limiter rounds off the loudest passages instead of clipping
  them.

## Build

```sh
cd projects/evo_retro
./build.sh                 # fetches + builds the cores once, then both binaries
python3 make_icons.py      # snes.rgba / genesis.rgba (original controller glyphs)
```

`build.sh` is `./cores.sh` (the cores at pinned commits, compiled with the unit's
toolchain into `build/evo_retro/libretro_*.a`, about a minute) followed by
`make SYSTEM=genesis` and `make SYSTEM=snes` → `bin/evo_genesis`, `bin/evo_snes`.
After a change to the frontend only, the two `make` lines are enough.

`./scripts/deploy.sh evo_retro` pushes both binaries, their launch scripts and
icons, and regenerates the launcher manifest. The tiles are described by
[`app.snes.desc`](app.snes.desc) and [`app.genesis.desc`](app.genesis.desc) (one
project, two tiles).

## Run

The launcher tiles run [`target/snes_launch.sh`](target/snes_launch.sh) and
[`target/genesis_launch.sh`](target/genesis_launch.sh), which set the audio library
path and restore the BMW screen if the emulator crashes. Logs:
`/dev/shmem/snes_launch_stdout.log`, `/dev/shmem/genesis_launch_stdout.log`.

By hand, `./evo_snes /fs/sda1/evo/snes/game.sfc` skips the list. For testing:

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
| [`cores.sh`](cores.sh), [`patches/`](patches) | fetch, patch and build the cores |
| [`libretro.h`](libretro.h) | the interface header (MIT, from libretro-common) |
| [`spike/`](spike) | the headless benchmark used to choose the cores |

**Picture.** Scaled by the frontend in one pass per frame, to the fit chosen in
Settings. Sharp 2x repeats pixels exactly (the Genesis' narrower 256-pixel mode is
stretched to the same 640, as on a TV); the full-height fits blend only the
one-pixel seams. Frames are written straight into the display's off-screen buffer —
only the picture area, not the whole panel — through two calls added to
`evo_graphics_lib` (`evo_display_direct_begin` / `_post`). Writing into that buffer
is what costs: about 2 ms per megabyte on this unit, so the cost of a fit is mostly
its size. (A second thread on the other CPU core was tried for the big fits and
gave 15 %; the memory is the limit, so there is none.)

**Sound.** The console's clock and the sound device's clock are different crystals.
The picture is paced by a timer at the console's own frame rate; the sound is
resampled to the device's 44,100 Hz and its ratio nudged by at most half a percent
to keep a small buffer (about 30 ms) from running dry or filling up. A separate
thread feeds the device, so a slow frame cannot stall it.

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

## Limits

- Tested from scripts and screenshots, with measured sound statistics. How it
  feels to play and how it sounds have not yet been judged by a person.
- **Sega CD has not been run.** The list, the hidden track files and the "needs a
  BIOS" message are checked; loading an actual disc needs a BIOS file, which this
  project cannot supply. Known gaps before trying: `.chd` images and compressed
  (MP3 / OGG) audio tracks are not built into the core, and a disc's backup memory
  is written by the core only when the game is left, not a second after it changes.
- **SNES cartridges with extra processors** are unproven with real games. The Super
  FX's arithmetic passes its test ROMs on the unit, but two synthetic Super FX
  drawing demos ran at 70 % speed and drew wrongly, which may be those demos or may
  be the emulation under full load. SA-1 has not been tried. If a game is too slow:
  Settings > Super FX speed, or Snes9x 2005 (`./cores.sh snes2005`) as the lighter
  core.
- The settings page is reached from the in-game menu only, not from the ROM list.
- A `.zip` is read whole into memory; that suits cartridges, not CD images.
- The in-game menu's panel is as wide as the Sharp 2x picture, so in that fit the
  dimmed game behind it is hidden.

## Licences

The frontend in this directory is this project's own code. `libretro.h` is MIT.

The cores are **not** free software in the usual sense: **Snes9x** and **Genesis
Plus GX** may be used and redistributed free of charge but **not commercially**,
and modified versions must come with their source. That is why they are not in
this repository — `cores.sh` fetches them from their upstream repositories at
pinned commits and applies [`patches/`](patches) — and why a binary of `evo_snes`
or `evo_genesis` may only be passed on non-commercially, with that notice and the
cores' own licence files. Commercial ROMs and BIOS images are never part of this
project.

The two patches: Snes9x 2010's sound code reads 16-bit values at odd addresses,
which this CPU refuses (two macros made byte-wise), and it uses a GCC atomic helper
that does not exist for this toolchain ([`patches/sync_shim.c`](patches/sync_shim.c)).
Genesis Plus GX builds unmodified.

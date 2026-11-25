# evo_retro — SNES and Sega Genesis on the head unit (plan and status)

*Layout since 2026-11-25:* this folder is the shared frontend; each tile has its own
project folder, [`../evo_snes`](../evo_snes/README.md) and
[`../evo_genesis`](../evo_genesis/README.md), with its descriptor, icon, launch
script and the `core.sh` that fetches and builds its core.

Goal: two new launcher tiles, **SNES** and **Genesis**, that play your own ROMs with an
Xbox controller (via [evo_padd](../evo_padd/README.md)), sound through the car's
speakers, saves that survive a power cycle.

**Status, 2026-11-24:** milestones 0–5 are built and checked on the bench unit from
scripts (simulated pad, screenshots, timing and sound statistics); both tiles are in
the launcher. Not yet done: a person playing it (feel, sound by ear), a save that
has to survive the 12 V supply being cut (a reset of the unit is checked), a Sega CD
game (needs a BIOS file) and a real special-chip SNES game. How to use and build
each tile is in its own README; this file keeps the reasoning.

## The approach: one frontend, plug-in cores

Do not port two emulators the way the NES tile was done. Write **one small frontend**
that speaks the *libretro* interface, and link it against existing emulator cores.
libretro is a ten-function C interface (load a game, run one frame, hand over a
picture, a block of sound, ask for buttons) that hundreds of emulator cores already
implement, so every later system (Master System, Game Gear, Game Boy, PC Engine...) is
"build another core", not another port. The frontend is ours and is the only part
that knows about the head unit:

```
evo_retro (ours)                                core (theirs, unchanged or lightly patched)
  video   -> evo_graphics_lib, integer scale      retro_run()  one frame
  audio   -> QSA, resampled to the device rate    video / audio / input callbacks
  input   <- /dev/shmem/evo_pad, knob, keyboard   retro_serialize()  save states
  ROM picker, saves, in-game menu, quit           retro_get_memory()  battery saves
```

One binary per system (frontend + one core, linked statically): no plug-in loading,
no symbol clashes, 3–4 MB each.

## What was measured (2026-11-23, on the bench unit)

(These are the feasibility numbers from before the frontend existed - core only, no
picture or sound. What the finished tiles cost is in the README.)

A headless test frontend ([spike/retro_bench.c](spike/retro_bench.c)) was built against
three candidate cores with the unit's own toolchain (QNX 6.5 SDP, gcc 4.4.2) and timed
on the unit (2 × Cortex-A15, 1.5 GHz). 1,200 frames, two runs each; one 60 Hz frame
has 16.7 ms.

| System | Core | Builds with gcc 4.4.2 | Time per frame | Of one CPU core |
|---|---|---|---|---|
| SNES | **Snes9x 2010** (Snes9x 1.52) | yes, with one 2-line patch + a 10-line helper | 3.0 ms | 18 % |
| SNES | Snes9x 2005 (Snes9x 1.43) | yes, unmodified | 1.1 ms | 7 % |
| Genesis | **Genesis Plus GX** 1.7.4 | yes, unmodified | 2.15 ms | 13 % |

All three rendered correct frames on the unit (checked by pulling a frame back to the
host). The tiles' `core.sh` scripts reproduce the builds from pinned upstream
commits and `spike/build_bench.sh` links the benchmark against them.

**What this does and does not show.** The test ROM was the free *240p Test Suite*
sitting on its menu: CPU and sound emulation run at full rate, but the picture is
simple. Busy games cost more, and cartridges with extra chips (Super FX, SA-1) more
again. With 5× to 15× headroom the ordinary library is not in doubt; the special-chip
games are unmeasured until a real ROM is tried.

**Porting findings**

- Snes9x 2010's sound code reads 16-bit values at odd addresses, which this CPU
  refuses (the same alignment fault the NES and Quake ports hit). Fix: two macros made
  byte-wise ([snes9x2010-unaligned-apu.patch](../evo_snes/patches/snes9x2010-unaligned-apu.patch)).
- It also uses a GCC atomic builtin that this toolchain turns into a call to a helper
  that does not exist on QNX/ARM. Fix: [sync_shim.c](../evo_snes/patches/sync_shim.c).
- Genesis Plus GX already has a switch for strict-alignment CPUs (`-DALIGN_LONG`) and
  needed nothing else.
- Neither core needs C++ or anything newer than C99, which matters: gcc 4.4.2 rules
  out the emulators written in modern C++ (bsnes, current Snes9x).

## Choices

- **SNES: Snes9x 2010.** Three times the cost of the 2005 core and still five times
  faster than needed; in exchange it has the accurate sound core and much better game
  compatibility. Snes9x 2005 stays as the fallback if a special-chip game turns out
  too slow.
- **Genesis: Genesis Plus GX.** The most accurate of the C cores, and the same binary
  also plays Master System, Game Gear, SG-1000 and Sega CD (`.cue` + audio; needs a
  BIOS file you supply). PicoDrive would be faster and adds 32X, but there is no need
  for more speed and it brings ARM assembly and git submodules.
- **Licences.** Snes9x and Genesis Plus GX are free to use and redistribute but **not
  commercially**, and modified versions must come with complete source. So the cores
  do not go into this repository: the build fetches them at pinned commits and applies
  our patches (the model Quake 3 already uses). The frontend is our own code. A binary
  release that includes a core is then non-commercial too, which fits this project,
  and must say so. No ROMs or BIOS files are shipped, ever.

## Controls (Xbox pad)

| | SNES | Genesis (6-button) |
|---|---|---|
| D-pad / left stick | D-pad | D-pad |
| A / B / X / Y | B / A / Y / X (same positions as the SNES pad) | B / C / A / Y |
| LB / RB | L / R | X / Z |
| Menu / View | Start / Select | Start / Mode |
| Xbox button | in-game menu: resume, save state, load state, reset, quit | same |
| second pad | player 2 | player 2 |

The knob and a USB keyboard keep working as on the NES tile, for the basics.

## Milestones

Each ends with something checked on the unit; the `evo_padfake` tool from evo_padd
lets the pad paths be tested from a script.

0. **Feasibility — done (2026-11-23).** Cores build with the unit's toolchain and run
   several times faster than real time.
1. **Frontend, picture and input — done (2026-11-24).** ROM list, whole-number scaled
   picture, pad / knob / keyboard input, in-game menu. *Checked:* list → game → menu →
   quit driven by the simulated pad and by knob events, with screenshots. Showing a
   frame through the graphics library cost 7 ms (it copies the whole panel), so the
   library gained a direct path into the off-screen buffer: 2 ms.
2. **Sound — done.** Resampling to the device's 44,100 Hz, a separate output thread,
   rate control of at most ±0.5 %. *Checked:* no gaps and no device restarts in any
   run (40 s on the test ROM, 30 s on each of six SNES games); buffer steady at about
   30 ms. *Not checked:* how it sounds — nobody was listening.
3. **SNES — done.** Second binary, both tiles and icons in the launcher, launch
   scripts, ROM folders. *Checked:* launcher → SNES tile → list → game → Quit → back
   on the launcher page.
4. **Saves and the in-game menu — done, bar a hard power cut.** Battery saves written
   a second after a game changes them and on pause / quit; four save-state slots per
   game. *Checked:* save state → move on → load state restores the earlier screen, on
   both cores; battery saves written by three games and read back on the next start.
   A save state made before a reset of the unit was byte-identical afterwards and
   loaded (2026-11-24). *Not checked:* the 12 V supply cut right after a save, which
   is the case the one-second write is there for.
5. **Polish — done (2026-11-24); release — done (2026-11-25).**
   - *Picture fits:* Sharp 2x, Full height, TV 4:3, Wide 16:9, chosen in a Settings
     page of the in-game menu and shown live behind it. *Checked:* every fit against
     pixel tests on the host (exact repetition at 2x, nothing written outside the
     picture), and on the unit by screenshot and timing.
   - *Settings:* sound level, Genesis pad type, region, sprite limit, SNES slowdown
     and Super FX speed, kept in `settings.txt`; unknown lines go to the core as its
     own options. *Checked:* each value stepped on the unit; a region change switches
     the Genesis to 50 Hz at once and reloads the SNES game on Reset (the test ROM
     then reports PAL).
   - *Zip:* one game per archive, read with a small inflate of our own. *Checked:*
     24 round trips against zlib and 2,400 damaged archives on the host without a
     crash; both systems start a zipped game on the unit.
   - *Change game:* back to the list from the menu. *Checked:* seven loads in one
     run, memory use unchanged.
   - *Sega CD:* `.cue` / `.iso` in the list, track files hidden, a message when the
     BIOS is missing. *Not checked:* an actual disc — there is no BIOS here.
   - *Also:* a file too small to be a game is refused (Genesis Plus GX "runs" a
     2-byte file as a black screen), and a failed load says why.
   - *Release:* the source is in the public repository and both tiles are in the
     binary release of 2026-11-25, which is marked non-commercial and carries the
     cores' licence files and source.

## What milestone 5 found

- **The picture's cost is its size.** Writing into the scan-out buffer takes about
  2 ms per megabyte on this unit: 1.4–1.8 ms for the 2x fits, 4.1–5.8 ms for the
  full-height ones, more than the emulation itself. A helper thread on the second
  CPU core brought only 15 % (the memory is the limit), so it was taken out again.
  The real fix would be to give the compositor a small buffer and let it stretch;
  that needs the unit's own screenshot tool to verify, which was not usable (below).
- **The unit's panel-dump tool leaks.** Each `LayerManagerControl dump` keeps its
  3 MB of flash until the next boot and the tool stops working after about 35. A
  reset of the unit (`scripts/reboot.sh`) gave 75 MB back and the tool worked again;
  the first dump of the new boot held 3 MB as before, so it is the tool, not a
  one-off. The frontend therefore has its own screenshot aid (`EVO_RETRO_SNAP`), which shows
  what we drew but not what the compositor made of it.
- **Super FX is not settled.** Four Super FX arithmetic test ROMs pass on the unit.
  Two synthetic drawing demos ran at 70 % speed, three pictures per emulated-frame
  call, and drew wrongly; whether that is the demos (they also carry a header this
  core rejects until it is patched) or the emulation under full load is not known.
  None of the games tried so far uses an extra chip.
- **Snes9x 2010 rejects a cartridge header that claims less than 128 KB**, which
  some homebrew test ROMs do.

## Later, once the frontend exists

- **NES through the same frontend** (FCEUmm or QuickNES core): fixes the sound
  accuracy the current Nofrendo tile lacks, and leaves one frontend to maintain.
- More systems for the price of a build: Master System / Game Gear come free with
  Genesis Plus GX; Game Boy / Color, PC Engine and others have C cores.

## Risks

- **Sound is the hard part of any frontend** (milestone 2): two clocks, the emulated
  console's and the sound card's, have to be kept together. (Built; holds in every
  run so far.)
- **Special-chip SNES games** are unmeasured.
- **More alignment faults** may hide in rarely used paths (special chips, save states);
  each is a small patch, found by playing.
- **gcc 4.4.2** is old; an optimiser bug in a 60,000-line core is possible. Mitigation:
  build the suspect file at a lower optimisation level.
- **Flash space:** `/fs/sda1` has about 265 MB free; binaries are small, ROM folders
  are yours to size.

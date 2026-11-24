# DOOM EVO - Display Layer Fix and Launch/Quit Cycle

**Date**: 2026-11-01
**Status**: Working on the head unit from a clean state; verified by camera, on-target screenshot and `LayerManagerControl`

## What was wrong

`evo_display_unified.c` created the Screen window with `SCREEN_PROPERTY_VISIBLE=0`
and relied on the LayerManager to switch it on, but never created its ILM layer
(`ilm_layerCreateWithDimension` was resolved via dlsym and never called). Every
ILM call that followed was accepted and silently applied to a non-existent
layer 9000. Whether the window reached the panel then depended on leftover
LayerManager state from an earlier run: the first run after boot showed a
blank panel even though the on-target `screenshot` tool contained DOOM.

Two more problems hid behind it:

- The test runner started the binary behind a timestamp pipeline, so its
  SIGTERM hit the wrapper, not the game, and the game died from a broken pipe
  without cleanup. The wrapper also spawned `date` for every output line.
- `I_ShutdownDirectInput` joined the input thread, which sits reply-blocked
  inside `devu-kbd` (O_NONBLOCK is ignored on `/dev/usbkbd0`), so a quit hung
  until a key was pressed. `slay -f` sends SIGTERM, not SIGKILL, which hid this.

## Layer structure on the unit (screen 0, 1440x540)

74 layers in the render order, bottom first. Only one is visible at boot:
`2147483648` (0x80000000), the car HMI base layer, carrying surface 256. The
others are dormant (early video app, popups, etc.). `LayerManagerControl`
parses visibility with `atoi`, so `visibility true` means 0; use `1`/`0`.

## What the library does now (`evo_display_show`)

1. Posts the black window once so the LayerManager registers the surface
   named by the window ID string, and waits (up to 3 s) until
   `ilm_getSurfaceIDs` lists it. On the unit this takes about 100 ms.
2. `ilm_layerCreateWithDimension(9000, w, h)`, commit, layer source and
   destination rectangles.
3. Surface and layer visibility, `ilm_layerAddSurface`, layer render order
   = [surface], screen render order = layer 9000 FIRST followed by the
   existing order, commit. On this unit the first entry is drawn on top:
   appended last, the game was covered by the HMI base layer wherever they
   overlapped (picture-in-picture showed nothing, docked worked only because
   the HMI had been moved aside).
4. Verifies with `ilm_getSurfaceIDsOnLayer` that the surface is on the layer,
   re-adding and re-committing if not.

`evo_display_hide` (called from destroy) restores any foreign layer rectangle
changed for a docked layout, disables presentation mode (restoring only the
layers that were visible before), removes the surface from the layer, restores
the original render order without our id, removes layer 9000, and commits.
Presentation mode queries visibility through an aligned 4-byte flag: the
target's ILM client writes a 4-byte boolean and faulted with SIGBUS on a
1-byte one.

## DOOM side

- `-showfps` draws a frame-rate counter into the game screen.
- `-pip [x y w h]` floats the game picture (source rect 288,0 864x540) over
  the HMI at the given screen rectangle (default bottom-right 432x270).
- `-dock` scales the HMI base layer to 960x360 at (0,90) and puts the game
  picture beside it at (960,120) 480x300; both stay usable. `-hmilayer <id>`
  overrides the HMI layer id (default 2147483648).
- Full screen (default) hides the HMI via presentation mode until exit.
- SIGTERM/SIGINT set a flag; `I_FinishUpdate` calls `I_Quit()` so every exit
  path restores the HMI. Letterbox borders are opaque black.
- Demos with version byte 109 (the 1.9 demos in doom1.wad) are accepted, so
  the attract demos and `-timedemo demo1` run on the unit.

## Launcher

`target/doom_launch.sh` (copy next to the binary in `/var/run/screen_tests`):
starts `devu-kbd`/`devu-mouse` if needed, runs the game, then as a safety net
destroys surface 2000 and layer 9000 and sets the HMI layer visible again.

## Frame rate

`-timedemo demo1` on the unit: 1710 gametics in 1306 realtics, about 46 fps
of raw rendering, yet normal play sat at 29 fps. Cause: with one render
buffer `screen_post_window()` waited for the next display refresh, so a
frame with about 22 ms of work could only complete every second vsync.
`evo_display_create` now sets `SCREEN_PROPERTY_SWAP_INTERVAL = 0`; in-level
frame rate is 34.4 to 35.0 fps (the tic rate). `I_GetTime` uses
`ClockCycles()` on QNX, since `gettimeofday()` only advances per kernel tick,
and the tic wait loop yields instead of sleeping. Tearing is possible with
swap interval 0; a second render buffer would be the clean fix.

## Verified cycle (camera + LayerManagerControl)

launch -> surface registered, layer 9000 created, surface confirmed on it,
HMI layer hidden, game on panel -> `slay doom_evo` -> termination handler,
presentation mode disabled (HMI restored), surface removed, order restored,
layer removed, exit status 0 -> HMI on panel, layer 9000 "resource was not
found".

## Layouts verified on the panel (on-unit screenshot + camera)

- `-dock`: HMI scaled into the left two thirds, game picture on the right,
  both live; after quit the HMI layer is back at 0,0 1440x540 and visible.
- `-pip`: game picture floating bottom-right over the HMI (after the render
  order fix above).
- full screen: HMI hidden, game full width, HMI restored on quit.


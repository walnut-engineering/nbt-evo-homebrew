# DOOM EVO - Game Timing and Input Fix

**Date**: 2026-11-01
**Status**: Fixed, verified on the host harness and on the head unit (2026-11-01, see below)

## Symptom

Player input (WASD, arrows) was unreliable or ignored in-game even though the
input thread delivered key events, and the game loop speed did not track the
35 tics/s clock. The last commit chased this with debug prints in
`G_Responder`, `G_BuildTiccmd` and `P_MovePlayer` and an "early gamekeydown"
workaround in `G_Responder`.

## Root cause

Two timing overrides in `source/d_net.c`, added while debugging a stall:

1. **NetUpdate**: in single player, `nettics[0]` was forced to
   `max(nowtime + 1, gametic + 3)` before the ticcmds for those tics existed.
   Vanilla DOOM advances `nettics[0]` only through the loopback packet
   (`HSendPacket(0)` -> `reboundstore` -> `GetPackets`), which happens after a
   ticcmd is built for each elapsed real-time tic.
2. **TryRunTics**: the "wait for new tics" loop was cut short after 150
   iterations by forcing `lowtic` forward.

Together these let `TryRunTics` run tics that had no ticcmd built yet. The
engine then read stale entries from the `netcmds` ring buffer (12 tics old)
or zeros, so key presses were dropped or applied late, and the game advanced
one tic per rendered frame instead of one tic per 1/35 s.

Measured on the host harness (`host/`, 350 tics of E1M1 with a scripted W
hold): 1.2 s wall clock instead of 10 s, key events delivered at tic 150
instead of tic 35, player never moved.

## Fix

- `source/d_net.c`: removed both overrides, restoring the vanilla single-player
  path. Added a 1 ms `usleep` in the wait loop so the main thread does not
  spin a whole core while waiting for the next tic.
- `source/g_game.c`: removed the "early gamekeydown" workaround in
  `G_Responder` (it also let arrow keys move the player while the automap was
  panning). Per-tic movement debug prints are now behind `-DDOOM_INPUT_TRACE`.
- `source/p_user.c`: per-tic movement debug prints behind `-DDOOM_INPUT_TRACE`.

The per-tic prints mattered on target: the test runner pipes stdout through a
shell loop that runs `date` for every line, so 200+ lines/s of debug output
can back up the pipe and block the game loop in `printf`.

## Verification (host harness)

| Check | Before | After |
|---|---|---|
| Wall clock for 350 tics | 1.2 s | 10.7 s |
| Key events delivered | bunched at tic 150 and 281 | on their scheduled tics |
| W held tics 35-140 | no movement | y -3616 -> -2897 |
| D strafe, RIGHT turn, CTRL fire | nothing | x +70, angle 90 -> 335, ammo 50 -> 49 |
| `-timedemo demo1/2/3` | - | all run to completion, no crash |

ARM build: `make all` in `source/` succeeds (656K, `bin/doom_evo`).

## On-target checklist (next deploy)

1. `cd projects/doom_evo/source && make clean && make all && make deploy_doom_evo`
2. In the stdout log, `NetUpdate: building N ticcmds` should appear once per
   elapsed tic and `TryRunTics` should no longer print timing warnings.
3. **Check for a stale config on the target.** The game reads and writes
   `$HOME/.doomrc`. A `.doomrc` saved by an older build still holds
   `key_up 173` (arrow) and will override the WASD defaults in `m_misc.c`.
   Delete it on the target, or confirm it shows `key_up 119`, `key_down 115`,
   `key_strafeleft 97`, `key_straferight 100`.
4. Then re-enable mouse input in `i_input_evo.c` (currently commented out as
   "TEMPORARILY DISABLED") and continue with HID Phase 2.

## Reproducing on the host

```bash
cd projects/doom_evo/host
make run          # scripted WASD run, prints player telemetry every 35 frames
make gdb          # same under gdb
```
See `host/README.md`.

## On-target validation (2026-11-01)

Deployed with `make deploy_doom_evo`, then run manually with `-warp 1 1` and a
USB keyboard (Chicony 04f2:0833, `/dev/usbkbd0` via `devu-kbd`):

| Check | Result |
|---|---|
| Game speed | 34.0 to 35.6 fps reported by `I_FinishUpdate` over 7200+ frames, i.e. 35 tics/s |
| Stability | no crash in a 60 s automated run or a 4+ minute interactive run |
| Backend | PowerVR-2D, hardware blit enabled |
| Keyboard | 346 key events queued and delivered, player controllable (confirmed by eye on the panel) |
| Panel | E1M1 rendered letterboxed at 1440x540, confirmed by camera and by the on-target `screenshot` tool |

Note: in-level frame rate dipped to about 30 fps for the first minute of the
interactive run before settling at 35; worth profiling later (the ARM build
is `-O0` with ARM7TDMI codegen, and `I_FinishUpdate` copies a 3 MB frame
twice per frame).

## Display issue: ILM layer was never created (fixed later the same day, see DISPLAY_LAYER_FIX.md)

`LayerManagerControl` on the unit shows surface 2000 exists but is on no
layer, its frame counter stays at 0, and **layer 9000 does not exist**.
`evo_display_show()` logs success for `ilm_layerSetVisibility(9000)`,
`ilm_layerAddSurface(9000, 2000)` and the render order, but those calls are
only queued until commit, and `ilm_layerCreateWithDimension` is resolved via
dlsym in `evo_display_unified.c` yet never called. The system log shows the
layer manager reporting `[LMGR-FPS] ... no posts` while DOOM posts at 35 fps,
so the window is composited by Screen directly, not by the layer manager.

Consequence: whether the window reaches the panel depends on leftover layer
manager state. The first automated run after boot showed a blank panel even
though the on-target screenshot contained DOOM; later runs (with a stale
surface 2000 already registered) were visible. Fix to make: create layer
9000 with `ilm_layerCreateWithDimension` before adding the surface, commit,
and verify with `LayerManagerControl get surface 2000` that it is on layer
9000 with increasing frame counters, from a freshly rebooted unit.

The test runner hides HMI layer 2147483648 before each run and never
restores it; `LayerManagerControl set layer 2147483648 visibility true`
brings the car HMI back.

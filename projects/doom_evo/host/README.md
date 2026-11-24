# doom_evo host harness

A 32-bit x86 build of the doom_evo engine that runs on the development
machine with no display, no USB devices and no head unit. It exists to
exercise the game loop, timing and input handling of the shared sources in
`../source/` before a deploy, and to reproduce engine-side crashes under gdb.

It is **not** a replacement for on-target testing: the EVO graphics library
(`i_video_evo_v2.c`), the USB input thread (`i_input_evo.c`) and the QNX
runtime are not part of this build.

## What is built

| Layer | Host build uses | Target build uses |
|---|---|---|
| Engine (`d_*.c`, `g_*.c`, `p_*.c`, `r_*.c`, ...) | `../source/*.c`, unchanged | same files |
| System / sound / network | `i_system_evo.c`, `i_sound_evo.c`, `i_net_evo.c` (plain POSIX) | same files |
| Video | `i_video_host.c` (headless, PPM dump, telemetry) | `i_video_evo_v2.c` |
| Input | `i_input_host.c` (scripted events from a file) | `i_input_evo.c` |

Output is `host/build/doom_host`. Nothing is written to `bin/`, so the
deploy script's binary auto-discovery is unaffected.

## Usage

```bash
cd projects/doom_evo/host
make            # build
make run        # E1M1, 350 tics, scripts/walk_forward.txt, frames every 70 tics
make gdb        # same run under gdb, prints a backtrace if it crashes
make clean
```

`make run` sets `HOME` to `build/home` so the game's `.doomrc` never touches
your real home directory, and `DOOMWADDIR` to `..` so it finds `doom1.wad`.

Override the arguments with `RUN_ARGS`, for example:

```bash
make run RUN_ARGS='-warp 1 2 -hosttics 700 -hostscript $(pwd)/scripts/walk_forward.txt'
```

### Harness options

| Option | Meaning |
|---|---|
| `-hosttics N` | quit cleanly once `gametic` reaches N (exit code 0) |
| `-hostscript FILE` | replay key events from FILE (see below) |
| `-hostdump N` | write `screens[0]` as `frame_NNNNN.ppm` every N frames |
| `-hostdumpdir DIR` | where the PPM files go |

All normal DOOM options (`-warp`, `-skill`, `-nomonsters`, `-playdemo`, ...)
work as usual.

### Input script format

One event per line: `<gametic> <key> <down|up|press>`.
`<key>` is a single character (`w`, `a`, `1`), one of `UP DOWN LEFT RIGHT ESC
ENTER SPACE CTRL SHIFT ALT TAB BACKSPACE F1..F12`, or a decimal DOOM key code.
`press` is a down at `<gametic>` and an up two tics later. Lines starting
with `#` are comments.

### Reading the log

Every 35 frames the harness prints one line like

```
[HOST] frame 105 tic 79 state 0 colours 144 player x=1055 y=-3328 z=-10 angle=90 health=100 ammo=50
```

`state 0` is `GS_LEVEL`, `colours` is the number of distinct palette indices
on screen (a blank screen is 1), and the player fields come straight from
`players[consoleplayer]`. E1M1 starts at x=1056 y=-3616 facing 90 degrees.

Convert a dumped frame to PNG with Pillow:

```bash
python3 -c "from PIL import Image; Image.open('build/frames/frame_00140.ppm').save('frame.png')"
```

## Expected result of `make run`

With the vanilla single-player timing in `d_net.c`, 350 tics take about
10 seconds of wall clock, the scripted W/D/RIGHT/CTRL events are delivered on
their exact tics, the player moves north then east, turns right and fires one
bullet (ammo 50 to 49).

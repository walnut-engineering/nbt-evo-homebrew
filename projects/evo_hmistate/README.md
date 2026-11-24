# evo_hmistate

The launcher's **summon gate** — a daemon that detects whether the OEM HMI is on
the main 6-tile Startmenu, so the launcher only opens from there (not over a
submenu or a notification). The launcher fail-opens if this daemon isn't running.

## How it works
There is no clean HMI-state interface to read, so it captures the OEM
framebuffer with `screen_read_display` (the same path [`evo_screenshot`](../evo_screenshot)
uses) and checks a **signature** of the Startmenu: bright label text in all six
fixed, evenly-spaced tile-label windows over a near-black lower band. Requiring
text in all six positions is highly specific (a submenu or popup doesn't) and
selection-invariant (the labels are there whichever tile is highlighted).

## Build
```sh
make -C projects/evo_hmistate     # -> bin/evo_hmistate (ARM QNX; no graphics lib, -O2)
```

## Run
```sh
evo_hmistate            # one-shot: print STARTMENU|OTHER + per-window counts, exit 0/1
evo_hmistate -daemon    # resident: re-check on each request via /dev/shmem/evo_hmistate
```
Tunable by env, no rebuild: `EVO_HMI_LUMA` (bright threshold, default 150),
`EVO_HMI_MINCOUNT` (min bright px per window, 110), `EVO_HMI_DARKMAX` (max
lower-band mean luma, 60; 0 disables the dark check).

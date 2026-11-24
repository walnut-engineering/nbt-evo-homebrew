# evo_launcher

The resident **homebrew app launcher** — the heart of this repo. It floats a card
of app tiles over the dimmed, still-live OEM HMI and launches the app you pick.

## How it works
- **Summon / input** — reads the iDrive knob from the shared feed ring
  ([`shared/include/evo_feed.h`](../../shared/include/evo_feed.h), produced on the
  unit by the controller tee). Tilt up/down opens or closes the card, rotate or
  tilt left/right moves the selection, press launches, any ring button closes.
  While the card is up it holds the controller *grab* so the OEM HMI ignores the knob.
- **Draw** — [`launcher_ui.c`](launcher_ui.c) composites the tile page and the
  launch splash into our own ILM layer via [`shared/evo_graphics_lib`](../../shared/evo_graphics_lib).
  The look is taken from the unit itself: HMI fonts, main-menu tile artwork and
  accent colour, chosen by the coded ID version and brand (flat fallback).
- **Launch** — a normal process, so it `posix_spawn()`s the chosen app directly;
  the overlay stays resident (splash → transparent while the app runs → idle).

## Tiles
Tiles come from the manifest `/fs/sda1/evo/launcher.manifest`, one per line:
`Name|command|icon` (`icon` optional, placeholder drawn when absent; `#` comments).
The manifest is generated from each project's `app.desc` by
[`scripts/gen_manifest.sh`](../../scripts/gen_manifest.sh). Theme override
`/fs/sda1/evo/launcher_theme` = `auto` (default), `id5` or `flat`.

## Build
```sh
make -C projects/evo_launcher          # -> bin/evo_launcher (ARM QNX)
make -C projects/evo_launcher preview  # host-only UI preview, no unit needed (bin/host/ui_preview)
```
`launcher_ui.c` is built at `-O2` (the full-frame compositing hot path); PNG /
TrueType decoding uses the vendored stb headers in `shared/third_party/stb`.

## Run
Installed as `/fs/sda1/evo/evo_launcher` and started `-daemon` by the boot
supervisor ([`doom_evo/controller/evo_autostart.sh`](../doom_evo/controller/evo_autostart.sh)).
See the top-level [`README.md`](../../README.md) and [`launcher_sdk/`](../../launcher_sdk)
for the whole platform and how to add your own tile.

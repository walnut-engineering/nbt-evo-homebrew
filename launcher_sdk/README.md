# EVO Homebrew Launcher SDK

A small platform for running your own full-screen apps on a BMW **NBT EVO ID5**
head unit (QNX 6.6, OMAP5). A resident launcher draws a tile page over the OEM
HMI; you summon it with the iDrive knob, pick a tile, and your app takes the
screen until you press **BACK**. Adding an app is a drop-in: a folder with a
source file, a 3-line Makefile, and a one-line descriptor.

```
        iDrive up/down on the Startmenu
                   │
                   ▼
   ┌─────────────────────────────────────┐
   │ HOMEBREW                             │   ← launcher page (dims the live HMI)
   │  ┌──────┐ ┌──────┐ ┌──────┐          │
   │  │ DOOM │ │ Term │ │System│   ▓      │   ← rotate to move, press to launch,
   │  ├──────┤ ├──────┤ ├──────┤   ▓      │     up/down to close; scrollbar on the
   │  │ GPS  │ │Quake │ │ ...  │   ░      │     right when apps overflow one page
   │  └──────┘ └──────┘ └──────┘          │
   └─────────────────────────────────────┘
```

## The app contract

An app is any QNX executable that:

1. **renders full screen** (1440×540) through `evo_graphics_lib`, and
2. **exits cleanly on iDrive BACK.**

The launcher spawns your binary, waits, and repaints its own page when the app
exits — so `return 0` from `main()` hands control back to the menu. That's the
whole contract. The standard skeleton (init → context → presentation-mode →
`evo_ai_start()` for BACK → draw loop → destroy) is in
[`template/hello_evo.c`](template/hello_evo.c); every shipped app follows it.

## Prerequisites

- The repo's QNX 6.6 cross toolchain under `../toolchain/` (linaro gcc 4.7.3 +
  the on-unit libs). It is a large binary dependency kept outside git — see the
  top-level `README.md`. Without it you can edit and read, but not build.
- `sshpass` on the build host (for `deploy.sh`).
- A reachable head unit (USB-ethernet by default) and its root password.

## Quick start — add an app in 4 steps

```sh
# 1. start from the template
cp -r launcher_sdk/template projects/myapp
cd projects/myapp && mv hello_evo.c myapp.c

# 2. point the Makefile + descriptor at your app
#    Makefile:  APP := myapp   SRCS := myapp.c
#    app.desc:  name = My App   exec = myapp   icon = myapp.rgba   order = 60
$EDITOR Makefile app.desc myapp.c

# 3. make an icon (edit make_icon.py, then run it) -> myapp.rgba
python3 make_icon.py && mv hello.rgba myapp.rgba

# 4. build + push + show it
cd ../.. && ./scripts/deploy.sh myapp
# summon the launcher (iDrive up/down on the Startmenu) - your tile is there.
```

Removing an app: delete its `app.desc` (or set `enabled = 0`) and redeploy.
Nothing else references it — the manifest is regenerated from the descriptors.

## `app.desc` reference

One `key = value` per line; `#` starts a comment.

| key       | required | meaning |
|-----------|----------|---------|
| `name`    | yes      | tile label shown on the launcher page |
| `exec`    | yes      | on-unit command the launcher runs. A bare name resolves to `$EVO_DIR/<name>`; an absolute path is used as-is; a `*.sh` wrapper is fine. |
| `icon`    | no       | `.rgba` icon file (searched in the project dir, then `projects/evo_launcher/icons/`). Omit for a plain tile. |
| `order`   | no       | integer sort key, low first (default 100). |
| `enabled` | no       | `1` shows the tile, `0` hides it (default 1). |
| `build`   | no       | project dir to `make` before deploy (default: this project). Empty = don't build (e.g. DOOM, which has its own build). |
| `deploy`  | no       | files to push to `$EVO_DIR`: `bin/<x>`, a project-relative path, or absolute. Default: the binary named by the Makefile's `APP`. Empty = push nothing. |

## Build system (`shared/app.mk`)

Every app shares one build profile, so a Makefile is three lines:

```make
APP  := myapp
SRCS := myapp.c
include ../../shared/app.mk
```

Knobs (set before the `include`): `GRAPHICS` (1 = link `evo_graphics_lib`, the
default; 0 for a non-graphical utility), `USE_DSI` (1 = link the service-broker
for apps that read unit services over DSI, like `evo_gps`), `OPT`,
`FAST_SRCS` (sources built on their own at `-O2` and linked in, for hot
rendering code in an otherwise `-O0` app), `STRIP`, `KIND` (`exe`/`so`), and
`EXTRA_CFLAGS` / `EXTRA_INCLUDES` / `EXTRA_LIBS` / `EXTRA_LDFLAGS`. Output lands
in `../../bin/<APP>`.

The shared app headers are in `shared/include/`: `evo_appinput.h` (iDrive
BACK → exit), `font8x8_basic.h` (bitmap font), `evo_feed.h` (iDrive feed ring),
`evo_hmistate.h` (the summon-gate struct).

## Deploy & configure

```sh
./scripts/deploy.sh               # build + push every enabled app + launcher + manifest
./scripts/deploy.sh gps terminal  # just these apps (+ the manifest)
./scripts/deploy.sh --core        # just the launcher runtime + manifest
./scripts/deploy.sh --dry-run     # build + generate the manifest, print pushes, no ssh
./scripts/deploy.sh --restart ... # also restart the launcher after pushing
```

Connection and layout live in [`scripts/evo_env.sh`](../scripts/evo_env.sh),
which carries **no secret**. Put your unit's IP + root password in
`scripts/evo_env.local.sh` (gitignored — copy `evo_env.local.sh.example`) or
pass them per-command:

```sh
EVO_HOST=192.168.0.9 EVO_PASS='...' ./scripts/deploy.sh
```

Defaults: `EVO_HOST=169.254.199.119`, `EVO_DIR=/fs/sda1/evo`. Transfers use
`ssh + cat` (no sftp/scp dependency, which QNX dropbear often lacks).

## How it works on the unit

- **Manifest.** `scripts/gen_manifest.sh` scans `projects/*/app.desc` into
  `build/launcher.manifest` (`Name|command|icon`, on-unit absolute paths). It's
  the single source of truth; don't hand-edit it.
- **Hot-reload.** The launcher re-reads the manifest whenever its mtime changes,
  on each summon — so a redeploy shows added/removed tiles with no restart.
- **Scrolling.** Tiles sit in one row of six, like the OEM main menu. With more
  than six apps the row scrolls sideways one tile at a time; arrows at the card
  edges and a row of dots show where you are.
- **Summon gate.** The `evo_hmistate` daemon tells the launcher whether the OEM
  HMI is on the main Startmenu, so the page only opens from there (not from a
  submenu or a notification). It fails open: if the daemon is absent the
  launcher summons as before. Disable the gate with `echo 0 > $EVO_DIR/launcher_gate`.
- **Autostart.** `evo_autostart.sh` supervises `evo_hmistate` + `evo_launcher`
  at boot. Kill switch: `touch $EVO_DIR/autostart.off`.

## Look and theme

The launcher draws itself with the head unit's own HMI resources, so it matches
whatever the unit is coded to show. Nothing from the HMI is shipped in this
repo: every file below is read from the unit at start-up.

| What | Where it comes from on the unit |
|---|---|
| HMI generation | the `/opt/hmi/data` symlink (`…/ID5/data`) |
| ID version, brand | the HU coding persisted on the Jacinto (group 3000: byte 29 mask `0x1C` = ID version, byte 133 mask `0x3C` = brand) |
| Fonts | the `.ttf` files in `<ro>/<brand>/font`, then `<ro>/common/font` |
| Tiles and selection frame | `<ro>/common/assetDB/main_menu_plusplus_heroes/resources/hauptmenu_{a,i}_media{,_rahmen}.png` — the HMI's blank "media" main-menu pad |
| Accent colour | the mean colour of `<ro>/common/widgetasset/homebutton/verlauf_home_orange.png` |

`<ro>` is `/opt/hmi/data/ro`. The launcher log (`/dev/shmem/evo_launcher.log`)
prints one `theme:` line with what was detected and loaded.

- **Styles.** `id5-glossy` (ID4++/ID5/ID5++: the pad artwork above) and `flat`
  (procedural tiles; used for ID6 lines and whenever the pad artwork is
  missing). If no font is found, text falls back to the 8x8 bitmap font.
- **Override.** `echo flat > $EVO_DIR/launcher_theme` (or `id5`, `auto`), then
  restart the launcher. Only the BMW ID5++ look has been checked against a live
  HMI; the others are derived from the files alone.
- **Host preview.** `make -C projects/evo_launcher preview` builds
  `bin/host/ui_preview`, which renders the same frames on the PC from a copy of
  those unit files (see the header of `projects/evo_launcher/tools/ui_preview.c`).
  Keep such copies out of the repo.
- **Code.** `projects/evo_launcher/launcher_ui.c` holds detection, asset loading
  and all drawing; `evo_launcher.c` keeps input, the overlay and app launching.
  PNG and TrueType decoding are `stb_image` / `stb_truetype` in
  `shared/third_party/stb/`.

## Layout

```
shared/app.mk            one build profile for every app
shared/include/          shared app headers
scripts/evo_env.sh       connection + helpers (no secret)
scripts/gen_manifest.sh  app.desc -> build/launcher.manifest
scripts/deploy.sh        build + push + manifest, one command
projects/<app>/app.desc  per-app tile registration
launcher_sdk/template/   copy-to-start a new app (this folder)
```

## Safety

This targets a **bench** head unit. The launcher only draws over the HMI and
spawns your apps — it does not touch vehicle buses. Apps that read unit services
(DSI/UDS) or write coding are a separate concern; keep risky writes for when
you're watching the screen.

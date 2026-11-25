# Licences — nbt-evo-homebrew-bin-@VER@

This archive is a collection of separate programs under different licences. Two of
them, `evo_snes` and `evo_genesis`, contain emulator cores that may **not be sold
or used commercially**, so the archive as a whole may only be passed on
**non-commercially**, unchanged or with these notices kept.

Source for everything: <https://github.com/walnut-engineering/nbt-evo-homebrew>
(the tag `v@VER@`), plus the upstream projects named below at the commits given.

| Files | What it is | Licence |
|---|---|---|
| `payload/evo_launcher`, `evo_hmistate`, `evo_terminal`, `evo_sysmon`, `evo_gps`, `evo_tee_most.so`, `evo_feedtap`; `most`; `extras/evo_reboot`, `evo_screenshot`, `evo_cursor`, `evo_gltest`; all `.sh` scripts; `payload/icons/` | this project's own code and drawings | MIT — `MIT.txt` |
| `payload/doom_evo` | the DOOM engine (id Software's source release) with this project's port | GPL-2.0 — `GPL-2.0.txt`. Source: `projects/doom_evo` |
| `payload/q3/quake3e.arm`, `payload/q3/demoq3/*.so` | Quake 3: the quake3e engine and ioquake3's game code, with this project's QNX / GLES2 port | GPL-2.0 — `GPL-2.0.txt`. Source: `projects/quake3e_qnx` (patches and glue), applied by its `fetch.sh` / `fetch_ioq3.sh` to `ec-/Quake3e` at `dec793de9677bc9c16c7027e8905c2d4f4643a19` and `ioquake/ioq3` at `52088b9eae247d8e321c8dc1fb49a879652854f9` |
| `payload/evo_padd`, `extras/evo_padfake` | Xbox wireless controller daemon, a port of xow (Severin von Wnuck), and its test tool | GPL-2.0-or-later — `GPL-2.0.txt`. Radio constants from OpenWrt's mt76 driver: `ISC-mt76.txt`. Source: `projects/evo_padd` |
| `payload/evo_nes` | NES emulator: the Nofrendo core (Matthew Conte) with this project's port and frontend | core: GNU Library GPL 2 — `LGPL-2.0-Nofrendo.txt`; the rest MIT. Source, including everything needed to rebuild the program with a changed core: `projects/evo_nes` |
| `payload/evo_snes` | SNES emulator: this project's frontend (MIT) linked with **Snes9x 2010** | the Snes9x licence — `Snes9x.txt`. **Non-commercial use only.** |
| `payload/evo_genesis` | Genesis emulator: this project's frontend (MIT) linked with **Genesis Plus GX** | the Genesis Plus GX licence — `Genesis-Plus-GX.txt`. **May not be sold or used in a commercial product or activity.** |

## The two emulator cores

- **Snes9x 2010** — `libretro/snes9x2010` at commit `@SNES_COMMIT@`,
  with two changes for this CPU and toolchain (`projects/evo_snes/patches/`).
- **Genesis Plus GX** — `libretro/Genesis-Plus-GX` at commit
  `@GEN_COMMIT@`, unmodified.

Their complete source as built is published beside this archive as
`nbt-evo-homebrew-cores-src-@VER@.tar.gz`. The frontend they are linked with is
`projects/evo_retro` in the repository; the build recipes are
`projects/evo_snes/core.sh` and `projects/evo_genesis/core.sh`.

## Not in this archive

No game data, ROMs or BIOS images; none of Microsoft's firmware for the Xbox
adapter; no BMW or Harman software (the programs use libraries that are already on
the head unit). `evo_gps` is built against Harman's DSI headers, which are MPL-2.0
and are in the repository (`projects/nbt-dsi-toolkit/dsi_sdk`).

Nintendo, Super Nintendo, Sega, Genesis, Mega Drive, Xbox, DOOM, Quake and BMW are
trademarks of their owners. This project is not affiliated with any of them.

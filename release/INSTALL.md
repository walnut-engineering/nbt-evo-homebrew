# NBT EVO Homebrew — prebuilt binaries

Prebuilt ARM/QNX binaries for the homebrew launcher and apps, plus an on-unit
installer. This is the compiled counterpart to the source repo
(`walnut-engineering/nbt-evo-homebrew`); the source is the place to read what any
of this does.

> **For a bench / test unit you own.** Not affiliated with or endorsed by BMW.
> Everything here runs in user space; nothing reflashes or modifies OEM images.
> The one optional step that touches a system file (`--enable-knob`) backs it up
> first and is fully reversible (see below).

> **Non-commercial.** `evo_snes` and `evo_genesis` contain the Snes9x and Genesis
> Plus GX emulator cores, which may be passed on free of charge but **not sold or
> used commercially**. So this archive as a whole is for non-commercial use only.
> Every licence is in `LICENSES/` (start with `LICENSES/README.md`).

## What's in the box

```
install.sh            on-unit installer
uninstall.sh          disable / restore
payload/              the launcher runtime (copied to /fs/sda1/evo)
  evo_launcher          resident launcher (summoned by the iDrive knob or a gamepad)
  evo_hmistate          "are we on the Startmenu?" summon gate
  doom_evo              DOOM engine            doom_launch.sh   its tile's launch wrapper
  q3/quake3e.arm        Quake 3 engine (GLES2) + q3/demoq3/*.so game modules, q3_launch.sh
  evo_nes               NES emulator           nes_launch.sh
  evo_snes              SNES emulator          snes_launch.sh
  evo_genesis           Genesis emulator       genesis_launch.sh
  evo_terminal          Terminal tile          evo_sysmon   System monitor tile
  evo_gps               GPS tile (DSI)
  evo_padd              Xbox wireless controller daemon (needs the adapter's firmware, below)
  evo_autostart.sh      boot supervisor (started by the shim)
  evo_tee_most.so       iDrive-feed tee (LD_PRELOADed into 'most')
  evo_feedtap           diagnostic: print live knob/tilt/button events
  launcher.manifest     the 8 tiles (DOOM, Quake 3, NES, SNES, Genesis, Terminal, System, GPS)
  icons/                tile icons (cpu, gps, terminal, nes, snes, genesis)
most                  the fail-safe MOST shim (installed only with --enable-knob)
extras/               non-tile tools (reboot, screenshot, cursor, gltest, padfake)
LICENSES/             every licence that applies, and which file it applies to
SHA256SUMS            checksums for everything above
```

## Prerequisites

- A root shell on the unit. Either `ssh root@<unit>` over the USB-ethernet link,
  or run the installer straight off a USB stick (it mounts on the unit as
  `/fs/sda1`, which is exactly where the launcher lives). The unit has no `tar`:
  unpack the archive on a PC and copy the folder over.
- The on-unit proprietary libraries (`libscreen.so.1`, the ILM/GENIVI layer
  manager, the DSI servicebroker) are already present on every NBT EVO; nothing
  from BMW/Harman is shipped here.

## Install

```sh
# on the unit (or from the USB stick's mount point):
sh install.sh                # 1) stage everything into /fs/sda1/evo — no system changes
sh install.sh --enable-knob  # 2) turn on iDrive-knob input + boot autostart (see below), then reboot
```

Step 1 is safe and non-invasive: it only writes to `/fs/sda1/evo`, a normal data
partition. After it, you can test an app directly, e.g.
`cd /fs/sda1/evo && ./evo_sysmon`. Installing over an older version keeps your
games, saves, settings and firmware; it stops the running launcher daemons to
replace them (the boot supervisor starts them again within seconds).

Step 2 is what makes the **knob** summon the launcher. It installs a small
**fail-safe shim** as `/opt/sys/bin/most`:

- the real MOST client is preserved as `/opt/sys/bin/most.real`;
- at boot the shim starts the launcher's feed tee and the autostart supervisor,
  then execs the real `most`;
- if the tee fails to load, or the kill switch `/fs/sda1/evo/most_tee.off`
  exists, the shim just execs the clean real `most` — the car behaves stock.

Reboot after `--enable-knob`. Then, on the Startmenu, tilt the iDrive knob
up/down to open the launcher; rotate to move, press to launch, tilt up/down to
close. `BACK` quits an app back to the launcher.

## What you add yourself

No games, no BIOS files and no Microsoft firmware are in this archive.

| For | Copy | To |
|---|---|---|
| DOOM | your own `doom1.wad` (shareware is fine) | `/fs/sda1/evo/doom1.wad` |
| Quake 3 | the free demo's `pak0.pk3` | `/fs/sda1/evo/q3/demoq3/pak0.pk3` |
| NES | your own `.nes` files | `/fs/sda1/evo/nes/` |
| SNES | your own `.sfc` / `.smc` files, plain or one game per `.zip` | `/fs/sda1/evo/snes/` |
| Genesis | your own `.md` / `.gen` / `.bin` files (also `.sms`, `.gg`), plain or zipped | `/fs/sda1/evo/genesis/` |
| Xbox controllers | the adapter's firmware, `FW_ACC_00U.bin` | `/fs/sda1/evo/xbox/` |

## Xbox controllers

`evo_padd` drives an **Xbox One Wireless Adapter** (USB `045e:02e6`) plugged into
the head unit, so the launcher, the games and the emulators take Xbox One
controllers. The adapter does nothing until it is given its firmware, which is
Microsoft's and is not included: on a PC, run
[`fetch_firmware.sh`](https://github.com/walnut-engineering/nbt-evo-homebrew/blob/main/projects/evo_padd/fetch_firmware.sh)
from the source repo (it downloads the Windows driver package from Windows Update
and checks the file's hash) and copy `FW_ACC_00U.bin` to `/fs/sda1/evo/xbox/`.

With the firmware in place the daemon starts at boot (it needs `--enable-knob`,
which is what runs the boot supervisor). To pair: press the button on the adapter,
then hold the controller's pair button until its logo flashes fast. The Xbox button
opens the launcher from the BMW main menu and opens each emulator's menu; the full
button tables are in the source repo, `projects/evo_padd/README.md`.

## Turn it off / uninstall

```sh
touch /fs/sda1/evo/most_tee.off   # instant off switch: next boot is stock (files stay)
sh uninstall.sh                   # restore the real 'most', stop the daemons (keep files)
sh uninstall.sh --purge           # the above, and delete /fs/sda1/evo — your games and saves too
```

A reboot after `uninstall.sh` returns the unit fully to stock behaviour.

## Notes

- **Tile icons for DOOM and Quake 3** are generated from the games' own artwork and
  are not shipped (copyright), so those two tiles show a plain label until you
  generate them (`projects/evo_launcher/tools/extract_game_icons.py` in the source
  repo). The other six tiles ship original icons.
- **The emulators** have been tested from scripts and screenshots on a bench unit;
  how they feel and sound in play has not been judged by a person yet. SNES
  cartridges with extra processors (Super FX, SA-1) and Sega CD are unproven. The
  details are in each tile's README in the source repo (`projects/evo_snes`,
  `projects/evo_genesis`, `projects/evo_nes`).
- **Quake 3** runs on the PowerVR GPU (GLES2 renderer2) at about 22 fps with iDrive
  or gamepad input and QSA sound.
- Logs land in RAM: `/fs/sda1/evo/autostart.log`, `/dev/shmem/evo_launcher.log`,
  `/dev/shmem/evo_tee_most.log`, `/dev/shmem/evo_padd.log`.
- Verify downloads against `SHA256SUMS` (`sha256sum -c SHA256SUMS` on a desktop).

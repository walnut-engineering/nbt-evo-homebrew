# Setup — building the NBT EVO Homebrew launcher

This repo contains only **source**. Two things it depends on live outside git —
a large external install (the QNX SDP) and some unit-derived binaries — and must
be present before you build. On a fresh clone you provide them yourself.

## 1. QNX 6.5.0 SDP (the cross-compiler)

The apps are built with the QNX 6.5.0 SDP's gcc 4.4.2 cross-compiler, which you
supply yourself (see `shared/sdp.mk` / `shared/app.mk`).

- Default location: `$HOME/qnx650_sdp/qnx650`
- Override per build: `make QNX_SDP=/path/to/your/qnx650`

The build uses `$(QNX_SDP)/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2`
and the matching `ntoarmv7` binutils.

## 2. `toolchain/target/` — the unit's target libraries

Linking needs a copy of the head unit's own shared libraries (`libscreen.so.1`,
the ILM/GENIVI layer-manager libs, …). The build references them at:

```
toolchain/target/lib
toolchain/target/opt/sys/lib
```

These are **unit-derived binaries** (BMW/Harman firmware), so they are **not
committed** (`/toolchain` is gitignored). Copy the relevant libraries off a head
unit you own into `toolchain/target/…`.

## 3. DSI service-broker libraries (only for DSI apps)

The DSI client **source** is bundled in this repo at
[`projects/nbt-dsi-toolkit/`](projects/nbt-dsi-toolkit) (Harman's DSI reference
implementation, MPL-2.0). What is *not* included are its runtime/link libraries
`projects/nbt-dsi-toolkit/dsi_sdk/lib/` — those are proprietary binaries that
exist on every NBT unit. For DSI apps (the **GPS** tile, `USE_DSI=1`), copy them
off your own unit into that directory. See
[`projects/nbt-dsi-toolkit/NOTICE`](projects/nbt-dsi-toolkit/NOTICE).

(All non-DSI tiles — DOOM, Quake 3, the emulators, Terminal, System — build without these.)

### DOOM / Quake extras
The **DOOM** and **Quake 3** tiles have their own engines + build scripts and
need the games' own data (shareware `doom1.wad`, `pak0.pk3`) and, for Quake, the
upstream `quake3e` / `ioq3` sources. Those are external/gitignored — see each
project's launch and `fetch` scripts. The utility tiles need none of this.

## Build

```
# one app
cd projects/evo_sysmon && make            # -> ../../bin/evo_sysmon (ARM QNX)

# everything + push to the unit (reads scripts/evo_env.local.sh for host+creds)
cp scripts/evo_env.local.sh.example scripts/evo_env.local.sh   # then edit
./scripts/deploy.sh                        # build all, push binaries+icons+manifest
```

Adding a tile is a 3-line Makefile + a one-line `app.desc` — see
[`launcher_sdk/README.md`](launcher_sdk/README.md).

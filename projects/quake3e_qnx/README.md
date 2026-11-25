# quake3e on the NBT EVO (QNX 6.5 / PowerVR SGX544MP)

Goal: run **Quake 3** on the head unit as a launcher tile, using the real GPU.
Base: **quake3e** (ec-/Quake3e), renderer **renderer2** ported to **OpenGL ES 2.0**,
built with the **QNX 6.5 SDP gcc 4.4.2**, data = the **free Q3 demo pak**.

The upstream source is fetched into `../quake3e/` (gitignored). This directory
holds our tracked port work: patches, the QNX platform layer, and the build.

## Why this is feasible (de-risked 2026-11-11)
- **Hardware GLES2 works** — a standalone app gets a direct GLES2 context on the
  PowerVR SGX544MP at 60 fps. Recipe in `../../docs/gpu-gles2-breakthrough.md`
  (GRAPHICS_ROOT=/lib/graphics/omap5430 + eglGetDisplay(1) + direct EGL on a
  SCREEN_USAGE_OPENGL_ES2 window). `projects/evo_gltest` is the proof.
- **The engine core compiles on the SDP gcc 4.4.2** — q_math.c / q_shared.c build
  clean once QNX is added to q_platform.h (`patches/0001-q_platform-qnx.patch`).
  No fundamental compiler incompatibility.

## The port (replace SDL glue with QNX-native)
quake3e's client uses SDL2 for GL-context/input/sound (`code/sdl/*`). SDL2 isn't
viable on QNX 6.5, so we provide QNX implementations of the same seams:
- **glimp** (`GLimp_Init` / `GLimp_EndFrame` / `GLimp_Shutdown`) → EGL + GLES2 on a
  QNX Screen window (the evo_gltest recipe). Replaces `sdl_glimp.c`.
- **input** → iDrive feed ring (evo_feed) + USB keyboard. Replaces `sdl_input.c`.
- **sound** → QSA/ALSA (as DOOM's i_sound_evo does). Replaces `sdl_snd.c`.
- **sys** → `code/unix/unix_*` is mostly POSIX; QNX needs small tweaks.
- **renderer2 → GLES2** → port the rend2 GLSL renderer to GLES2 (GLSL ES, no
  fixed-function, format/extension differences). The real rendering work.

## Milestones
- [x] M0  GLES2 pipeline proven on-unit (evo_gltest, 60 fps).
- [x] M1  Engine core compiles on the SDP (q_platform QNX patch).
- [x] M2  **Dedicated server built + runs on the unit** (2026-11-11). `build.sh`
          compiles the 64-source Q3DOBJ set with the SDP gcc 4.4.2 and links a
          508 KB QNX armle-v7 ELF. On-unit it prints `Q3 1.32e qnx-arm`, runs
          `Com_Init` → `FS_Startup` (scans `/fs/sda1/evo/q3`), and reaches the
          normal "install Quake III Arena" gate — i.e. qcommon+server+botlib+
          game-VM+sys all build, link and run. First real binary.
- [x] M3  **QNX glimp + client links + GLES2 window opens on the unit** (2026-11-11).
          `build_client.sh` builds the full client (static renderer, USE_SDL=0) with
          the QNX platform layer (`qnx/qnx_glimp.c` EGL/GLES2, `qnx_input.c`,
          `qnx_snd.c` stubs) and a minimal stub renderer (`qnx_ref_stub.c`). On-unit:
          `QNX Screen window 1440x540 (OPENGL_ES2) / EGL 1.4 / GL_RENDERER: PowerVR
          SGX 544MP` — the client opens a real hardware GLES2 window in-engine.
          (It then hits the demo-VM ABI wall: `User Interface is version 3, expected 6`.)
- [x] M4  **renderer2 → GLES2; the real rend2 renderer initialises on the unit's
          GPU** (2026-11-11). `build_client.sh` now links the full renderer2 set
          (rend2 + the renderercommon image/font/noise sources + the 28 stringify'd
          GLSL units) instead of the M3 stub, via the GL2→GLES2 compat shim
          `qnx/qnx_gl2compat.h`. On-unit it runs `R_Init` to completion:
          `GL_RENDERER: PowerVR SGX 544MP / OpenGL ES 2.0 / GLSL ES 1.00`,
          **`loaded 75 GLSL shaders (32 gen 28 light 15 etc)`**, `R_InitVaos`,
          `----- finished R_Init -----`. The renderer is live on the GPU. (It then
          hits the demo-VM ABI wall: `User Interface is version 3, expected 6` —
          same wall as M3, i.e. M5's job, not a renderer issue.)
- [x] M5a **native game modules built + ABI wall cleared** (2026-11-11). The game
          logic (ioquake3 GPL baseq3 source, `../ioq3`) is cross-built as NATIVE QNX
          shared libraries — `qagamearm.so`, `cgamearm.so`, `uiarm.so` — by
          `build_game.sh`, and loaded with `vm_game/vm_cgame/vm_ui = 0` (VMI_NATIVE,
          `VM_LoadDll` → dlopen → `dllEntry`+`vmMain`). Dedicated server on-unit:
          `VM_LoadDll(qagame) succeeded! / InitGame ... gamename\baseq3`, BotLib+AAS,
          bots `ClientConnect`/`ClientBegin` — **no `trap 425`**. Client on-unit:
          `VM_LoadDll(ui) succeeded! / 4 arenas parsed / 6 bots parsed /
          --- Common Initialization Complete ---` — boots to the **main menu**,
          rendered by the GLES2 renderer2, **no `version 3, expected 6`**.
- [x] M5b **VISIBLE + PLAYING on the CID** (2026-11-11). The game now shows on the
          head unit's screen and renders a live level. `qnx/qnx_ilm.c` registers our
          GLES2 window as a GENIVI ILM surface (window `SCREEN_PROPERTY_ID_STRING` =
          surface id) on its own layer, placed FIRST in CID screen 0's render order
          (= composited on top of the live HMI; confirmed vs the unit with
          `LayerManagerControl`). Boots to the Q3 **main menu**, and `+map q3dm7`
          with bots renders the arena through native qagame+cgame + renderer2/GLES2
          at **~22 fps @1440x540** (GPU-bound: backend ~42 of ~44 ms/frame).
- [x] M5c-render **world now renders correctly with textures** (2026-11-11,
          patch `0006`). The level looked black/garbled because of three distinct
          GLES2 bugs, all fixed:
          * **Textures sampled black** — rend2 uploads via the DSA shim, passing
            desktop-GL *sized/mismatched* internal formats (e.g. `GL_RGB8`, or
            `GL_RGB` internal with `GL_RGBA` data). GLES2's `glTexImage2D` requires
            `internalformat == format` and rejects sized formats, so **every**
            texture failed to allocate (`GL_INVALID_OPERATION`) and sampled black
            while geometry/texcoords were fine. Fix: force `internalformat = format`
            in `GLDSA_TextureImage2DEXT` (`tr_dsa.c`). This was the root "black world".
          * **Stale loading-screen "spritemap"** — no FBO + the sky doesn't fully
            cover the view, and `RB_BeginDrawingView` only clears depth, so the
            one-time loading screen bled through as scattered sprites. Fix: also clear
            `GL_COLOR_BUFFER_BIT` for the main world view (`tr_backend.c`).
          * **Screenshots** — `RB_ReadPixels` now reads `GL_RGBA` and packs to RGB
            (GLES2 rejects `GL_RGB` readback). The built-in `screenshot` command works;
            `timedemo 1 +demo <name>` runs as a reproducible benchmark.
          Good brightness on q3dm7 at `r_mapOverBrightBits 3 r_overBrightBits 2`.
- [x] M5c **input** (2026-11-12): `qnx/qnx_input.c` replaces `sdl_input.c` —
          keyboard + mouse (devu-kbd/devu-mouse, bounded blocking reader threads)
          and the **iDrive knob** (the `most`-tee feed ring `/dev/shmem/evo_ctrl_feed`;
          rotate→turn, tilt→move, push→fire, MENU/BACK→menu/quit, NAV/TEL→weapon).
- [x] M5c **QSA sound** (2026-11-12, heard on the bench): `qnx/qnx_snd.c` replaces
          `sdl_snd.c`. quake3e's DMA ring driven by a consumer thread whose blocking
          `snd_pcm_plugin_write` paces playback (same wrap logic as SDL's callback);
          DOOM's proven QSA recipe (S16_LE stereo 44100, BLOCK/START_FULL, 4096-byte
          frags) + the `audioconn -b 0xb8` browser source → speakers. Post-mix master
          gain (default 3x, `EVO_Q3_AUDIO_GAIN` percent) since the HU attenuates 0xb8.
          **Runtime gotcha:** the launch `LD_LIBRARY_PATH` MUST include
          `/opt/mm/lib/libasound` (the HARMAN libasound that resolves the preferred
          *playback* device `/dev/libasound/dev1-1`); the generic `/lib/libasound.so.2`
          only sees the capture node in `/dev/snd` → `open_preferred(PLAYBACK)` ENOENT.
- [x] M5c **launcher tile** (2026-11-12): `app.desc` + `target/q3_launch.sh` register a
          "Quake 3" tile in the homebrew launcher (order 15). The script carries the full
          GPU+QSA launch env, a generation lock + crash safety-net (restore the HMI layer,
          release the 0xb8 route), a tunable gain (`/fs/sda1/evo/q3_gain`) and args
          (`/fs/sda1/evo/q3_args`, default = a q3dm7 bot match). Deploy via
          `scripts/deploy.sh`. Verified end-to-end: summon → select → launch → gameplay with
          sound → BACK quits clean → HMI restored. Tile icons = each game's own title art,
          built by `evo_launcher/tools/extract_game_icons.py` (DOOM `M_DOOM` lump + Quake
          `logo512`), gitignored like the game data.
- [x] M5c **BACK = confirm-to-quit** (2026-11-12): in `qnx_input.c`, a single iDrive BACK no
          longer quits - it arms + prints an on-screen prompt (console notify area); a second
          BACK within `BACK_CONFIRM_MS` (3 s) quits. Prevents an accidental BACK from dumping
          you to the HMI mid-game.
- [ ] M5c Remaining: final brightness default (nice-to-have).

### M2 build
`./build.sh` (add `-v` to echo each compile). It drives the SDP gcc 4.4.2
directly over the exact dedicated-server source set (the Makefile's `Q3DOBJ`),
mirroring its per-group defines (`-DDEDICATED`, `-DBOTLIB`, `-fno-fast-math` for
the VM; interpreted VM only via `-DNO_VM_COMPILED`). Output:
`build/quake3e.ded.arm`. It continues past per-file errors and summarises
failures, so one pass surfaces every QNX-compat issue.

### Run the demo bot match on the unit (M2.5)
```
# host: fetch + carve the free demo pak0.pk3 (id's makeself installer; do NOT
# execute it), then push to the unit's demoq3/ dir (see git history for the
# carve: gzip payload starts at the first 1f 8b 08, tar out demoq3/pak0.pk3).
# unit:
cd /fs/sda1/evo/q3
./quake3e.ded.arm +set dedicated 1 +set fs_basepath /fs/sda1/evo/q3 \
  +set fs_game demoq3 +set sv_pure 0 +set bot_enable 1 +set com_hunkMegs 64 \
  +map q3dm7 +addbot sarge 4 +addbot grunt 4 +addbot major 3 +addbot daemia 3
```
Demo bots: sarge, grunt, major, daemia, stripe. The pak goes in `demoq3/`
(not `baseq3/`): the engine only accepts the demo-checksum pak under the
`demoq3` game dir; in `baseq3` it demands the retail checksum and aborts.

### QNX port fixes landed in M2
- `qnx_prefix.h` (forced via `-include`): renames QNX's unused `<sys/types.h>`
  `msg_t` aside so it stops colliding with quake3e's `msg_t` network-buffer type.
  Port glue, not an upstream edit.
- `patches/0002-unix-qnx-port.patch`: guards the stale SysV `<sys/ipc.h>`/
  `<sys/shm.h>` includes in `unix_main.c` (QNX 6.5 has no `<sys/shm.h>`), and
  gives `Sys_DefaultHomePath` a QNX branch returning `""` (the unit's `$HOME` is
  `/dev/shmem`, a flat namespace that rejects `mkdir`) so FS uses `fs_basepath`.

## Build
`fetch.sh` clones quake3e and applies the patches. Build uses the SDP toolchain
(see `../../shared/sdp.mk`); a dedicated `build.sh` (M2+) compiles the needed
sources with `arm-unknown-nto-qnx6.5.0eabi-gcc-4.4.2`, bypassing quake3e's
uname-based Makefile (no QNX platform in it).

### M2.5 interim validation — interpreted VM runs real game code (2026-11-11)
With the free demo `pak0.pk3` in `demoq3/` (`+set fs_game demoq3 +set sv_pure 0`),
the dedicated server ran a bot match on `q3dm7`: `InitGame`, BotLib init,
`loaded maps/q3dm7.aas`, `AAS initialized`, bots connected + spawned, and real
`Kill:` (telefrag) events — all executed by our **interpreted VM** on QNX/ARM
(`Architecture doesn't have a bytecode compiler, using interpreter`). This proves
the QVM interpreter + game + botlib + AAS all work on the unit.

Three ARM **alignment faults** (SIGBUS, QNX signal 10) were found and fixed on the
way (`patches/0003-arm-unaligned-access.patch`): the unaligned `*(int*)` zip
central-dir reads in `unzip.c`; the QVM operand decode in `vm.c` (`code_pos`
walks byte-by-byte so a 4-byte operand is unaligned); and the VM load/store
opcodes in `vm_interpreted.c` (VM memory at VM-controlled, often-unaligned
addresses). All now go through `memcpy`. QNX on this unit enforces strict ARM
alignment (SCTLR.A), so these are mandatory for any Q3 build here.

**Known limit — demo data ABI:** after a few bots spawn the demo run ends with
`Bad game system trap: 425`. The 1999 demo `qagame.qvm` uses a game↔engine
syscall numbering that doesn't match quake3e's modern (1.32+) ABI — the engine
itself warns the demo pak is "not guaranteed or supported". This is a *data*
mismatch, not a port bug (it is deterministic and everything before it runs).
**Implication for M5:** supply game *code* that matches quake3e's ABI — build
quake3e's own `qagame` (QVM, or more practically a native QNX `.so` cross-built
with the SDP gcc like everything else) and pair it with the demo's media assets.

### The QNX platform layer (`qnx/`, replaces `code/sdl/`)
- `qnx_glimp.c` — GLimp_Init/EndFrame/Shutdown/GL_GetProcAddress + gamma/clipboard.
  Real GLES2 on the PowerVR via the evo_gltest recipe (Screen window + eglGetDisplay(1)
  + direct EGL). `GL_GetProcAddress` = dlsym(RTLD_DEFAULT) then eglGetProcAddress;
  desktop-GL-only names return NULL (the M4 surface).
- `qnx_input.c` — IN_Init/IN_Frame/HandleEvents/Key_CapsLockOn stubs (no event source
  yet; iDrive feed + USB keyboard via evo_appinput.h is next).
- `qnx_snd.c` — SNDDMA_* stub (SNDDMA_Init returns qfalse; QSA backend later).
- `qnx_ref_stub.c` — a throwaway stub renderer (GetRefAPI + refexport_t) so the client
  links and the window opens for M3, decoupled from the real renderer2 port (M4). It
  opens the context in BeginRegistration and clears/swaps; all scene calls are no-ops.

### Build the client (M3/M4)
`./build_client.sh` (DEBUG=1 for symbols). Static renderer (USE_RENDERER_DLOPEN=0),
USE_SDL=0, bundled libjpeg, QNX platform layer + the real renderer2. It also builds
the host `stringify` tool and turns `renderer2/glsl/*.glsl` into C fallback-shader
strings. Output `build/quake3e.arm`. Run on-unit with
`GRAPHICS_ROOT=/lib/graphics/omap5430` and an LD_LIBRARY_PATH that includes both the
graphics dir (GPU, see docs/gpu-gles2-breakthrough.md) and `/opt/mm/lib/libasound`
(QSA playback, see the M5c sound note above). The proven full launch env:
```
GRAPHICS_ROOT=/lib/graphics/omap5430 \
LD_LIBRARY_PATH=/lib/graphics/omap5430:/opt/mm/lib/libasound:/opt/mm/lib:/opt/sys/lib:/lib:/usr/lib:/fs/sda0/lib \
EVO_Q3_AUDIO_GAIN=300 ./quake3e.arm +set fs_basepath /fs/sda1/evo/q3 +set fs_game demoq3 \
  +set sv_pure 0 +set com_hunkMegs 64 +set bot_enable 1 +devmap q3dm7 +addbot sarge 2 +addbot grunt 2
```
(The M3 stub `qnx/qnx_ref_stub.c` is kept in the tree for reference but is no longer
compiled.)

### The renderer2 → GLES2 port (M4)
rend2 is ioq3's shader-based renderer; its `qgl.h` already carries a desktop-vs-ES
proc split and it reaches GL only through `qgl*` pointers resolved at runtime via
`ri.GL_GetProcAddress` (→ dlsym into libGLESv2). So the port is small and surgical
(`patches/0004-renderer2-gles2.patch` + `qnx/qnx_gl2compat.h`):
- **`qnx_gl2compat.h`** — on `__QNXNTO__`, `qgl.h` includes this instead of desktop
  `<GL/gl.h>` + `glext.h`. It pulls in `<GLES2/gl2.h>` + `gl2ext.h` and back-fills the
  ~43 desktop GL types/enums rend2 references that ES2 lacks (`GLdouble`, `GL_RGBA8`,
  `GL_DEPTH_COMPONENT24`, `GL_POLYGON`, …). Tokens only need to *exist* to compile;
  ES2-illegal uses (e.g. sized internalFormats) are on gated paths.
- **GLSL ES header** (`tr_glsl.c`) — rend2's shaders are authored in
  attribute/varying/gl_FragColor/texture2D style = native **GLSL ES 1.00**, so the ES
  branch just prepends `#version 100` + precision qualifiers (no keyword remap). All
  **75 shader permutations compile on the PowerVR**.
- **skip FBO-only post shaders** (`tr_glsl.c`) — shadowmask/ssao/depth-blur use
  `sampler2DShadow` / `dFdx()` (not in GLSL ES 1.00) and are only bound on the
  framebuffer-object path, which is off on ES2. Skipped on QNX.
- **ES2 proc shims** (`tr_extensions.c`) — after the loader resolves the qgl table,
  the desktop-only slots (`glDepthRange`/`glClearDepth` double, `glDrawBuffer`,
  `glPolygonMode`) are NULL on ES2; install wrappers that call `glDepthRangef`/
  `glClearDepthf` or no-op.
- one upstream typo fixed (`TK_GENEGIC` → `TK_GENERIC`, `tr_shader.c`).

### The game VM modules (M5a) — native QNX shared libraries
quake3e is engine-only, so the `qagame`/`cgame`/`ui` game logic comes from
**ioquake3**'s GPL baseq3 source (`../ioq3`, gitignored; `fetch_ioq3.sh`). quake3e
supports native modules (`VM_Create(…VMI_NATIVE)` → `VM_LoadDll` → `dlopen`, resolving
`dllEntry` + `vmMain`), and ioq3 ships only a CMake build (no classic q3lcc/q3asm QVM
toolchain) — so `build_game.sh` cross-compiles the game logic with the SDP gcc into
**native `.so` modules** (`qagamearm.so`, `cgamearm.so`, `uiarm.so`; the engine loads
`<name>` + `ARCH_STRING`"arm" + `DLL_EXT`".so"). Run with `vm_game 0 vm_cgame 0 vm_ui 0`.

Why the ABI matches with almost no patching: the frozen net/VM structs
(`playerState_t`/`entityState_t`/`usercmd_t`) are byte-identical between ioq3 and
quake3e; the game `G_` import enum matches (quake3e only appended traps at the tail);
and ioq3 explicitly numbers its QVM-only helper traps `CG_MEMSET = 100` (appended after
the real traps), so cgame/ui trap numbers already line up — no realignment needed. The
only patch is `ioq3_patches/0001-q_platform-qnx.patch` (QNX block). g_rankings.c (defunct
Global Rankings) and cg_newdraw.c (`#error … not … for classic Q3A`) are excluded to
match baseq3. Build with `-Wl,--no-undefined` so a missing module source fails the link
here rather than at `dlopen` on the unit (that caught `ui_sppostgame.c`).

Deploy the three `.so` into the active game dir (`demoq3/`) alongside the demo pak.

Status: **complete and playable.** The full client runs on the unit's GPU — the
native `uiarm.so` / `cgamearm.so` / `qagamearm.so` modules load, renderer2 draws
on the PowerVR SGX544MP (75 GLSL shaders, `finished R_Init`), and the game is
**visible over the live HMI** (own ILM layer), with **iDrive-knob input and QSA
sound**, packaged as a **"Quake 3" launcher tile** (verified end-to-end: summon →
launch → play with sound → BACK quits clean → HMI restored). `+devmap q3dm7` with
bots renders at ~22 fps @1440×540. Supply the free Q3 demo `pak0.pk3` in
`demoq3/`. The only open item is a cosmetic default-brightness tweak.

## Gamepad

With [evo_padd](../evo_padd/README.md) running and an Xbox controller connected the
game is twin-stick: left stick moves, right stick looks, RT attacks, LT zooms, A jumps,
B crouches, X uses the item, LB / RB / Y change weapon, View shows the scores, Menu
opens the menu and the Xbox button pressed twice quits. In menus the stick or D-pad
moves, A selects and B goes back. `EVO_Q3_PADLOOK=<percent>` scales the look speed.
The pad is read in `qnx/qnx_input.c` (`pad_service`).

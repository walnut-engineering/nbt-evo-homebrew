# Hardware OpenGL ES 2.0 WORKS — the real GPU path (2026-11-11)

The year-old "dual-GPU" effort chased **2D HMI acceleration** (Vivante GAL
`gco2D_*`, Cairo bridge) and never beat software — that remains dead (see the
status note in [README.md](./README.md)).

**But real hardware 3D — direct OpenGL ES 2.0 on the PowerVR SGX544MP — works
from a standalone process.** This is the path for 3D games (Quake 3). Proven by
`projects/evo_gltest` (a minimal EGL + GLES2 triangle):

```
eglGetDisplay(1) -> 1
EGL 1.4  vendor=QNX Software Systems
GL_RENDERER = PowerVR SGX 544MP    GL_VERSION = OpenGL ES 2.0 build 1.9@2291151
RENDERED 300 frames in 4976 ms = 60.3 fps (640x360)   # vsync-locked at 60 Hz
```

## The working recipe (what the old RE missed)

Three things all have to be right; miss any one and `eglGetDisplay` returns
`EGL_NO_DISPLAY` with `EGL_SUCCESS` (which is what blocked the old attempts):

1. **`GRAPHICS_ROOT=/lib/graphics/omap5430`** (a symlink to `.../omap5430_ES2_0`).
   It must be the dir that contains **`graphics.conf`** — that's where `libEGL`
   finds its WSEGL/WFD driver list. Pointing at the driver-`.so` dir alone (e.g.
   `/fs/sda0/lib/graphics/omap5430_ES2_0`) is NOT enough; it lacks `graphics.conf`.
2. **`eglGetDisplay((EGLNativeDisplayType)1)`** — the `screen` graphics.conf numbers
   the EGL displays: **1 = PowerVR SGX (3D)**, 2 = Vivante GC320 (2D). `EGL_DEFAULT_DISPLAY`
   (0) is not configured → `NO_DISPLAY`.
3. **Use EGL directly** (`libEGL.so.1` + `libGLESv2.so.1`), create a QNX **Screen
   window** with `SCREEN_USAGE_OPENGL_ES2`, and `eglCreateWindowSurface` on it. The
   old RE tried to shoehorn `screen-gles2.so` into its 2D "blit-module" abstraction;
   that was never the GLES path. (The GENIVI forwarding EGL `libEGL_X` is NOT needed
   and does not help a standalone app — `evo_glfwd` confirms it also returns NO_DISPLAY.)

Runtime env (as `evo_gltest` is run):
```
GRAPHICS_ROOT=/lib/graphics/omap5430
LD_LIBRARY_PATH=/lib/graphics/omap5430:/lib:/usr/lib:/fs/sda0/lib
```
Link: `-l:libEGL.so.1 -l:libGLESv2.so.1` (from `toolchain/target/lib`); headers from the SDP.

## Probes

- `projects/evo_gltest/evo_gltest.c` — EGL + GLES2 window + spinning triangle + fps.
  Build: `make -C projects/evo_gltest`. Run on unit with the env above:
  `evo_gltest [W] [H] [frames]`.
- `projects/evo_gltest/evo_glfwd.c` — forwarding-EGL probe (shows `libEGL_X` is not
  the route). Built by the same Makefile.

## Open / next

- **Visibility/compositing:** the fps proves the GPU pipeline; the Screen window's
  z-order vs the live HMI/ILM still needs wiring for a full-screen game tile (same
  layer story as the launcher apps).
- **Headroom:** 60 fps is vsync-locked on a trivial scene; a heavier GLES2 benchmark
  is needed to size Quake 3 at 1440x540. The SGX544MP is the same GPU class that ran
  Quake 3 on other OMAP5 devices, so it is expected to be ample.
- **Quake 3:** port ioquake3 / quake3e with a GLES2 renderer (opengl2 renderer or a
  GL1.x→GLES wrapper), a QNX platform layer, EGL/GLES2 context (this recipe), iDrive/
  keyboard input, QSA audio (as DOOM does), and the game data (`pak0.pk3`).

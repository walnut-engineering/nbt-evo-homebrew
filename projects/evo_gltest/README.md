# evo_gltest

Two **EGL / OpenGL ES 2.0 probes** used to settle the Quake 3 render path on the
PowerVR SGX544 — not launcher tiles.

- **`evo_gltest`** — direct EGL/GLES2 probe: links `libEGL.so.1` + `libGLESv2.so.1`
  and tries to bring up a GPU context directly.
- **`evo_glfwd`** — probes the GENIVI **forwarding** EGL (`libEGL_X` /
  `libGLESv2_X_FORWARD`, the `IMGegl*` path the HMI apps use to reach the GPU
  through the compositor). A plain client's direct `eglGetDisplay` returns
  `NO_DISPLAY` because the screen server owns the GPU; this `dlopen`s the
  forwarding libs and reports whether they yield a usable display + init. Links
  nothing GPU (dlopen only).

## Build
```sh
make -C projects/evo_gltest       # -> bin/evo_gltest and bin/evo_glfwd (ARM QNX)
```
`evo_gltest` links the EGL/GLESv2 client libraries from `toolchain/target/lib`
(proprietary on-unit libraries you supply).

## Run
```sh
evo_gltest        # direct EGL/GLES2 bring-up report
evo_glfwd         # forwarding-EGL probe report
```
See [`projects/quake3e_qnx`](../quake3e_qnx) for how the GLES2 path is actually used.

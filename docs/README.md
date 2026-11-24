# EVO graphics — reference notes

A small set of reference notes for the graphics stack behind the apps in this
repo: QNX **Screen** + the GENIVI **ILM** layer manager, the software compositor
([`shared/evo_graphics_lib`](../shared/evo_graphics_lib)), and the hardware
**OpenGL ES 2.0** path. The verbose phase-by-phase development logs that used to
live here have been removed; these are the notes still worth keeping.

- **[gpu-gles2-breakthrough.md](gpu-gles2-breakthrough.md)** — how to get a real
  hardware **OpenGL ES 2.0** context on the PowerVR SGX544MP: the exact
  `GRAPHICS_ROOT` / `eglGetDisplay(1)` / direct-EGL recipe the Quake 3 port and
  `projects/evo_gltest` use. (The old 2D "dual-GPU" acceleration path never beat
  software and is not used.)
- **[backend-qnx-screen-guide.md](backend-qnx-screen-guide.md)** — choosing a QNX
  Screen backend / blit module on this hardware.
- **[system-architecture-analysis.md](system-architecture-analysis.md)** — how QNX
  Screen, the ILM layer manager, and our software compositor fit together.

`images/` holds the screenshots used by the top-level [`README.md`](../README.md).

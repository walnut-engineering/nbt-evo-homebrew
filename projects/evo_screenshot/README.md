# evo_screenshot

A **framebuffer capture** utility — grabs the live OEM HMI display via QNX Screen
(`screen_read_display` on a privileged `SCREEN_DISPLAY_MANAGER_CONTEXT`). Not a
launcher tile; a CLI tool, and the capture path [`evo_hmistate`](../evo_hmistate)
relies on.

## Output
A 16-byte header `{"EVOS", u32 w, u32 h, u32 bpp=4}` then `w*h*4` de-strided RGBX
bytes. A host-side tool converts it to PNG (resolving channel order there).

## Build
```sh
make -C projects/evo_screenshot   # -> bin/evo_screenshot (ARM QNX; no graphics lib)
```

## Run
```sh
evo_screenshot [outfile=/dev/shmem/hmi.raw] [display_index=0]   # run as root
```

# evo_cursor

A **mouse-cursor / pointer overlay** demo — and the proof that our ILM surface
composites over the live HMI with per-pixel alpha. Unlike DOOM/terminal (which
hide the HMI), this leaves the HMI visible and draws a cursor into a transparent
framebuffer on top of it. Not a launcher tile.

## Build
```sh
make -C projects/evo_cursor       # -> bin/evo_cursor (ARM QNX)
```

## Run
```sh
evo_cursor               # live: read /dev/usbmouse0 and move the cursor
evo_cursor -probe [secs] # draw an opaque / transparent / 50%-alpha test pattern (default 15s)
evo_cursor -anim  [secs] # self-animate along a Lissajous path, no mouse (default 20s)
evo_cursor -scale N      # cursor sprite pixel scale (default 3)
evo_cursor -sens  N      # divide mouse deltas by N (default 1; 2-3 = slower)
```
Teardown is trivial: the HMI was never hidden, so exiting just destroys our layer.

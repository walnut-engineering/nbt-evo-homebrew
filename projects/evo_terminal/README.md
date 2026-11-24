# evo_terminal

A **terminal emulator** tile — a real QNX shell on the car display, driven by a
USB keyboard.

## How it works
Renders a text grid through [`shared/evo_graphics_lib`](../../shared/evo_graphics_lib)
(the same Screen + ILM machinery DOOM uses), runs `ksh` on a pseudo-terminal, and
drives it from a USB keyboard:

```
USB keyboard (/dev/usbkbd0, PS/2 set-1 scancodes) -> ASCII -> pty master
shell (ksh on pty slave) -> pty master -> VT100 parse -> text grid
     -> scaled 8x8 bitmap font -> framebuffer -> present
```

## Build
```sh
make -C projects/evo_terminal     # -> bin/evo_terminal (ARM QNX)
```

## Run
Launcher tile **Terminal** (`order 20`, icon `terminal.rgba`). iDrive **BACK**
exits — no keyboard needed to quit; a USB keyboard is needed to type.

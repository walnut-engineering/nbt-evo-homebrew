# evo_padd — Xbox wireless controllers on the head unit

`evo_padd` is a daemon that makes Xbox One controllers work with the homebrew
launcher and its apps, through an **Xbox One Wireless Adapter** plugged into one of
the head unit's USB ports.

The head unit has no USB HID driver, no Wi-Fi driver for this adapter, and no usable
Bluetooth HID host. The adapter is a bare MediaTek MT76 radio that does nothing until
its host uploads firmware and runs a small access point for the controllers to join.
`evo_padd` does all of that in one process on top of QNX `libusbdi`. It is a port of
[xow](https://github.com/medusalix/xow) to QNX and C.

## What works

| | |
|---|---|
| Adapter | Xbox One Wireless Adapter, USB `045e:02e6` (the original, larger one) |
| Controllers tested | model 1708 (`045e:02ea`) and model 1537 (`045e:02d1`), both at once |
| Pairing | adapter button + controller pair button; remembered by the controller |
| Input | all buttons, both sticks, both triggers, Xbox button; up to 4 pads exposed |
| Bring-up | about 0.2 s from start to radio on |

Not done: rumble, headset audio, Elite paddles. The newer small adapter (`045e:02fe`)
and the Series X\|S controller (model 1914) are accepted but untested.

## What you need

1. The adapter, plugged into the head unit.
2. The adapter's firmware, `FW_ACC_00U.bin`. It is Microsoft's and is **not** in this
   repository or its releases. On a PC, run:

   ```sh
   ./fetch_firmware.sh          # needs curl, cabextract, sha256sum
   ```

   It downloads the Windows driver package from Windows Update, extracts the file
   and checks its hash. Copy the result to the unit as
   `/fs/sda1/evo/xbox/FW_ACC_00U.bin`.

## Build and install

```sh
make                      # -> ../../bin/evo_padd  (QNX 6.5 SDP toolchain, see shared/app.mk)
```

Copy `bin/evo_padd` to `/fs/sda1/evo/evo_padd` on the unit. `evo_autostart.sh` starts
it at boot when both the binary and the firmware file are present. To start it by
hand:

```sh
/fs/sda1/evo/evo_padd -w &
```

## Pairing a controller

1. Press the button on the adapter. Its light blinks for 30 seconds.
2. Turn the controller on and hold its pair button until the Xbox logo flashes fast.
3. The logo goes solid when the controller has joined.

After that the controller reconnects by itself whenever it is switched on. Stopping
the daemon switches connected controllers off.

## Controls

**Everywhere:** the Xbox button opens the homebrew page from the BMW main menu, and
leaves an app (each app's own "BACK" gesture, below).

**Launcher**

| Pad | Does |
|---|---|
| Xbox button, or D-pad / left stick up or down | open the page (close it again with up / down) |
| D-pad / left stick left and right, LB / RB | choose a tile |
| A | start it |
| B, X, Y, Menu, View | close the page |

When an app exits you are back on the page.

**DOOM** — twin-stick

| Pad | In the game | In DOOM's menu |
|---|---|---|
| Left stick / D-pad | move and strafe (diagonals) | move (repeats) |
| Right stick | turn, analogue | |
| RT, or X | fire | |
| A, or B | use / open | A select (`y` on a prompt) · B back (`n` on a prompt) |
| LT | walk (you run by default) | |
| LB / RB | previous / next weapon | |
| Y, or View | automap | |
| Menu | open the menu | close it |
| Xbox button | tap: menu · **hold 1 s: quit** | tap: close |

**Quake 3** — twin-stick

| Pad | In the game | In menus |
|---|---|---|
| Left stick / D-pad | move (diagonals) | move (repeats) |
| Right stick | look, analogue (`EVO_Q3_PADLOOK=<percent>` scales it) | |
| RT | attack | |
| LT | zoom | |
| A / B | jump / crouch | A select · B back |
| X | use item | |
| LB / RB, Y | previous / next weapon | |
| View | scores (hold) | |
| Menu | menu | back |
| Xbox button | **press twice: quit** | |

**NES**

| Pad | Does |
|---|---|
| D-pad / left stick | D-pad, with diagonals |
| A, B | NES A |
| X, Y | NES B |
| Menu / View | Start / Select |
| LB / RB | move in the ROM list |
| Xbox button | leave the ROM list; in a game **press twice: quit** |

A second controller is player 2.

**SNES and Genesis** ([evo_retro](../evo_retro/README.md))

| Pad | SNES | Genesis |
|---|---|---|
| D-pad / left stick | D-pad, with diagonals | D-pad, with diagonals |
| A / B / X / Y | B / A / Y / X (the same positions as on an SNES pad) | B / C / A / Y |
| LB / RB | L / R | X / Z |
| Menu / View | Start / Select | Start / Mode |
| Xbox button | the in-game menu: resume, save / load state, reset, quit | same |

A second controller is player 2. In the ROM list and the menu: A chooses, B goes
back, LB / RB page through the list or change the save slot.

**Terminal, System, GPS:** B or the Xbox button exits.

### Apps that only know the knob

The daemon also presents the first pad as the iDrive controller, so an app written
for the knob works with a pad without changes:

| Pad | Arrives as |
|---|---|
| D-pad / left stick | tilt (one direction at a time) |
| LB / RB | rotate, repeating while held |
| A | knob press |
| B, X, Y | OPTION, MEDIA, COM |
| Menu, View | NAV, TEL |
| Xbox button | BACK |

DOOM, Quake 3 and the NES, SNES and Genesis tiles read the pad directly instead (next
section), which is what gives them diagonals, analogue sticks and triggers.

## Using the pad from your own app

`shared/include/evo_pad.h` describes `/dev/shmem/evo_pad`, a small block the daemon
keeps up to date with the full state of up to four pads:

```c
#include "evo_pad.h"

const struct evo_pad *pads = evo_pad_open();        /* NULL if the daemon never ran */
...
unsigned b = evo_pad_buttons(pads, 0);              /* 0 if no pad / daemon gone   */
if (b & EVO_PAD_A) jump();
int x = pads->pad[0].lx;                            /* -32768..32767, right positive */
```

Events the daemon mirrors into the knob ring carry `EVO_PAD_FEED_MARK` in `b[3]`; an
app that reads the pad block skips those so that a press is not seen twice.

## Options

```
evo_padd [-v|-vv|-vvv] [-f firmware] [-p] [-n] [-w] [-t seconds]
  -v    trace controller input      -vv  protocol detail      -vvv  every USB transfer
  -f    firmware file (default /fs/sda1/evo/xbox/FW_ACC_00U.bin)
  -p    start in pairing mode (as if the adapter's button had been pressed)
  -n    publish the pad state only; no knob events
  -w    wait for the adapter to be plugged in (boot use); exits when it is unplugged
  -t    exit after this many seconds (testing)
SIGUSR1 = pairing mode.  SIGTERM = controllers off, radio off, exit.
```

## How it is put together

```
evo_padd.c   main: options, signals, the two receive threads, shutdown
usbq.c       USB transport on libusbdi
mt76.c       the radio: firmware upload, register and channel setup, beacon, clients
mt76_regs.h  register constants (from xow / OpenWrt's mt76)
dongle.c     receive path: adapter events and 802.11 frames -> controllers
gip.c        GIP, the controller protocol: handshake, acknowledgements, input reports
padout.c     output: /dev/shmem/evo_pad and the knob ring
```

The sequence of register writes, radio commands and frames is xow's. What differs is
the form: xow overlays packed bitfield structs on packet bytes; this port packs and
parses every header byte by byte, because the head unit's ARM faults on unaligned
access.

### Things specific to this unit's USB stack (QNX 6.5 `io-usb`)

These cost the most time in the port and are worth knowing for any `libusbdi` work on
the unit:

- **Use the completion callback for bulk transfers.** With `usbd_io(..., NULL, ...)`
  the status and length read back afterwards can still be those of the *previous*
  transfer: reads arrive cut short or twice, writes report "in progress". The values
  handed to the callback are right.
- **Never time out or abort a pending read.** A read that times out leaves its
  transfer pending inside `io-usb`; its late completion then lands on a reused or
  freed request and crashes `libusbdi`'s event thread. `usbd_abort_pipe()` can block
  for ever. The receive threads therefore wait on one transfer for as long as it
  takes, and the daemon ends by switching the radio off and leaving the process;
  `io-usb` reclaims the pipes.
- **Match the device before attaching.** `usbd_attach()` is exclusive. The insertion
  callback decides from the ids `io-usb` passes it and attaches only to the adapter,
  so a resident daemon never takes a USB stick or a phone away from its own driver.
- `usbd_endpoint_descriptor()` looks an endpoint up by its **address**, the control
  pipe is opened by passing the **device descriptor** to `usbd_open_pipe()`, and
  transfer buffers come from `usbd_alloc()`.
- Without a USB port reset (xow does one, this port does not), a firmware reset is
  acknowledged with state `0` instead of xow's `0x80000000`; both are accepted.

## Limits

- The knob ring has one writer by design (the tee inside `most`). The daemon is a
  second writer: fine while the knob is idle, but a knob event that lands in the same
  instant as a pad event can be lost. Apps that read the pad block are not affected.
- The adapter draws up to 500 mA and transmits on 2.4 and 5 GHz next to the unit's own
  radios.

## Licence and credits

GPL-2.0-or-later (see `COPYING`). Ported from **xow** by Severin von Wnuck (Medusalix),
with constants from OpenWrt's **mt76** driver (ISC, notice in `mt76_regs.h`) and
details checked against **xone**.

Two small tools live here too: `evo_usbprobe` (`make probe`) lists USB devices and
endpoints on the unit, and `evo_padfake` (`make fake`) is a pretend gamepad that
writes the pad-state block from a script, so an app's gamepad handling can be tested
without a controller in hand.

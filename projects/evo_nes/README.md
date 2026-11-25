# evo_nes

An **NES emulator** tile — Nofrendo running on the head unit, driven by the iDrive
knob (or a USB keyboard), with sound out the car speakers. Bring your own ROMs.

## How it works
- **Core:** [`nofrendo/`](nofrendo) is the Nofrendo NES core (LGPL, vendored from the
  esp32-nesemu mirror since the original site is gone — see `nofrendo/VENDORED.md`
  and `QNX_PATCHES.md`). It has a real APU and 40 mappers.
- **O/S port:** [`evo_nes_osd.c`](evo_nes_osd.c) implements nofrendo's `osd.h` +
  `viddriver_t` seam: a 60 Hz frame clock, the 8-bit video buffer hand-off,
  per-frame joypad events, and `.sav` / save-state paths beside the ROM.
- **Frontend:** [`evo_nes.c`](evo_nes.c) is the launcher-app side: an
  [`evo_graphics_lib`](../../shared/evo_graphics_lib) full-screen context (OEM HMI
  hidden), the NES frame palette-converted and **2× integer-scaled** (256×224 →
  512×448) centred on the 1440×540 panel, the iDrive feed-ring reader, a USB
  keyboard reader, QSA audio (DOOM's recipe + the `audioconn -b 0xb8` speaker
  route), and a knob-driven **ROM picker**.
- The two halves meet only in [`evo_nes_port.h`](evo_nes_port.h), so nofrendo's
  headers never mix with the system/graphics headers.

## Controls
| iDrive | USB keyboard | Xbox pad ([evo_padd](../evo_padd/README.md)) | NES |
|---|---|---|---|
| tilt ↑↓←→ (hold) | arrows | D-pad or left stick, with diagonals | D-pad |
| press the knob | X / Space | A or B | A |
| OPTION or MEDIA | Z | X or Y | B |
| NAV | Enter | Menu | Start |
| TEL | Right Shift / Tab | View | Select |
| rotate | ↑↓ | LB / RB, or D-pad | choose (ROM picker) |
| **BACK twice** within 3 s | Esc Esc | Xbox button twice | quit to the launcher (once leaves the picker) |

A second pad is player 2. The emulator reads the pad state directly
(`shared/include/evo_pad.h`), so every button can be held at once.

MENU is deliberately untouched (reserved for the Android video switch).

## ROMs, saves, audio
- Put `.nes` files in **`/fs/sda1/evo/nes/`** (the USB stick). No game data ships
  here; with no ROMs present the picker just says so.
- Battery saves (`game.sav`) and save states land **beside the ROM**.
- Audio gain: `echo 400 > /fs/sda1/evo/nes_gain` (percent; default 300 because the
  unit attenuates the 0xb8 browser source). No rebuild needed.

## Build
```sh
make -C projects/evo_nes          # -> bin/evo_nes (ARM QNX; whole thing at -O2)
python3 projects/evo_nes/make_icon.py   # -> nes.rgba / nes.png (original controller glyph)
```
`EXTRA_LIBS := -l:libasound.so.2` pulls in QSA; the vendored core is compiled
straight into the binary (no shared objects).

## Run
Launcher tile **NES** (`order 16`), which runs [`target/nes_launch.sh`](target/nes_launch.sh):
it sets the audio library path (the HARMAN `libasound` must come first), starts
`devu-kbd` if idle, and restores the HMI layer if the emulator crashes.
`./evo_nes /fs/sda1/evo/nes/game.nes` skips the picker. Logs:
`/dev/shmem/nes_launch_stdout.log`.

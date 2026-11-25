# evo_padd — Xbox wireless controller support (plan)

Goal: an Xbox One / Series controller, connected through the **Xbox One Wireless
Adapter** plugged into the head unit, drives the homebrew launcher and its apps
(NES first, then DOOM / Quake 3).

## What the hardware is (measured on the unit, 2026-11-23)

- Receiver = Xbox One Wireless Adapter, USB `045e:02e6` (the original, larger model).
  It is not a HID device: it is a MediaTek **MT76** Wi-Fi radio. ASIC version register
  reads `0x76320044`, MAC version `0x76623000`.
- One vendor-class interface; bulk OUT `0x04`–`0x09`, bulk IN `0x84`, `0x85`.
- It does nothing until the host uploads firmware (`FW_ACC_00U.bin`, 70,620 bytes,
  sha256 `48084d9f…eabdf66`) and then runs a small 802.11 access point that the
  controllers associate with. Controller traffic is GIP (Game Input Protocol) inside
  802.11 QoS data frames.
- The head unit has no HID class driver and no Wi-Fi driver for this chip, so all of
  that has to live in our own process, on raw `libusbdi`.

Reference implementation: **xow** (userspace, libusb, C++, GPL-2.0-or-later) — the
same job on Linux. `xone` (kernel, same author) is the newer reference for controller
quirks.

## M0 — done: the USB transport works on this unit

`evo_usbprobe -c` (this directory) proved everything xow needs from libusb exists in
QNX `libusbdi` here:

| need | result |
|---|---|
| enumerate + attach | `usbd_connect` (wildcard) → insertion callback → `usbd_attach` |
| vendor control transfers on EP0 | 4 register reads returned real values |
| bulk pipes | `0x84`, `0x85`, `0x04`, `0x05` all open |
| firmware | obtained from Microsoft's driver cab, hash matches xone's |

libusbdi notes worth keeping: `usbd_endpoint_descriptor()` looks an endpoint up by its
**address**, not an index; the control pipe is opened by passing the **device
descriptor** to `usbd_open_pipe()`; transfer buffers come from `usbd_alloc()`.

## Structure

```
evo_padd.c     daemon main: attach, bring-up, threads, shutdown
usbq.c/.h      libusbdi transport: vendor control transfers, bulk in/out via completion callbacks
mt76.c/.h      chip driver, ported from xow dongle/mt76.cpp (2,095 lines C++)
mt76_regs.h    register constants, generated from xow's defines
dongle.c/.h    RX demux: MCU events, 802.11 management frames, QoS data -> GIP
gip.c/.h       GIP: frame header, ACK, announce/power/LED handshake, input report
padout.c       output: pad-state shared memory + launcher feed-ring events
evo_padfake.c  test tool: a pretend pad written into the shared block
```

Port rules (C99, gcc 4.4.2, strict-alignment ARM): no bitfield structs or pointer
casts onto packet bytes — every header is packed and parsed with explicit
little-endian byte helpers; pthreads instead of `std::thread`/`std::function`; one
mutex around EP0 and one around TX.

## Milestones — status 2026-11-23

All checked on the bench unit with a model 1708 and a model 1537 controller.

1. **Firmware upload — done.** 12 ms; every chunk confirmed by the chip.
2. **Radio bring-up — done.** 0.2 s from start to "radio up"; the adapter's button
   arrives as an event.
3. **Pairing + association — done.** Association answered in 0–3 ms; the timing risk
   below did not materialise.
4. **GIP — done.** Announce, power, LED, serial, battery, input reports; both pads at
   once.
5. **Output — done.** `/dev/shmem/evo_pad` plus knob-ring events. The launcher, DOOM,
   Quake 3, the NES tile and the small apps were each driven end to end with
   `evo_padfake` and screenshots (and the launcher + DOOM with the real pads).
6. **Productise — mostly done.** README, `fetch_firmware.sh`, GPL licence, `-w` wait
   mode, 30 s pairing timeout, attach restricted to the adapter, start from
   `evo_autostart.sh` (installed on the unit).
   *Checked 2026-11-24:* after a reset of the unit the boot script started the daemon
   by itself, 11 s after the HMI came up (firmware loaded, radio up, waiting for a
   controller).
   **Still to check:** a controller rejoining by itself after such a restart (needs
   one in hand), the same after the 12 V supply was cut, unplug / replug of the
   adapter, a long session, and the first-connection drop seen once (a controller
   disassociated 2.5 s after joining and needed re-pairing; not seen again in later
   runs). The daemon is in the binary release since 2026-11-25; the adapter's
   firmware is not (see the README).

What the port turned up about this unit's USB stack is in the README ("Things specific
to this unit's USB stack").

## Risks

- **Association timing.** The controller expects the association response within
  milliseconds. xow manages this from userspace on Linux; QNX message-passing USB adds
  latency per transfer. *Resolved: measured 0–3 ms.*
- **Controller model.** xow is tested with models 1537, 1697 and 1708; the Series X|S
  pad (1914) is listed as untested there and is handled in xone. If the pad is a 1914,
  expect to borrow from xone.
- **Feed ring has one writer by design.** A pad daemon is a second producer (as
  `evo_feedpush` already is). Fine while the knob is idle; simultaneous knob + pad input
  can drop an event. The pad-state shared memory avoids the problem for apps that read it.
- **Power and radio.** The adapter draws up to 500 mA from the unit's port (it
  enumerates fine today) and transmits on 2.4/5 GHz next to the unit's own Wi-Fi/BT.
- **Firmware is Microsoft's.** It is fetched from Windows Update by the user and never
  committed or shipped; without it the daemon cannot work.

Out of scope for now: rumble, headset audio, Elite paddles, more than one adapter.

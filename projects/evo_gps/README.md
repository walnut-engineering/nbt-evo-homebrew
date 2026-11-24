# evo_gps

A full-screen **GPS / position** tile — live position read from the nav engine's
positioning service over DSI.

## How it works
Talks to the NBT nav positioning service over the QNX-native DSI transport (the
same path as the toolkit's [`evo_dsiqnx`](../nbt-dsi-toolkit) / `evo_navctl`):

```
SBAttachInterface("PosReal.DNAVIPos", v33) -> subscribe REQUEST_NOTIFY
NOTIFY (cmd=DataResp) carries the current position;
  attr 0x0: field[3] = longitude, field[4] = latitude  (NDS units = deg * 2^32/360)
```

Rendered with `evo_graphics_lib` + 8×8 font; iDrive **BACK** exits (no keyboard).
The raw NOTIFY arrays are shown at the bottom so the remaining fields
(fix/sats/speed/heading/HDOP) can be identified against a real fix. `PosReal` is
real GPS; `PosDemo` (via the toolkit's `evo_navctl`) can spoof it for a bench
demo with no antenna.

## Build
```sh
make -C projects/evo_gps          # -> bin/evo_gps (ARM QNX)
```
`USE_DSI=1` links the DSI service-broker from
[`nbt-dsi-toolkit`](../nbt-dsi-toolkit); its runtime libraries (`dsi_sdk/lib/`)
are proprietary and **not shipped** — copy them off a unit you own (see the
toolkit's [`NOTICE`](../nbt-dsi-toolkit/NOTICE)).

## Run
Launcher tile **GPS** (`order 40`, icon `gps.rgba`).

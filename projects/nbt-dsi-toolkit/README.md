# NBT DSI Toolkit

A small toolkit for talking to the **DSI / MoCCA** service bus inside a **BMW NBT / NBT EVO**
head unit (QNX 6.5.0). It lets you resolve the unit's internal services and **subscribe to their
notifications from your own program** — read-only and non-invasively — the same way the stock HMI does.

The centerpiece is **`evo_dsitcp`**, a from-scratch **DSI MoCCA-over-TCP client**. As far as we know
there's no public implementation of this transport; it was reverse-engineered from the wire and the
open-source DSIHarman headers. Everything here was built for a hobby project (running DOOM with the
iDrive controller), but the DSI client is generic and is the foundation for anything that needs to read
the car's internal state — telemetry, fault memory, inputs, etc.

> ⚠️ **Read-only by design.** These tools only *attach* and *subscribe*. They never register a service,
> never send mutating requests, and the unit multicasts notifications per-client, so a subscriber does
> not disturb the HMI. Don't add write paths on a vehicle bus without knowing exactly what you're doing.

---

## What's in here

```
src/        DSI client tools (C, cross-compiled for QNX armle-v7)
  evo_dsitcp.c   MoCCA-over-TCP subscriber: attach a service, subscribe, dump notifications
  evo_sbtcp.c    resolve ONE service -> its TCP endpoint (ip:port) + serverID/clientID
  evo_sbmap.c    map ALL services -> serverID -> TCP endpoint (dumps the whole bus)
  evo_sblist.c   list every registered interface name/version (SBGetInterfaceList)
  evo_dsi.c      resolve a service's QNX-native channel (nid/pid/chid) [legacy transport]
  evo_dsiqnx.c   native (QNX message-passing) subscriber: same DSI PDUs over a QNX channel
  evo_dsicall.c  native method CALL: invoke a DSI method (updateId) and dump the response
scripts/    host-side pcap decoders (Python 3, no deps)
  dsi_scan.py    find + summarize every DSI message in a pcap
  dsi_tcp.py     reconstruct a connection's ConnReq/subscribe/notification timeline
  find_knob.py   rank notification streams by "which values changed" (input discovery)
captures/   boot_handshake.pcap (a full, real handshake) + service_map.txt (a dumped bus)
dsi_sdk/    DSI SDK headers (open source) + the unit's runtime libs (SEE NOTICE — do not redistribute)
Makefile    standalone cross build
PROTOCOL.md the wire protocol, decoded
```

## Quick start

1. **Build** (needs the **QNX 6.5.0 SDP** — its own `gcc 4.4.2` plus the armle-v7 target
   libs/headers; this is the unit's real toolchain/ABI. See the Makefile head):
   ```sh
   make                              # uses the QNX 6.5 SDP toolchain (shared/sdp.mk)
   make QNX_SDP=/path/to/qnx650      # if your SDP lives elsewhere
   ```
   The default SDP location is `$HOME/qnx650_sdp/qnx650`; override `QNX_SDP`
   to point at your install. (The retired Linaro 4.7.3 cross-gcc is no longer used —
   `gcc 4.4.2` does not accept `-mno-unaligned-access`, which armv7-a handles in hardware.)
2. **Copy** the binaries to the unit (SSH/scp; the stock sshd is independent of the car stack) and run
   with the DSI libraries on the path:
   ```sh
   export LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib
   ```
3. **See what's on the bus:**
   ```sh
   ./evo_sbmap                      # every service -> serverID -> ip:port
   ./evo_sbtcp VehicleInformation.DSYSNBTVehicleInfo   # one service's endpoint + ids
   ```
4. **Subscribe and watch notifications** (attr range `0..0x30`, run 8 s):
   ```sh
   ./evo_dsitcp VehicleInformation.DSYSNBTVehicleInfo 0 0 0x30 8
   ```
   You'll see the forward connect, the server's reverse connection back to you, then a stream of
   `DataResp` notifications: `sid=<serverID> rid=0xC00000xx rt=0x0202 n=<count> [values...]`.

## How it works (short version — full detail in [PROTOCOL.md](PROTOCOL.md))

DSI between the NBT's two SoCs (OMAP ⇄ Jacinto) runs as **MoCCA over TCP** on the internal ethernet
(`veom0`, 160.48.199.x), **bidirectional**:

```
  servicebroker ── SBAttachInterfaceTCP(name) ─▶ server ip:port + serverID + clientID
  you ───────────── TCP connect ─────────────▶ Jacinto master port (e.g. .253:65534)
      └── ConnReq (advertises YOUR ip:port) + REQUEST_NOTIFY per attribute ──▶
  Jacinto ── opens a REVERSE TCP connection back to the ip:port you advertised ──▶ you
      ◀────────────── DataResp notifications stream over the reverse connection ──┘
```

So a client must **listen on a local port and advertise it**; the server dials back to deliver
notifications. `evo_dsitcp` does all of this. Service IDs and the master port are assigned per-boot, so
always discover them at runtime via `SBAttachInterfaceTCP` (the toolkit does).

## Discovering an input/attribute

Notifications are keyed by `requestID = 0xC0000000 | attribute_id`. To find which service/attribute
carries a given signal (a button, a sensor, the iDrive knob…), capture the wire while you exercise it,
then rank the streams by what changed:

```sh
# on the unit: tcpdump -i veom0 -s0 -w spin.pcap host <jacinto-ip>   (while you operate the input)
python3 scripts/find_knob.py spin.pcap           # the changing (serverID, requestID) stands out
grep <serverID> captures/service_map.txt         # -> the interface name to attach
```

## Status / notes

- Verified working on an NBT EVO (OMAP5432 + DRA6xx Jacinto, QNX 6.5.0). The `evo_dsitcp` client
  attaches, connects, subscribes, accepts the reverse connection, and receives live notifications.
- The QNX-native transport (`SBAttachInterface` + `MsgSend` ConnectRequest) is **not** how these clients
  talk — that path exists (`evo_dsi` resolves the channel) but the stock clients use MoCCA-over-TCP.
- `dsi_sdk/lib/*.so` are **proprietary** unit libraries. They are **not** redistributable — pull them
  from your own unit. See [NOTICE](NOTICE). The headers under `dsi_sdk/include` are the open-source
  DSIHarman DSI headers.

## Credits

Reverse-engineered from the wire + the open-source [DSIHarman](https://github.com/) DSI sources.
Built as part of a personal NBT modding project. Contributions / corrections welcome.

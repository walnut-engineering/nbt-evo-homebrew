# NBT EVO — iDrive controller → DOOM input: architecture & status

Reference for the controller-input work on the BMW NBT EVO head unit. Updated 2026-11-02.

## Hardware / node topology

- **OMAP** (OMAP5432, hostname `hu-omap`, QNX 6.5.0 `procnto-smp-instr`): runs the HMI
  (`HmiMain3D`), DOOM, most apps. Reached over **USB-ethernet `dwc0` = 169.254.199.119** (our SSH link).
- **Jacinto** (DRA6xx, hostname `hu-jacinto`): audio/tuner/MOST + the **car master**
  `HUJacintoStage1`. Reached from the OMAP via qnet: `on -f hu-jacinto <cmd>`.
- **V850 / "IOC"**: companion MCU, the **CAN gateway**. Talks to the Jacinto over SPI
  (`dev-spi-dra6xx` → `io-ipc` `/dev/ipc/ipc2..19`). `v850commander` (on the Jacinto) controls it.
- **Internal ethernet** `veom0`: OMAP `160.48.199.254` ↔ Jacinto `160.48.199.253` (/30), via the
  `ksz8864` switch. DSI-over-TCP between nodes rides this. `tcpdump` is on the unit (`/bin/tcpdump`).
- The two servicebrokers **federate** (`servicebroker -m/net/hu-omap/srv/servicebroker -q160.48.199.254:3746`),
  so an OMAP DSI client resolves Jacinto-provided services by name.

## The iDrive controller path

```
iDrive knob → CAN → V850/IOC → SPI → Jacinto io-ipc(4110) → HUJacintoStage1(4108)
   → a DSI service (TBD) → (DSI MoCCA-over-TCP, veom0) → HmiMain3D(258100) on OMAP
```

- **Consumer** = `HmiMain3D` (pid 258100), proven by a spin-vs-idle CPU diff: **+4.06 s** on a spin
  (see `re/scripts/cmp_cpu.py`, `re/captures/o?.txt j?.txt`).
- **Transport = DSI MoCCA-over-TCP on veom0 (SOLVED, 2026-11-03).** Clients do NOT use qnet-native
  MsgSend — they open TCP to the Jacinto master port **.253:65534 (0xFFFE)**, bidirectional: forward
  conn (client→.253:65534) carries ConnReq + REQUEST_NOTIFY; the server opens a REVERSE conn back to
  the client's advertised (ip,port) and pushes cmd=8 notifications over it. Full wire spec + the
  working subscriber are below. Golden capture: `re/captures/boot_handshake.pcap`.
- **The exact controller service is NOT yet identified.** Earlier guesses were wrong: the service map
  (`re/captures/service_map.txt`, via `evo_sbmap`) shows `0x7a12d = VehicleInformation.DSYSNBTVehicleInfo`
  (telemetry, not the knob) and `EnvPeriphery.DSYSEnvPeriphery = 0x7a136` (v1.10, only ~5 boolean attrs,
  likely power/ignition flags). No service is obviously named for the controller. **Resolve with a spin
  test:** `tools/spin_test.sh` captures all Jacinto DSI while you operate the controller, then
  `re/scripts/find_knob.py` ranks streams by changing values to reveal the knob's (serverID, requestID).

**Approach (user, option A): a hand-written DSI MoCCA-over-TCP client** (`controller/evo_dsitcp.c`,
WORKING) that subscribes like the HMI, decodes the notification into rotary-delta + button bits, and
feeds the DOOM feed. Read-only, non-invasive (DSI multicasts per-client; the HMI is unaffected). SDK in
`dsi_sdk/`.

### DSI MoCCA-over-TCP wire protocol (decoded from `boot_handshake.pcap`)
- Resolve endpoint+IDs: `SBAttachInterfaceTCP(h,name,maj,min,&STCPConnectionInfo)` → server `ip:port`,
  TCP serverID, fresh clientID. (The qnet `SBAttachInterface` returns a *different* serverID per service.)
- 40-byte MessageHeader (LE): `int32 type=0x200; u16 protoMajor=4; u16 protoMinor=0; SPartyID serverID@8;
  SPartyID clientID@16; u32 cmd@24; u32 flags@28; i32 packetLength@32; u32 reserved@36`.
- **ConnReq** cmd=9, body=8B `{u32 ip (host-order, e.g. 0xa030c7fe=160.48.199.254), u32 port}` = the
  client's own advertised listen endpoint; server replies 8B `{serverip,port}` (not a DSI header).
- **Subscribe** cmd=7, body=16B EventInfo `{u32 ifVersion=0; u32 rtype=0x0101 REQUEST_NOTIFY; u32
  requestID=0xC0000000|attr; i32 seq=0}`.
- **Notification** cmd=8, EventInfo `{rtype=0x0202 OK / 0x0203 no-data, requestID, seq}` + int-array data.

### NOT the controller (ruled out)
- **`touchComDevice`** = the **capacitive touchscreen** bridge (io-ipc `ipc27`), NOT the iDrive knob.
  (User-confirmed. The B1 LD_PRELOAD interposer built on it still works and is reusable for a future
  Android-display touch-injection project.)
- **Passive io-ipc tap** (ipc25-28 on either node): dead — io-ipc delivers only to the primary reader;
  a 2nd reader sees nothing in normal operation.
- **MOST**: user confirmed the controller is not on the optical MOST ring (works with it unplugged).

## DSI SDK + client (moved to `../nbt-dsi-toolkit/`)

The DSI SDK (`dsi_sdk/`: MoCCA headers + the unit's `servicebroker`/`colibry` libs), the DSI client +
servicebroker tools, the pcap decoders, and the decoded wire protocol now live in the standalone
**`../nbt-dsi-toolkit/`** project (see its `README.md` / `PROTOCOL.md`). On the unit, DSI clients need
`LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib`.

## Tools (`controller/`, build with `make`)

> **The general DSI client + servicebroker tools moved to `../nbt-dsi-toolkit/`** (standalone, shareable,
> reusable by any NBT mod): `evo_dsitcp` (MoCCA-over-TCP subscriber), `evo_sbtcp`, `evo_sbmap`, `evo_sblist`,
> `evo_dsi`, the pcap decoders, the DSI SDK, and the protocol spec. The DSI client runs as its own process
> and feeds DOOM through the `/dev/shmem/evo_ctrl_feed` ring below, so the DOOM build no longer links DSI.
> The table below is the DOOM/touch-specific helpers that remain here.

| tool | kind | purpose |
|---|---|---|
| `evo_feedtap` | exe | read the `/dev/shmem/evo_ctrl_feed` ring and print events (DOOM feed consumer reference) |
| `evo_tee.so` | preload | LD_PRELOAD tee for touchComDevice: raw-logs + tees ipc reads to the feed (touch bridge) |
| `evo_tcd_shim` | exe | ELF shim that sets LD_PRELOAD and execs the real touchComDevice |
| `evo_ipctap`/`evo_ipctap2` | exe | passive io-ipc channel readers (now known ineffective — kept for reference) |
| `evo_ipcinject` | exe | io-ipc writer (proven not to drive the HMI) |
| `evo_ctrlmon`/`evo_ctrl_probe`/`evo_ctrl_inject.so` | — | earlier controller experiments |

`evo_feed.h` defines the SPSC ring (`/dev/shmem/evo_ctrl_feed`, 512 slots) shared with DOOM
(`source/i_input_evo.c`).

## Key constraints learned
- **A failed `LD_PRELOAD` is FATAL on this QNX** (`ldd:FATAL`, program won't run). Interposing a
  critical process by env-table LD_PRELOAD can brick it. BMW's own stock example: the OMAP
  `LD_PRELOAD_SCREEN_PATCH` Environment entry (`EnvName=LD_PRELOAD`) referenced via
  `<RequiresEnvironment>`. A fail-safe needs a shim that `dlopen`-tests first + a boot counter.
- `HUJacintoStage1` (Jacinto Process #0, `RESTART_ON_ERROR`/`STARTER_RECOVERY`) and `HmiMain3D` are
  `zipped` (inflated at boot by `ziploader`; the on-disk/boot path is transient) → not swappable,
  high blast radius. Prefer the DSI-client route over interposing them.
- Unit userland is minimal: `grep` has no `-a`/`-A`/`-B`; no `uniq`/`sort`/`od`/`strings`/`timeout`/
  `nohup`; `pidin fds` over ALL procs **hangs** (query per-pid). A working `awk` (gawk) and GNU `grep`
  (`ggrep`) were deployed to `/fs/sda1/evo`.
- No reboot command. `v850commander appreset` (run `on -f hu-jacinto /bin/v850commander appreset`)
  resets only the V850. `OnOffDSICommander appreset` hangs hand-run. **Physical power-cycle** is the
  reliable restart; the shim/tee/backups on `/fs/sda1/evo` + `/opt/car/bin` are persistent.

## Recovery
SSH (`sshd -i`, io-pkt) is independent of the car `starter`, so it survives a broken car stack.
Revert the touchComDevice shim: `mount -uw /fs/sda0 && cp /fs/sda1/evo/touchComDevice.orig
/opt/car/bin/touchComDevice && mount -ur /fs/sda0`, then power-cycle. Last resort: serial to the
Jacinto / re-image.

## Next step
The MoCCA-over-TCP subscriber (`../nbt-dsi-toolkit/src/evo_dsitcp.c`) is **working** (attaches, connects,
subscribes, receives notifications — read-only, non-invasive). Remaining:
1. **Identify the controller service (needs the bench).** Run `tools/spin_test.sh` while operating the
   iDrive knob/buttons; the toolkit's `scripts/find_knob.py` ranks the DSI streams and the knob's
   (serverID, requestID) stands out (changing values). `grep` that serverID in
   `../nbt-dsi-toolkit/captures/service_map.txt` for the interface name.
2. **Subscribe + decode.** `evo_dsitcp <that-service>`; map requestIDs → rotary-delta + button bits.
3. **Feed DOOM.** Push decoded events into `/dev/shmem/evo_ctrl_feed` (`evo_feed.h` ring) consumed by
   `source/i_input_evo.c`.

Earlier dead ends (do not retry): qnet-native `SBAttachInterface` + `MsgSend` ConnectRequest EINVALs
(the real clients use TCP, not qnet MsgSend); `0x7a12d` is VehicleInfo not the knob; `EnvPeriphery`
(`0x7a136`) is power/ignition flags.

## Connecting
Deploy/run over ssh with the repo helpers: `. ../../scripts/evo_env.sh`, then `evo_ssh '<cmd>'` and `evo_push <local> <remote>`.
Credentials come from `scripts/evo_env.local.sh` (gitignored; template `scripts/evo_env.local.sh.example`).

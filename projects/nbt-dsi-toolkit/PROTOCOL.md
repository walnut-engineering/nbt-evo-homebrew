# DSI MoCCA-over-TCP — wire protocol (decoded)

Reverse-engineered from a real NBT EVO handshake (`captures/boot_handshake.pcap`) and cross-checked
against the open-source DSIHarman headers (`dsi_sdk/include/dsi/`). All multi-byte fields are
**little-endian**. Offsets are in bytes.

## Topology

The NBT has two SoCs on an internal switched ethernet (`veom0`): the **OMAP** (runs the HMI, 160.48.199.254)
and the **Jacinto** (car master `HUJacintoStage1`, 160.48.199.253). A federated **servicebroker** on each
side provides name→endpoint resolution across both. DSI services are addressed by an **`SPartyID`**:

```
SPartyID (8B, 8-aligned):  uint32 localID;  uint32 extendedID;
```
`extendedID` is the DSI node (seen: `0x3e8`=1000 OMAP, `0x3e9`=1001 Jacinto). `localID` is the per-boot
interface/party id (e.g. a service is `0x0007a1xx`; a client attachment is `0x00018xxx`). **These are
assigned per boot — never hard-code them; resolve at runtime.**

## Step 0 — resolve the endpoint (servicebroker)

`SBAttachInterfaceTCP(handle, "Name.DSYSRole", major, minor, &STCPConnectionInfo)` returns:

```
STCPConnectionInfo {
  SFNDInterfaceVersion ifVersion;              // actual version
  STCPSocketInfo socket { uint32 ipaddress;    // server IP, HOST byte order
                          uint16 port; }        // server TCP port, HOST byte order
  SPartyID serverID;                            // the TCP-transport server party
  SPartyID clientID;                            // a fresh client party allocated to you
}
```

Notes:
- The server port is a per-boot **master port** shared by all of that node's services (observed `65534`
  = `0xFFFE` on the Jacinto). Many services multiplex over one connection to it.
- The **qnet-native** `SBAttachInterface` returns a *different* serverID for the same service (different
  transport). Use the TCP call for the TCP transport.

## Step 1 — the two connections (bidirectional)

```
FORWARD:  you(OMAP ephemeral) ── TCP connect ──▶ server socket (Jacinto master port)
REVERSE:  Jacinto(ephemeral)  ── TCP connect ──▶ the (ip,port) you advertised in the ConnReq
```

Requests/subscribes go out on FORWARD. **Notifications come back on REVERSE.** So your client must
`bind()`+`listen()` on a local port *before* it sends the ConnReq, and `accept()` the server's dial-back.

## Step 2 — message framing

Every DSI message is a 40-byte header + optional body. A connection may begin with an 8-byte transport
reply (see below) that is *not* a DSI header; detect it because its first 4 bytes != `0x200`.

```
MessageHeader (40B):
  off0   int32   type           = 0x00000200   (DSI_MESSAGE_MAGIC)
  off4   uint16  protoMajor     = 4
  off6   uint16  protoMinor     = 0
  off8   SPartyID serverID       (8B)
  off16  SPartyID clientID       (8B)
  off24  uint32  cmd             (see below)
  off28  uint32  flags           = 0
  off32  int32   packetLength    (body length in bytes)
  off36  uint32  reserved        = 0
```

Commands (`cmd`): `7`=DataRequest, `8`=DataResponse, `9`=ConnectRequest, `10`=DisconnectRequest,
`11`=ConnectResponse.

## Step 3 — ConnectRequest (FORWARD, cmd=9)

Header with `cmd=9`, `packetLength=8`, then an 8-byte body = **your own advertised endpoint**:

```
ConnReq body (8B):  uint32 ip;   uint32 port;     // both HOST byte order
  e.g. ip 160.48.199.254 -> 0xA030C7FE -> bytes  fe c7 30 a0
       port 65481         -> 0x0000FFC9 -> bytes  c9 ff 00 00
```
Send one ConnectRequest per service you want on that connection (serverID differs, clientID differs,
body/endpoint identical). The server answers on FORWARD with an **8-byte transport reply** = its own
`{ip,port}` (not a DSI header), e.g. `fd c7 30 a0 fe ff 00 00` = 160.48.199.253 : 65534. Shortly after,
it opens the REVERSE connection to the endpoint you advertised.

## Step 4 — subscribe (FORWARD, cmd=7, REQUEST_NOTIFY)

Header `cmd=7`, `packetLength=16`, body = `EventInfo`:

```
EventInfo (16B):
  off0  uint32 ifVersion   = 0
  off4  uint32 rtype       = 0x0101   (REQUEST_NOTIFY;  0x0100 = REQUEST one-shot)
  off8  uint32 requestID   = 0xC0000000 | attribute_id
  off12 int32  seq         = 0
```
Subscribe to a range of `attribute_id`s to discover which carry data.

## Step 5 — notifications (REVERSE, cmd=8, DataResponse)

Header `cmd=8`, body = `EventInfo` (16B) + payload:

```
EventInfo: rtype = 0x0202 RESULT_DATA_OK (has data) | 0x0203 (no current data / pending)
           requestID = the same 0xC00000xx you subscribed
           seq       = sequence
payload:   an array of int32 values (attribute-specific; e.g. a boolean flag, a counter, a struct)
```

Example (EnvPeriphery): `rid=0xC0000016 rt=0x0202 -> [1]`.

## Gotchas

- TCP is a stream: a `recv()` may split or coalesce messages. Reassemble by `packetLength`
  (`evo_dsitcp.c` does this; also handles the leading 8-byte transport reply).
- IDs and the master port change every boot — always go through `SBAttachInterfaceTCP`.
- Services ending in `_tcp` in the broker list are the TCP-export aliases; attach the base name.

---

# DSI native (QNX message-passing) transport

The TCP transport above is what the two SoCs use to talk to **each other** (OMAP ⇄ Jacinto).
Services that live on the **same** SoC as your client are not reached over TCP — they are plain
**QNX channels**, and the same DSI PDUs ride QNX message passing instead of a socket. `evo_dsiqnx`
(attribute subscribe) and `evo_dsicall` (method call) speak this transport.

## Resolve the channel

`SBAttachInterface(h, "Component.Interface", major, minor, &ci)` fills `ci.channel` with the
server's `{nid, pid, chid}` plus a `serverID` and a fresh `clientID`. (This is the colibry
servicebroker call — the same one `evo_dsi` uses; no TCP variant needed.)

## Connect (cmd=9)

```
ChannelCreate()                         -> our receive chid (notifications land here)
ConnectAttach(nid, pid, chid)           -> coid to the server
MsgSend(coid, connreq, 40+16, reply, 16)
```

The 40-byte MessageHeader is **identical to the TCP one with ONE difference**: `header[0]` (the
`type` field) is **0 for native, not 0x200**. The `0x200` magic is a TCP-stream framing marker
only; sending it on a QNX channel gets the ConnReq rejected with **EINVAL** (this was the single
thing that blocked the first native attempts).

The 16-byte ConnReq body is **four uint32** — the client's reverse endpoint as QNX channels, not
an ip/port:

```
[0] chid1   (our receive channel; a minimal client uses the same chid for both)
[1] chid2   (second channel — the server uses a chunk/priority pair)
[2] pid     (our getpid())
[3] nid     (0 for a local node)
```

The server `MsgReply`s **16 bytes**: `[server_dchid1, server_dchid2, server_pid, nid]`. The client
then `ConnectAttach`es to the two server data channels and issues DataRequests there; notifications
arrive back on our `chid` via `MsgReceive`.

## Subscribe to an attribute (cmd=7)

Same as TCP: `EventInfo{ ifVersion, rtype=REQUEST_NOTIFY(0x0101), requestID = 0xC0000000|attr, seq }`.
Notifications come back as cmd=8 DataResponses (`rtype=0x0202` with data, `0x0203` = no data yet).

## Call a method (cmd=7, one-shot request)

Methods use a **different requestID range** from attributes. Per the DSI spec the request-id space is:

```
0x00000000 .. 0x7FFFFFFF   REQUEST   (a method's updateId, used as-is)
0x80000000 .. 0xBFFFFFFF   RESPONSE  (a reply carries 0x80000000 | updateId)
0xC0000000 .. 0xFFFFFFFF   ATTRIBUTE (an attribute subscribe, 0xC0000000 | attr)
```

So to invoke a method: DataRequest (cmd=7) with `EventInfo{ rtype=REQUEST(0x0100),
requestID = <plain updateId> }` and `body = the marshaled args`. The async response arrives on the
reverse channel as cmd=8 with `requestID = 0x80000000 | updateId` and `rtype` in the 0x0200 family
(`0x0200` success, `0x0201` carries an error code).

**Worked example (verified):** `WPRing.DMMWavePlayer` v1, `executeCommand = updateId 0`,
`body = [int32 command]` (STOP=0, PLAY=1, ABORT=2). `evo_dsicall WPRing.DMMWavePlayer 1 0 1`
plays; an illegal command returns `rtype=0x0201` with error code 2.

## Gotchas (native)

- `header[0]` **must be 0**, not `0x200` (see above). Everything else in the 40-byte header matches TCP.
- The body is chunked at `0xFD8` bytes by the framework's send path; small PDUs are unaffected.
- Many "accept external audio" services (OE streaming, WavePlayer sample tables) connect and respond
  fine but produce no audio on a **bench** because their upstream subsystem (online entertainment,
  telephony, media library) is OFF — the DSI path is correct, the source is context-gated.

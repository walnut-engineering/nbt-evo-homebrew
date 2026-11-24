# iDrive controller → DOOM input plumbing (SOLVED 2026-11-06)

Goal: drive DOOM with the OEM iDrive controller — **rotate → turn**, **tilt forward/back → move**,
**tilt left/right → strafe**, **push the knob → fire**, **hold MENU 1 s → quit DOOM** — while the
BMW HMI is **muted** (it must not navigate underneath).

## The real data path
The controller is CAN on the car side. The V850/IOC turns it into MOST-style telegrams
(FBlock 0xE5) and delivers them to the OMAP over the SPI io-ipc link, channel **/dev/ipc/ipc25**,
whose single reader is the MOST subsystem client **`/opt/sys/bin/most`** (starter.cfg #105-107,
env `V850_MOSTSUBNET_OMAP_DIRECT_REGISTRATION=1`: the V850 is a MOST *subnet*). `most` hands the
events to the HMI. Nothing crosses veom0/DSI/qnet for the knob, and the physical MOST ring is not
involved (works with the ring unplugged). Earlier theories (touchComDevice, DSI `EnvPeriphery`,
Jacinto io-ipc, passive ipc25 tap) are dead: see the memory file `evo-idrive-controller`.

```
iDrive ZBE ─CAN→ V850/IOC ─SPI io-ipc ch25→ [most.real + evo_tee.so] ──→ HMI (muted while grabbed)
                                                       │ e5_scan (evo_e5.h)
                                                       ▼
                                      /dev/shmem/evo_ctrl_feed  (SPSC ring, evo_feed.h v3)
                                                       │
                                                       ▼
                                   DOOM i_input_evo.c controller_thread → turn/move/strafe/fire
```

## Pieces (all in this directory unless noted)
| piece | what |
|---|---|
| `evo_most_shim.c` | installed as `/opt/sys/bin/most` (real binary renamed `most.real`). Fail-safe exec shim: dlopen-tests the tee, honours kill switch `/fs/sda1/evo/most_tee.off`, runs clean if anything is off. `--evo-check` self-test. |
| `evo_tee.c` → `/fs/sda1/evo/evo_tee_most.so` | LD_PRELOAD in `most`: hooks open/read/readcond on `/dev/ipc/*`, decodes telegrams into the ring, applies the **grab mask**, raw-logs to `/dev/shmem/evo_tee_most.log` (re-arm with `touch /dev/shmem/evo_tee_most.rearm`). |
| `evo_e5.h` | the protocol: `e5_scan` (telegram → events) and `e5_mask` (neutralise for the HMI, in place). |
| `evo_feed.h` | ring schema v3: events + `grab_until` (consumer-refreshed deadline, TTL 2 s) + `tee_alive`. DOOM carries an inline copy (keep in sync). |
| `evo_feedtap.c` | `evo_feedtap <secs> [grab]`: print live events; with `grab` holds the HMI mute (test without DOOM). |
| `e5_test.c` | host unit test: replays a tee raw log through `e5_scan`/`e5_mask` (`gcc -o e5_test e5_test.c`). |
| `/fs/sda1/evo/most_tee_install.sh` | on-unit install / `undo` (rename-based; the running binary is text-busy). |

## Telegram format (payload after the 40-byte io-ipc header; several per read())
```
e5 01 <FktHi> 0c  0c 02 00  e5 01 07 00 00 00  01 1d <seq> d0 d1 d2 d3
FktHi 10  ROTARY : d0d1 = 16-bit absolute position, centre 0x8000, +1 per detent clockwise
FktHi 20  BUTTON : d0 = 01 down / 00 up, d1 = tilt direction (01 forward 02 right 04 back 08 left, bench-confirmed; 00 on release; d0=02 after ~2 s = long hold)
                   d2d3 = id: 1e01 centre push, 1d01 joystick tilt,
                   00xx ring buttons bitmask 01 MENU 02 BACK 04 OPTION 08 MEDIA 10 NAV 20 TEL 40 COM
e5 0f 06 0c ...   controller heartbeat every 2 s;  FktHi 30 = misc counter;  15 02 01 04 ... = 100 ms poll
```
`<seq>` is a per-message counter (the 11-01 "detent counter" reading was wrong).

## The grab (HMI mute)
While `feed->grab_until` is in the future, the tee rewrites every controller telegram **before `most`
parses it**: the rotary position the HMI sees is frozen through a running offset (continuous when the
grab ends, no jump), presses become a neutral "id 0000 up", and a release is only passed if the HMI
saw the matching press. DOOM refreshes the deadline every 400 ms and clears it on exit; if DOOM dies
the HMI gets the controller back within 2 s. Verified on the bench: 303/303 telegrams masked during
a 40 s hold, HMI did not react, continuity afterwards.

## Control map (DOOM, source/i_input_evo.c controller thread)
| control | in game | in the DOOM menu / a prompt |
|---|---|---|
| rotate | turn (rate-scaled: slow detents nudge, fast spins whip round; in-game mouse-sensitivity slider scales it; env `DOOM_CTRL_TURN`, default 170) | move up/down the list |
| tilt fwd / back | walk (held while tilted, min 350 ms on a tap) | up / down |
| tilt left / right | strafe | left / right (sliders) |
| push | fire (held = auto-fire) | select (`y` on a prompt) |
| MENU short | open DOOM menu | close |
| MENU held 1 s | quit DOOM (grab released, HMI back) | same |
| OPTION | open DOOM menu | close |
| BACK | automap | close / "no" |
| MEDIA | use (doors, switches, lifts) | - |
| NAV / TEL | next / previous owned weapon | - |
| COM | run toggle | - |

## Operate
```
/fs/sda1/evo/evo_feedtap 20          # watch decoded events (knob/tilt/push/buttons)
/fs/sda1/evo/evo_feedtap 40 grab     # same while muting the HMI
cd /var/run/screen_tests && sh doom_launch.sh -warp 1 1    # play; hold MENU 1 s to quit
DOOM_CTRL_TURN=6 sh doom_launch.sh -warp 1 1               # turn feel
```
Logs: `/dev/shmem/evo_tee_most.log` (tee), `/dev/shmem/evo_ctrl.log` (DOOM side).

## Lessons
- **A failed LD_PRELOAD is fatal on this QNX** → the shim dlopen-tests first and falls back to a clean exec.
- **Never use `__sync_*` / `__atomic_*` builtins with the Linaro arm-linux toolchain**: libgcc implements
  them via the Linux kernel helpers at 0xffff0fxx, which do not exist on QNX (SIGSEGV; it killed `most`
  once). Use an inline `dmb` (`.word 0xf57ff05b`).
- A `.so` mapped by a running process can't be overwritten with `cat >`; upload as `.new` and `mv` over it.
- Rebooting the unit from the NUC: UDS `11 01` to 0x63 (re_knowledge/scripts/zgw_route.py `Hsfz`).

- **ARM `char` is unsigned**: DOOM's ticcmd uses `char forwardmove/sidemove`; build with `-fsigned-char`
  (backward/strafe-left became +231: "flies forward very fast").
- **devu-kbd / devu-mouse reads can't be unblocked** by close()/select()/readcond(), and a REPLY-blocked thread
  keeps the process from exiting (even SIGKILL waits for the next key/mouse event). Arm a kernel
  `TimerTimeout(_NTO_TIMEOUT_REPLY|_NTO_TIMEOUT_SEND)` before each read (bench-tested with controller/rdtest.c).
- **Launcher race**: a relaunch that slays the old game must not let the OLD launcher's cleanup run (it destroyed
  the new instance's surface/layer -> game ran blind, HMI muted). `target/doom_launch.sh` has a generation lock,
  and releases the audio route (`audioconn -r 0xb8`) before the game rebuilds it (silent relaunch otherwise).
- Quitting from the controller thread by calling I_Quit() deadlocks (it joins its own thread): set a flag and let
  I_GetEvents (main thread) call I_Quit().

## Next
- Launcher page / other homebrew: consume the same ring; grab while the app has the screen.
- Injection into the HMI (synthesised telegrams in the same hook) for software-driven HMI navigation.

#!/usr/bin/env python3
"""evo_drive.py - host-side iDrive input injector + screen verifier for the NBT EVO head unit.

Drives the homebrew launcher and apps PROGRAMMATICALLY, with NO MOST optical bus required.
It injects controller events into the shared feed ring (/dev/shmem/evo_ctrl_feed, evo_feed.h
v3) using the on-unit `evo_feedpush`, then captures the *composited* HMI framebuffer with the
on-unit `evo_screenshot` and pulls it back decoded to PNG - so iDrive navigation and button
combinations can be exercised and visually verified from the NUC, headless.

Why no MOST: evo_feedpush is simply a second producer of the same ring the real "most tee"
feeds.  The launcher / DOOM / terminal all CONSUME that ring; injecting there is exactly the
layer they read.  (The tee's heartbeat `tee_alive` stays live either way.)

Usage:
  evo_drive.py shot [label]                 capture a screenshot only
  evo_drive.py press <token...>             inject a sequence, then screenshot
  evo_drive.py seq <label> <token...>       named inject + screenshot (labelled PNG)
  evo_drive.py procs                         print homebrew process state
  evo_drive.py ring                          print feed-ring head/grab/tee + last events
  evo_drive.py scenario <name>              run a built-in scenario (launch_doom, nav, exit)
  evo_drive.py recover                       slay stray apps + restore the OEM HMI layer

Sequence tokens (lowered to evo_feedpush on the unit, chained with precise sh sleeps):
  turn:<n>        rotary detents (+ = clockwise)          -> evo_feedpush turn <n>
  fwd|back|left|right[:ms]   5-way tilt hold (def 400ms)  -> evo_feedpush <dir> <ms>
  fire[:ms]       centre press (def 250ms)                -> evo_feedpush fire <ms>
  btn:<id>[:ms]   hard button press (id 0x01 MENU/0x02 BACK/...) default 150ms
  wait:<ms>       pause (no event) - for app init / state settle

Env: EVO_HOST (default 169.254.199.119), EVO_PASS (default CHANGEME — set it),
     EVO_OUT  (default /tmp/evo_drive).
"""
import os, sys, subprocess, time, datetime

HOST = os.environ.get("EVO_HOST", "169.254.199.119")
PASS = os.environ.get("EVO_PASS", "CHANGEME")
UNIT_DIR = "/fs/sda1/evo"
FEEDPUSH = UNIT_DIR + "/evo_feedpush"
SHOTBIN  = UNIT_DIR + "/evo_screenshot"
HMI_RAW  = "/dev/shmem/hmi.raw"
FEED     = "/dev/shmem/evo_ctrl_feed"
W, H     = 1440, 540
RING     = 512
DEFOUT   = os.environ.get("EVO_OUT", "/tmp/evo_drive")
SETTLE_MS = 300   # default gap inserted between injected tokens so the launcher's
                  # ~30-50Hz poll processes each event (and its state transition) in turn

SSH_OPTS = ["-o", "HostKeyAlgorithms=+ssh-rsa", "-o", "PubkeyAcceptedKeyTypes=+ssh-rsa",
            "-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=8",
            "-o", "LogLevel=ERROR"]


def ssh(cmd, binary=False, timeout=40):
    env = dict(os.environ, SSHPASS=PASS)
    argv = ["sshpass", "-e", "ssh"] + SSH_OPTS + ["root@%s" % HOST, cmd]
    r = subprocess.run(argv, env=env, stdout=subprocess.PIPE,
                       stderr=(subprocess.DEVNULL if binary else subprocess.PIPE),
                       timeout=timeout)
    return r.stdout if binary else r.stdout.decode("utf-8", "replace")


def token_to_sh(tok):
    """Lower one sequence token to an evo_feedpush invocation (or a sleep)."""
    name, _, arg = tok.partition(":")
    if name == "wait":
        return "sleep %s" % (max(0, int(arg)) / 1000.0)
    if name == "turn":
        return "%s turn %d" % (FEEDPUSH, int(arg or "1"))
    if name in ("fwd", "back", "left", "right"):
        return "%s %s %s" % (FEEDPUSH, name, arg or "400")
    if name == "fire":
        return "%s fire %s" % (FEEDPUSH, arg or "250")
    if name == "btn":
        a = arg.split(":") if arg else []
        bid = a[0] if a else "0x02"
        ms = a[1] if len(a) > 1 else "150"
        return "%s btn %s %s" % (FEEDPUSH, bid, ms)
    raise SystemExit("unknown token: %s" % tok)


def inject(tokens):
    """Run a token sequence on the unit as ONE chained sh command (precise pacing)."""
    parts = []
    for i, t in enumerate(tokens):
        parts.append(token_to_sh(t))
        if i != len(tokens) - 1 and not tokens[i + 1].startswith("wait"):
            parts.append("sleep %s" % (SETTLE_MS / 1000.0))
    sh = "; ".join(parts)
    ssh(sh, timeout=max(40, int(sum(_tok_ms(t) for t in tokens) / 1000) + 20))


def _tok_ms(t):
    name, _, arg = t.partition(":")
    if name == "wait":
        return int(arg or 0)
    if name in ("fwd", "back", "left", "right"):
        return int(arg or 400) + SETTLE_MS
    if name == "fire":
        return int(arg or 250) + SETTLE_MS
    if name == "btn":
        a = arg.split(":")
        return int(a[1]) if len(a) > 1 else 150 + SETTLE_MS
    if name == "turn":
        return abs(int(arg or 1)) * 30 + SETTLE_MS
    return SETTLE_MS


def decode(raw, png):
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit("PIL required: pip install pillow")
    need = W * H * 4
    if len(raw) < need:
        raw = raw + b"\x00" * (need - len(raw))
    img = Image.frombytes("RGBA", (W, H), raw[:need], "raw", "BGRA")
    img.save(png)
    px = img.getdata()
    nb = sum(1 for r, g, b, a in px if r > 8 or g > 8 or b > 8)
    return 100.0 * nb / len(px)


def shot(label="shot", outdir=DEFOUT):
    os.makedirs(outdir, exist_ok=True)
    ssh("cd %s && ./%s >/dev/null 2>&1" % (UNIT_DIR, os.path.basename(SHOTBIN)))
    raw = ssh("cat %s" % HMI_RAW, binary=True)
    ts = datetime.datetime.now().strftime("%H%M%S")
    png = os.path.join(outdir, "%s_%s.png" % (ts, label))
    nb = decode(raw, png)
    print("[shot] %-22s nonblack=%5.1f%%  %s" % (label, nb, png))
    return png, nb


def procs():
    out = ssh("pidin -F '%a %P %N' 2>/dev/null | grep -iE "
              "'evo_launcher|evo_terminal|evo_sysmon|doom_evo' | grep -v grep")
    print("[procs]\n" + (out.rstrip() or "  (no homebrew apps running)"))
    return out


def ring():
    import struct
    d = ssh("cat %s" % FEED, binary=True)
    if len(d) < 8 + RING * 8 + 8:
        print("[ring] short read (%d bytes)" % len(d)); return
    magic, head = struct.unpack_from("<II", d, 0)
    gu, ta = struct.unpack_from("<II", d, 8 + RING * 8)
    kinds = {1: "ROT", 2: "DIR", 3: "CTR", 4: "BTN"}
    print("[ring] magic=0x%08x head=%u grab_until=%u tee_alive=%u" % (magic, head, gu, ta))
    for i in range(max(0, head - 6), head):
        off = 8 + (i % RING) * 8
        k = d[off]; b = list(d[off + 2:off + 8])
        print("   ev[%u] %s b=%s" % (i, kinds.get(k, "?%d" % k), b[:2]))


def recover():
    print("[recover] slay stray apps + restore OEM HMI layer")
    print(ssh("for a in evo_terminal evo_sysmon doom_evo; do slay -9 -f $a 2>/dev/null "
              "&& echo '  killed '$a; done; "
              "LD_LIBRARY_PATH=/opt/sys/lib /opt/sys/bin/LayerManagerControl "
              "set layer 2147483648 visibility 1 >/dev/null 2>&1; echo '  HMI layer restored'").rstrip())


SCENARIOS = {
    # summon -> verify card -> launch DOOM (splash) -> verify no summon-over-app -> BACK-exit -> menu
    "launch_doom": [
        ("base",            []),
        ("summon",          ["back:400"]),                        # card should appear
        ("launched",        ["wait:400", "fire:300", "wait:3800"]),  # splash then DOOM
        ("try_summon_over", ["back:400", "wait:700"]),            # up/down: must NOT show the card over DOOM
        ("back_exit",       ["btn:0x02:200", "wait:2800"]),       # BACK button quits DOOM -> OEM menu
    ],
    # summon then rotate through tiles to verify selection moves
    "nav": [
        ("summon",      ["back:400"]),
        ("right1",      ["turn:1"]),
        ("right2",      ["turn:1"]),
        ("left1",       ["turn:-1"]),
        ("dismiss",     ["back:400"]),
    ],
}


def scenario(name):
    steps = SCENARIOS.get(name)
    if not steps:
        raise SystemExit("unknown scenario %r (have: %s)" % (name, ", ".join(SCENARIOS)))
    outdir = os.path.join(DEFOUT, name + "_" + datetime.datetime.now().strftime("%H%M%S"))
    print("[scenario] %s -> %s" % (name, outdir))
    for label, toks in steps:
        if toks:
            inject(toks)
        shot(label, outdir)
        procs()


def main():
    if len(sys.argv) < 2:
        print(__doc__); return
    cmd = sys.argv[1]
    if cmd == "shot":
        shot(sys.argv[2] if len(sys.argv) > 2 else "shot")
    elif cmd == "press":
        inject(sys.argv[2:]); shot("press")
    elif cmd == "seq":
        inject(sys.argv[3:]); shot(sys.argv[2])
    elif cmd == "procs":
        procs()
    elif cmd == "ring":
        ring()
    elif cmd == "recover":
        recover()
    elif cmd == "scenario":
        scenario(sys.argv[2])
    else:
        raise SystemExit("unknown command %r" % cmd)


if __name__ == "__main__":
    main()

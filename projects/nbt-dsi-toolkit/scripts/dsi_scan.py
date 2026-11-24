#!/usr/bin/env python3
# Scan a pcap for DSI MessageHeaders (type==0x200, protoMajor==4) anywhere in each
# frame (encapsulation-agnostic). Summarize all commands; dump ConnectRequest(9) bytes.
import struct, sys, collections

CMD = {7:"DataReq",8:"DataResp",9:"ConnReq",10:"DiscReq",11:"ConnResp",
       12:"ConnReqNak?",0:"0"}

def frames(fn):
    d=open(fn,'rb').read()
    magic=struct.unpack_from('<I',d,0)[0]
    le='<' if magic in (0xa1b2c3d4,0xa1b23c4d) else '>'
    off=24
    while off+16<=len(d):
        ts_s,ts_u,caplen,origlen=struct.unpack_from(le+'IIII',d,off); off+=16
        yield ts_s+ts_u/1e6, d[off:off+caplen]; off+=caplen

def dsi_headers(buf):
    # yield (offset, type,major,minor, serverID(8), clientID(8), cmd, flags, plen, resv)
    n=len(buf)
    for i in range(0, n-40):
        typ=struct.unpack_from('<i',buf,i)[0]
        if typ!=0x200: continue
        major=struct.unpack_from('<H',buf,i+4)[0]
        if major!=4: continue
        minor=struct.unpack_from('<H',buf,i+6)[0]
        sid=buf[i+8:i+16]; cid=buf[i+16:i+24]
        cmd,flags,plen,resv=struct.unpack_from('<IIiI',buf,i+24)
        if plen<0 or plen>4096: continue
        yield i,typ,major,minor,sid,cid,cmd,flags,plen,resv

def h(b): return b.hex()

fn=sys.argv[1]
want=set(int(x) for x in sys.argv[2:]) if len(sys.argv)>2 else None
counts=collections.Counter()
t0=None
for t,fr in frames(fn):
    if t0 is None: t0=t
    for i,typ,maj,minr,sid,cid,cmd,flags,plen,resv in dsi_headers(fr):
        counts[cmd]+=1
        if want and cmd not in want: continue
        body=fr[i+40:i+40+plen]
        sidv=struct.unpack('<II',sid); cidv=struct.unpack('<II',cid)
        print(f"t={t-t0:7.3f} off={i:4d} cmd={cmd}({CMD.get(cmd,'?')}) "
              f"minor={minr} flags=0x{flags:x} plen={plen} "
              f"serverID={sidv[0]:#010x}/{sidv[1]:#010x} clientID={cidv[0]:#010x}/{cidv[1]:#010x}")
        if cmd in (9,10,11) or (want and cmd in want):
            print(f"    hdr : {h(fr[i:i+40])}")
            print(f"    body: {h(body)}  ({len(body)}B)")
print("---- command histogram ----")
for c in sorted(counts): print(f"  cmd={c:2d} {CMD.get(c,'?'):10s} x{counts[c]}")

#!/usr/bin/env python3
# Reconstruct DSI-over-TCP handshakes from a veom0 pcap.
# Groups packets by TCP connection, flags SYNs, and for the connection(s) that
# carry a target serverID, prints the ordered DSI message timeline and dumps the
# ConnectRequest(9) / subscribe(7 REQUEST_NOTIFY) bytes so we can clone them.
import struct, sys, collections

TARGET = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x0007a12d
CMD = {7:"DataReq",8:"DataResp",9:"ConnReq",10:"DiscReq",11:"ConnResp"}
RT  = {0x0100:"REQUEST",0x0101:"REQUEST_NOTIFY",0x0102:"STOP_NOTIFY",0x0202:"RESULT_DATA_OK"}

def pkts(fn):
    d=open(fn,'rb').read()
    le='<' if struct.unpack_from('<I',d,0)[0] in (0xa1b2c3d4,0xa1b23c4d) else '>'
    off=24; t0=None
    while off+16<=len(d):
        ts_s,ts_u,cap,ol=struct.unpack_from(le+'IIII',d,off); off+=16
        fr=d[off:off+cap]; off+=cap
        t=ts_s+ts_u/1e6
        if t0 is None: t0=t
        yield t-t0,fr

def parse(fr):
    if len(fr)<34 or fr[12:14]!=b'\x08\x00': return None
    ihl=(fr[14]&0xf)*4; proto=fr[14+9]
    if proto!=6: return None
    src='.'.join(map(str,fr[26:30])); dst='.'.join(map(str,fr[30:34]))
    l4=14+ihl
    sport,dport=struct.unpack_from('>HH',fr,l4)
    doff=((fr[l4+12]>>4)&0xf)*4; flags=fr[l4+13]
    pay=fr[l4+doff:]
    return src,sport,dst,dport,flags,pay

def dsi_in(pay):
    out=[]
    for i in range(0,len(pay)-40):
        if struct.unpack_from('<i',pay,i)[0]!=0x200: continue
        if struct.unpack_from('<H',pay,i+4)[0]!=4: continue
        sid=struct.unpack_from('<I',pay,i+8)[0]
        cid=struct.unpack_from('<I',pay,i+16)[0]
        cmd,flags,plen=struct.unpack_from('<IIi',pay,i+24)
        if plen<0 or plen>4096 or i+40+plen>len(pay):
            if not (0<=plen<=4096): continue
        body=pay[i+40:i+40+plen]
        out.append((i,sid,cid,cmd,flags,body))
    return out

conns=collections.defaultdict(list)   # key -> list of (t, dir, flags, dsis)
syns={}
target_conns=set()
for t,fr in pkts(sys.argv[1]):
    p=parse(fr)
    if not p: continue
    src,sport,dst,dport,flags,pay=p
    key=tuple(sorted([(src,sport),(dst,dport)]))
    if flags & 0x02:  # SYN
        syns.setdefault(key,[]).append((t,src,sport,dst,dport,flags&0x10!=0))
    ds=dsi_in(pay)
    conns[key].append((t,src,sport,dst,dport,flags,ds))
    for (_,sid,_,_,_,_) in ds:
        if sid==TARGET: target_conns.add(key)

print(f"target serverID={TARGET:#010x}; connections carrying it: {len(target_conns)}")
for key in target_conns:
    print(f"\n===== connection {key} =====")
    if key in syns:
        for (t,s,sp,d,dp,ack) in syns[key]:
            print(f"  t={t:7.3f} SYN{'-ACK' if ack else ''} {s}:{sp} -> {d}:{dp}  (listener={d}:{dp})")
    else:
        print("  (no SYN captured -> connection pre-existed; started capture too late)")
    for (t,src,sport,dst,dport,flags,ds) in conns[key]:
        for (off,sid,cid,cmd,cf,body) in ds:
            extra=""
            if cmd in (7,9,10,11) and len(body)>=8:
                v=struct.unpack_from('<I',body,4)[0]
                extra=f" rt={RT.get(v,hex(v))}"
            print(f"  t={t:7.3f} {src}:{sport}->{dst}:{dport} {CMD.get(cmd,'?'):9s} "
                  f"sid={sid:#010x} cid={cid:#010x} plen={len(body)}{extra}")
            if cmd in (9,10,11):
                print(f"       body={body.hex()}")

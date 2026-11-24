import struct, sys
fn = sys.argv[1] if len(sys.argv)>1 else "captures/qnet_seq.pcap"
d = open(fn,'rb').read()
# pcap global header 24 bytes; magic
magic = struct.unpack_from('<I', d, 0)[0]
le = '<' if magic in (0xa1b2c3d4,0xa1b23c4d) else '>'
off = 24; t0=None; frames=[]
while off+16 <= len(d):
    ts_s, ts_u, caplen, origlen = struct.unpack_from(le+'IIII', d, off)
    off += 16
    frame = d[off:off+caplen]; off += caplen
    t = ts_s + ts_u/1e6
    if t0 is None: t0=t
    frames.append((t-t0, frame))

def scan_dsi(buf):
    # buf starts at ethernet frame; skip 14-byte eth header
    out=[]
    for i in range(14, len(buf)-40):
        protoMajor = struct.unpack_from('<H', buf, i+4)[0]
        if protoMajor != 4: continue
        cmd = struct.unpack_from('<I', buf, i+24)[0]
        plen= struct.unpack_from('<I', buf, i+32)[0]
        if cmd not in (7,8) or plen>160 or plen<0: continue
        if i+40+plen > len(buf): 
            plen = len(buf)-i-40    # truncated (snaplen)
        body = buf[i+40:i+40+plen]
        sid = struct.unpack_from('<I', buf, i+8)[0]
        if len(body)>=16:
            rtype=struct.unpack_from('<I',body,4)[0]; rid=struct.unpack_from('<I',body,8)[0]
            data=body[16:]
            ints=list(struct.unpack_from('<%di'%(len(data)//4),data,0)) if len(data)>=4 else []
            out.append((cmd,sid,rtype,rid,ints))
        break   # first DSI header per frame
    return out

last={}
for t,fr in frames:
    for cmd,sid,rtype,rid,ints in scan_dsi(fr):
        if cmd!=8: continue           # notifications only
        key=(sid,rid)
        if ints!=last.get(key):
            chg=""
            if key in last and last[key] and len(last[key])==len(ints):
                chg=" Δ="+",".join(("%+d"%(ints[i]-last[key][i]) if ints[i]!=last[key][i] else "·") for i in range(len(ints)))
            print("t=%5.1f sid=0x%08x rid=0x%08x n=%2d [%s]%s"%(t,sid,rid,len(ints),",".join(str(v) for v in ints[:14]),chg))
            last[key]=ints

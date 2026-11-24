import struct, sys
fn=sys.argv[1]
d=open(fn,'rb').read(); off=24; t0=None; frames=[]
while off+16<=len(d):
    ts_s,ts_u,cap,orig=struct.unpack_from('<IIII',d,off); off+=16
    fr=d[off:off+cap]; off+=cap; t=ts_s+ts_u/1e6
    if t0 is None:t0=t
    frames.append((t-t0,fr))
# loose: first DSI header per frame
def first(buf):
    for i in range(14,len(buf)-40):
        if struct.unpack_from('<H',buf,i+4)[0]==4:
            cmd=struct.unpack_from('<I',buf,i+24)[0]; plen=struct.unpack_from('<I',buf,i+32)[0]
            if cmd in(7,8) and 0<=plen<=160:
                sid=struct.unpack_from('<I',buf,i+8)[0]
                body=buf[i+40:i+40+plen]
                rid=struct.unpack_from('<I',body,8)[0] if len(body)>=12 else 0
                return (cmd,sid,rid,i)
    return None
st={}; undecoded=0
for t,fr in frames:
    r=first(fr)
    if not r: undecoded+=1; continue
    cmd,sid,rid,ioff=r; k=(cmd,sid,rid)
    s=st.setdefault(k,{'n':0,'t0':t,'t1':t}); s['n']+=1; s['t1']=t
print("frames=%d decoded_keys=%d undecoded_frames=%d"%(len(frames),len(st),undecoded))
for k in sorted(st,key=lambda k:(st[k]['t1']-st[k]['t0'])):
    cmd,sid,rid=k;s=st[k]
    print("cmd=%d sid=0x%08x rid=0x%08x cnt=%3d span=%.1f-%.1f"%(cmd,sid,rid,s['n'],s['t0'],s['t1']))

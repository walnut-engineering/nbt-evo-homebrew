import struct, sys
fn=sys.argv[1]
d=open(fn,'rb').read(); le='<'; off=24; t0=None; frames=[]
while off+16<=len(d):
    ts_s,ts_u,cap,orig=struct.unpack_from(le+'IIII',d,off); off+=16
    fr=d[off:off+cap]; off+=cap; t=ts_s+ts_u/1e6
    if t0 is None:t0=t
    frames.append((t-t0,fr))
def dsis(buf):
    r=[]; i=14; n=len(buf)
    while i < n-40:
        pM=struct.unpack_from('<H',buf,i+4)[0]; pm=struct.unpack_from('<H',buf,i+6)[0]
        cmd=struct.unpack_from('<I',buf,i+24)[0]; plen=struct.unpack_from('<I',buf,i+32)[0]
        ok=(pM==4 and pm<16 and cmd in(7,8) and 0<=plen<=160)
        if ok and len(buf)>=i+40+16:
            rtype=struct.unpack_from('<I',buf,i+40+4)[0]
            if cmd==8 and not(0x0200<=rtype<=0x0205): ok=False
            if cmd==7 and not(0x0100<=rtype<=0x0107): ok=False
        if ok:
            sid=struct.unpack_from('<I',buf,i+8)[0]
            body=buf[i+40:i+40+plen]
            rid=struct.unpack_from('<I',body,8)[0] if len(body)>=12 else 0
            data=body[16:] if len(body)>16 else b''
            ints=list(struct.unpack_from('<%di'%(len(data)//4),data,0)) if len(data)>=4 else []
            r.append((cmd,sid,rid,ints)); i+=40+max(plen,8)
        else: i+=4
    return r
stats={}
for t,fr in frames:
    for cmd,sid,rid,ints in dsis(fr):
        k=(cmd,sid,rid)
        s=stats.setdefault(k,{'n':0,'t0':t,'t1':t,'vals':set(),'nint':len(ints)})
        s['n']+=1; s['t1']=t; 
        if ints: s['vals'].add(tuple(ints[:6]))
for k in sorted(stats,key=lambda k:-stats[k]['n']):
    cmd,sid,rid=k; s=stats[k]
    print("cmd=%d sid=0x%08x rid=0x%08x  cnt=%3d  t=%.1f-%.1f  nint=%d  distinct=%d"%(
        cmd,sid,rid,s['n'],s['t0'],s['t1'],s['nint'],len(s['vals'])))

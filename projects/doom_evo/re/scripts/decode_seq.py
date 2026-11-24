import subprocess, struct, sys, re
PCAP = sys.argv[1] if len(sys.argv)>1 else "captures/ctrl_seq.pcap"
out = subprocess.check_output(["tcpdump","-nttr",PCAP,"-x","src host 160.48.199.253 and tcp"],
      stderr=subprocess.DEVNULL).decode(errors="replace")
pkts=[]; cur=None; ts=None; t0=None
for ln in out.splitlines():
    m=re.match(r'^\s+0x([0-9a-f]+):\s+(.*)$', ln)
    if m:
        hp=m.group(2).split('  ')[0].replace(' ','')
        try: cur.extend(bytes.fromhex(hp))
        except: pass
    else:
        tm=re.match(r'^(\d+\.\d+)\s', ln)
        if tm:
            if cur is not None and len(cur): pkts.append((ts,bytes(cur)))
            ts=float(tm.group(1)); 
            if t0 is None: t0=ts
            cur=bytearray()
if cur is not None and len(cur): pkts.append((ts,bytes(cur)))

last={}
for ts,p in pkts:
    pl=p[40:]                           # IP(20)+TCP(20)
    if len(pl)<40: continue
    cmd=struct.unpack_from('<I',pl,24)[0]; plen=struct.unpack_from('<I',pl,32)[0]
    if cmd!=8 or plen<16 or 40+plen>len(pl): 
        # try to also catch messages not at offset 0 (coalesced) - skip for now
        continue
    body=pl[40:40+plen]
    reqid=struct.unpack_from('<I',body,8)[0]
    data=body[16:]; ints=list(struct.unpack_from('<%di'%(len(data)//4),data,0)) if len(data)>=4 else []
    key=reqid
    if ints!=last.get(key):
        rel=ts-t0
        chg=""
        if key in last and last[key] and len(last[key])==len(ints):
            chg=" Δ="+",".join(("%+d"%(ints[i]-last[key][i]) if ints[i]!=last[key][i] else "·") for i in range(len(ints)))
        print("t=%5.1f 0x%08x n=%2d [%s]%s"%(rel,reqid,len(ints),",".join("%d"%v for v in ints),chg))
        last[key]=ints

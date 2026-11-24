import subprocess, struct, sys, re
PCAP=sys.argv[1]
flt=sys.argv[2] if len(sys.argv)>2 else "tcp"
out=subprocess.check_output(["tcpdump","-nttr",PCAP,"-x",flt],stderr=subprocess.DEVNULL).decode(errors="replace")
pkts=[]; cur=None; ts=None; hdr=None; t0=None
for ln in out.splitlines():
    m=re.match(r'^\s+0x([0-9a-f]+):\s+(.*)$',ln)
    if m:
        hp=m.group(2).split('  ')[0].replace(' ','')
        try: cur.extend(bytes.fromhex(hp))
        except: pass
    else:
        tm=re.match(r'^(\d+\.\d+) IP (\S+) > (\S+):',ln)
        if tm:
            if cur is not None and len(cur): pkts.append((ts,hdr,bytes(cur)))
            ts=float(tm.group(1)); hdr=(tm.group(2),tm.group(3))
            if t0 is None:t0=ts
            cur=bytearray()
if cur is not None and len(cur): pkts.append((ts,hdr,bytes(cur)))
CMD={7:"DataReq",8:"DataResp",9:"ConnReq",10:"DiscReq",11:"ConnResp"}
for ts,hdr,p in pkts:
    pl=p[40:]
    if len(pl)<40: continue
    cmd=struct.unpack_from('<I',pl,24)[0]; plen=struct.unpack_from('<I',pl,32)[0]
    if cmd not in CMD or plen>len(pl)-40 or plen<0: continue
    s=hdr[0].split('.'); src=s[-1] if len(s)>=5 else hdr[0]
    d=hdr[1].split('.'); dst=d[-1].rstrip(':') if len(d)>=5 else hdr[1]
    body=pl[40:40+plen]
    extra=""
    if cmd in (7,8) and len(body)>=16:
        rtype=struct.unpack_from('<I',body,4)[0]; reqid=struct.unpack_from('<I',body,8)[0]
        data=body[16:]; ints=list(struct.unpack_from('<%di'%(len(data)//4),data,0)) if len(data)>=4 else []
        extra="rid=0x%08x rt=0x%04x [%s]"%(reqid,rtype,",".join(str(v) for v in ints[:16]))
    print("t=%5.1f %5s>%5s %-8s plen=%3d %s"%(ts-t0,src,dst,CMD[cmd],plen,extra))

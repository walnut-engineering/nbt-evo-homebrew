import sys, re
HDR=40
# known noise/known payload signatures (payload = after 40-byte header)
def classify(p):
    if len(p)>=4 and p[0]==0x15 and p[1]==0x02 and p[2]==0x01 and p[3]==0x04: return "poll"
    if len(p)>=2 and p[0]==0xe5 and p[1]==0x0f: return "heartbeat"
    if len(p)>=3 and p[0]==0x05 and p[1]==0x00 and p[2]==0x1f: return "status"
    if len(p)>=16 and p[0]==0xe5 and p[1]==0x01 and p[7]==0xe5 and p[9]==0x07 and p[14]==0x1d:
        return "ROTARY ctr=0x%02x"%p[15]
    return None
rows=[]
for line in open(sys.argv[1], errors="replace"):
    m=re.match(r'([\d.]+)\s+(ipc\d+)\s+n=(\d+):(.*)', line.strip())
    if not m: continue
    t=float(m.group(1)); ch=m.group(2); hexs=m.group(4).split()
    try: b=[int(x,16) for x in hexs]
    except: continue
    if len(b)<=HDR: continue
    rows.append((t,ch,b[HDR:]))
if not rows:
    print("NO FRAMES CAPTURED (controller idle or capture empty)"); sys.exit(0)
t0=rows[0][0]
# cluster by gap > 0.4s
clusters=[]; cur=[]
last=None
for r in rows:
    k=classify(r[2])
    if k in ("poll","heartbeat","status"): continue  # drop pure noise
    if last is not None and r[0]-last>0.4 and cur:
        clusters.append(cur); cur=[]
    cur.append(r); last=r[0]
if cur: clusters.append(cur)
print("== %d event clusters (noise dropped) =="%len(clusters))
for i,c in enumerate(clusters):
    kinds={}
    sigs={}
    for t,ch,p in c:
        k=classify(p) or "OTHER"
        kk=k.split()[0]
        kinds[kk]=kinds.get(kk,0)+1
        if kk=="OTHER":
            sig=ch+":"+" ".join("%02x"%x for x in p[:14])
            sigs[sig]=sigs.get(sig,0)+1
    rel=c[0][0]-t0
    summ=", ".join("%s x%d"%(k,n) for k,n in kinds.items())
    print("\n[cluster %d  t+%.1fs]  %s"%(i+1,rel,summ))
    for sig,n in sorted(sigs.items(),key=lambda x:-x[1])[:6]:
        print("    OTHER x%-3d %s"%(n,sig))

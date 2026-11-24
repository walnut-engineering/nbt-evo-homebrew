import subprocess, struct, sys, re
PCAP = sys.argv[1] if len(sys.argv)>1 else "captures/spin.pcap"
# get per-packet hex for the controller notification direction (.253:65528 -> .254:65521)
out = subprocess.check_output(
  ["tcpdump","-nr",PCAP,"-x","src host 160.48.199.253 and src port 65528 and greater 60"],
  stderr=subprocess.DEVNULL).decode(errors="replace")
# tcpdump -x prints lines "\t0x0010:  hhhh hhhh ..." grouped per packet (packet line has no 0x prefix)
pkts=[]; cur=None
for ln in out.splitlines():
    m=re.match(r'^\s+0x([0-9a-f]+):\s+(.*)$', ln)
    if m:
        hexpart=m.group(2).split('  ')[0]
        cur.extend(bytes.fromhex(hexpart.replace(' ','')))
    elif ln and not ln.startswith('\t'):
        if cur is not None: pkts.append(bytes(cur))
        cur=bytearray()
if cur: pkts.append(bytes(cur))

print("packets:",len(pkts))
prev=None
for p in pkts:
    if len(p) < 40: continue
    pl = p[34:]                 # strip 14 eth? -x starts at IP. 20 IP +20 TCP =40 -> payload at 40
    pl = p[40:]
    if len(pl) < 40+16: continue
    cmd = struct.unpack_from('<I', pl, 24)[0]
    plen= struct.unpack_from('<I', pl, 32)[0]
    if cmd != 8: 
        continue
    body = pl[40:40+plen]
    if len(body) < 16: continue
    rtype = struct.unpack_from('<I', body, 4)[0]
    reqid = struct.unpack_from('<I', body, 8)[0]
    data = body[16:]
    ints = list(struct.unpack_from('<%di'%(len(data)//4), data, 0))
    tag = "0x%08x"%reqid
    line = tag+" rtype=0x%04x n=%d "%(rtype,len(ints))+" ".join("%d"%v for v in ints)
    # show delta from prev same-reqid
    print(line)

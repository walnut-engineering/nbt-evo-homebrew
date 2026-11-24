#!/usr/bin/env python3
# find_knob.py <spin.pcap> - from a wire capture taken WHILE the iDrive controller
# is operated, rank DSI notification streams by activity so the knob/buttons stand
# out. Each (serverID, requestID) is scored by notification count + distinct values
# + value range (a rotary counter sweeps a range; a button toggles 0/1).
import struct, sys, collections

def notifs(fn):
    d=open(fn,'rb').read(); off=24
    while off+16<=len(d):
        ts_s,ts_u,cap,ol=struct.unpack_from('<IIII',d,off); off+=16
        fr=d[off:off+cap]; off+=cap
        for i in range(0,len(fr)-40):
            if struct.unpack_from('<i',fr,i)[0]!=0x200 or struct.unpack_from('<H',fr,i+4)[0]!=4: continue
            sid=struct.unpack_from('<I',fr,i+8)[0]
            cmd,flags,plen=struct.unpack_from('<IIi',fr,i+24)
            if cmd!=8 or plen<16 or i+40+plen>len(fr): break
            body=fr[i+40:i+40+plen]
            rtype,rid=struct.unpack_from('<II',body,4)
            data=body[16:]; ints=struct.unpack_from('<%di'%(len(data)//4),data,0) if len(data)>=4 else ()
            yield sid,rid,rtype,ints
            break

stat=collections.defaultdict(lambda:{'n':0,'vals':collections.Counter(),'rtypes':set()})
for sid,rid,rtype,ints in notifs(sys.argv[1]):
    s=stat[(sid,rid)]; s['n']+=1; s['rtypes'].add(rtype)
    s['vals'][ints[0] if ints else None]+=1

rows=[]
for (sid,rid),s in stat.items():
    vals=[v for v in s['vals'] if v is not None]
    distinct=len(set(vals)); rng=(max(vals)-min(vals)) if vals else 0
    score=s['n']+distinct*10+ (1 if distinct>1 else 0)*50
    rows.append((score,sid,rid,s['n'],distinct,rng,sorted(set(vals))[:12]))
rows.sort(reverse=True)
print(f"{'score':>6} {'serverID':>10} {'requestID':>10} {'count':>6} {'distinct':>8} {'range':>8}  values")
for score,sid,rid,n,distinct,rng,vals in rows[:30]:
    mark=' <== CHANGING' if distinct>1 else ''
    print(f"{score:6d} {sid:#010x} {rid:#010x} {n:6d} {distinct:8d} {rng:8d}  {vals}{mark}")
print("\nStreams with distinct>1 are the ones that changed during the capture (knob/buttons).")

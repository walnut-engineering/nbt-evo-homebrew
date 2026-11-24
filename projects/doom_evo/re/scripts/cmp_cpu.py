import re
def t2s(x):
    x=x.strip()
    if re.fullmatch(r'[0-9]+(\.[0-9]+)?', x): return float(x)
    tot=0.0
    for v,u in re.findall(r'([0-9]+)([dhms])', x): tot+=int(v)*{'d':86400,'h':3600,'m':60,'s':1}[u]
    return tot
def load(fn):
    d={}
    for line in open(fn):
        p=line.split()
        if len(p)<8 or not p[0].isdigit(): continue
        try: d[p[0]]=(p[1], t2s(p[6])+t2s(p[7]))
        except: pass
    return d
def deltas(f1,f2):
    a,b=load(f1),load(f2); out={}
    for pid,(n,c2) in b.items(): out[pid]=(n, c2-a.get(pid,(n,0))[1])
    return out
def report(spin1,spin2,idle1,idle2,label):
    s=deltas(spin1,spin2); i=deltas(idle1,idle2)
    rows=[]
    for pid,(n,sd) in s.items():
        idl=i.get(pid,(n,0))[1]
        rows.append((sd-idl, sd, idl, pid, n))
    rows.sort(reverse=True)
    print(f"=== {label}: spin CPU minus idle CPU (sec) — controller signal ===")
    print(f"  {'net':>8} {'spin':>8} {'idle':>8}  pid        name")
    for net,sd,idl,pid,n in rows[:12]:
        print(f"  {net:+8.3f} {sd:8.3f} {idl:8.3f}  {pid:>8}  {n}")
report('o1.txt','o2.txt','o3.txt','o4.txt','OMAP')
print()
report('j1.txt','j2.txt','j3.txt','j4.txt','JACINTO')

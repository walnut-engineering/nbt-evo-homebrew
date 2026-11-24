import re, sys

def t2s(x):
    x=x.strip()
    if re.fullmatch(r'[0-9]+\.[0-9]+', x) or re.fullmatch(r'[0-9]+', x):
        return float(x)
    tot=0.0; 
    for val,unit in re.findall(r'([0-9]+)([dhms])', x):
        v=int(val); tot += v*{'d':86400,'h':3600,'m':60,'s':1}[unit]
    return tot

def load(fn):
    d={}
    for line in open(fn):
        p=line.split()
        if len(p)<8 or not p[0].isdigit(): continue
        pid=p[0]; name=p[1]
        # fields: pid name sid M DD YYYY/HH:MM utime stime ...
        try:
            ut=t2s(p[6]); st=t2s(p[7])
        except Exception:
            continue
        d[pid]=(name, ut+st)
    return d

def diff(f1,f2,label):
    a=load(f1); b=load(f2)
    rows=[]
    for pid,(name,cpu2) in b.items():
        cpu1=a.get(pid,(name,0.0))[1]
        rows.append((cpu2-cpu1, pid, name))
    rows.sort(reverse=True)
    print(f"=== {label}: top CPU gainers over the spin window (sec) ===")
    for d,pid,name in rows[:15]:
        if d<=0.0001: continue
        print(f"  +{d:7.3f}s  pid {pid:>8}  {name}")

diff('o1.txt','o2.txt','OMAP')
print()
diff('j1.txt','j2.txt','JACINTO')

import sys,struct,lzma
REQ={0:[2],1:[2],2:[],3:[1,2],4:[1,2],5:[1,2],6:[0,1,2],7:[0,1,2],8:[2],9:[2,3],10:[3,4],11:[5],12:[5],13:[5]}
d=lzma.open(sys.argv[1]).read(); pos=8
def u(n):
    global pos; v=int.from_bytes(d[pos:pos+n],'little'); pos+=n; return v
def s64(): 
    v=u(8); return v-(1<<64) if v>>63 else v
regq=set(); ops=[]  # tree
def pop(depth):
    k=u(1); fl=u(1); gated=bool(fl&2); 
    if gated: u(4); u(8); [u(8) for _ in range(depth)]
    slots=list(REQ[k]); 
    if fl&1: slots.append(4)
    if fl&4: slots.append(6)
    ops_=[]
    for s in slots:
        b=u(4); st=[s64() for _ in range(depth)]; ops_.append((s,b,st))
    return ('op',k,ops_)
def ploop(enc):
    cnt=u(8); n=u(2); ch=[]
    for _ in range(n):
        ik=u(1); ch.append(pop(enc+1) if ik==0 else ploop(enc+1))
    return ('loop',cnt,ch)
top=[]
while True:
    seg=u(1)
    if seg==1:
        nr=u(4)
        for _ in range(nr):
            k=u(1); nw=u(4)
            for _ in range(nw):
                w=u(4)
                if k==0: regq.add(w)
    elif seg==2:
        u(4); nb=u(8); pos+=(nb+7)//8
    elif seg==3:
        for _ in range(u(4)): top.append(pop(0))
    elif seg==4: top.append(ploop(0))
    elif seg==0xFF: break
first={};last={};resets={};segs=[]
t=[0]
def run(node,idx):
    if node[0]=='op':
        _,k,o=node
        for s,b,st in o:
            if s<=2:
                w=b+sum(a*i for a,i in zip(st,idx))
                if k in (8,9) and s==2 and w in first and w not in regq:
                    segs.append((first[w],last[w])); first[w]=t[0]
                first.setdefault(w,t[0]); last[w]=t[0]
        t[0]+=1
    else:
        for i in range(node[1]):
            for c in node[2]: run(c,[i]+idx[:0] if False else idx+[i]) if False else run(c,idx+[i])
sys.setrecursionlimit(10000)
# strides are ordered outermost depth first? try that assumption: idx list outer->inner
for n in top: run(n,[])
anc=segs+[(first[w],last[w]) for w in first if w not in regq]
ev=[]
for a,b in anc: ev.append((a,1)); ev.append((b+1,-1))
ev.sort(); cur=mx=0
for _,e in ev: cur+=e; mx=max(mx,cur)
print('reg qubit wires',len(regq),'touched',len(first),'max id+1',max(first)+1,'ancilla wires',len(anc),'max ancilla overlap',mx,'=> lower bound on relocation',len(regq)+mx)

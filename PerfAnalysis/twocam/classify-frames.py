import sys
W,H=135,278
fs=W*H
d=open(sys.argv[1],'rb').read()
n=len(d)//fs
frames=[d[i*fs:(i+1)*fs] for i in range(n)]
refA=open('/tmp/pd/f2/refA.raw','rb').read()[:fs]
refB=open('/tmp/pd/f2/refB.raw','rb').read()[:fs]
def mad(a,b): return sum(abs(x-y) for x,y in zip(a,b))/fs
runs=[]
for i,f in enumerate(frames):
    m=sum(f)/fs
    if m<=3.0: t='black'
    else:
        da,db=mad(f,refA),mad(f,refB)
        t='A' if da<8 else ('B' if db<8 else 'other')
    if runs and runs[-1][0]==t: runs[-1][2]=i
    else: runs.append([t,i,i])
a_runs=[r for r in runs if r[0]=='A']
b_runs=[r for r in runs if r[0]=='B']
# alternation = A runs that start after the first B run started
firstB=next((r[1] for r in b_runs), None)
after=[r for r in a_runs if firstB is not None and r[1]>firstB]
print(f"{sys.argv[2] if len(sys.argv)>2 else sys.argv[1]}: {n} frames")
print(f"  A runs={len(a_runs)} B runs={len(b_runs)}  A-runs after the first B={len(after)}")
for t,a,b in runs:
    if t!='black': print(f"    {t:5s} {a:4d}..{b:4d} {b-a+1:4d} f  {a/60.0:5.2f}s..{b/60.0:5.2f}s")

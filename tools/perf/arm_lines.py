#!/usr/bin/env python3
# Ranks by function and line a histogram of ARM addresses (arm_count.sh). Usage: arm_lines.py <histogram> <binary> <translator.c>
import subprocess, collections, sys
L=[l.split() for l in open(sys.argv[1])]
L=[(int(a,16),int(b)) for a,b in L]
inp='\n'.join(hex(a) for a,b in L)
r=subprocess.run(['arm-linux-gnueabihf-addr2line','-f','-e',sys.argv[2]],input=inp,capture_output=True,text=True).stdout.split('\n')
fn=collections.Counter(); ln=collections.Counter()
for i,(a,b) in enumerate(L):
    f=r[2*i]; l=r[2*i+1].split('/')[-1].split(' ')[0]
    fn[f]+=b; ln[l]+=b
T=sum(fn.values()); print('total',T/3e6,'M per frame approx')
for k,v in fn.most_common(12): print(f'{v/1e6:8.2f}M {v*100/T:5.1f}% {k}')
src=open(sys.argv[3]).read().split('\n')
print()
for k,v in ln.most_common(24):
    n=k.split(':'); t=''
    if n[0]=='translator.c' and n[1].isdigit(): t=src[int(n[1])-1].strip()[:66]
    print(f'{v/1e6:7.2f}M {v*100/T:5.1f}% {k:20} {t}')

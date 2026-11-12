#!/usr/bin/env python3
"""Texture budget for the Nintendo DS port. Reads a pnj.pack and, for each stage, packs every sprite of the four sheets the stage draws
(sysparts, charaparts, the stage map and character sheets) into one page at a given scale, to show whether it fits the DS texture memory (512 KB).
Scale is relative to native (the 320x480 field, which is the pack 1024 sheets halved).  Usage: python3 tools/nds_budget.py pnj.pack [scale ...]
Needs numpy, scipy and Pillow. Sprites are found as connected opaque regions, so this is an estimate, not the final packer."""
import struct, io, sys
import numpy as np
from PIL import Image
from scipy import ndimage
d=open(sys.argv[1],'rb').read(); n=struct.unpack('<I',d[8:12])[0]; p=12; ent={}
for i in range(n):
    l=struct.unpack('<H',d[p:p+2])[0]; p+=2; nm=d[p:p+l].decode(); p+=l
    off,sz=struct.unpack('<II',d[p:p+8]); p+=8; ent[nm]=(off,sz)
def get(nm): o,s=ent[nm]; return d[o:o+s]
atlas=[tuple(map(int,l.split())) for l in get('meta/atlas.txt').decode().splitlines() if l.strip()]
TEX=["bnglogo","bndr_logo","sysparts","stages","charaparts"]+[f"stage_{i}_{k}" for i in range(1,9) for k in ("map","cha")]
use={}
for t,x,y,w,h,tw in atlas: use.setdefault(t,0); use[t]+=1
print('sprites per tex',{TEX[k]:v for k,v in sorted(use.items())})
def blobs(name,scale):
    im=Image.open(io.BytesIO(get(f'tex/{name}.png'))).convert('RGBA'); a=np.array(im)[:,:,3]>8
    m=ndimage.binary_dilation(a,iterations=3)
    lab,k=ndimage.label(m); sl=ndimage.find_objects(lab)
    out=[]
    for s in sl:
        h=s[0].stop-s[0].start; w=s[1].stop-s[1].start
        # native = half of 1024; target = native*scale
        out.append((max(1,int(np.ceil(w*0.5*scale))),max(1,int(np.ceil(h*0.5*scale)))))
    return out,int(a.sum()*0.25*scale*scale)
def shelf(rects,W):
    rects=sorted(rects,key=lambda r:-r[1]); x=y=sh=0
    for w,h in rects:
        if w>W: return None
        if x+w>W: y+=sh; x=0; sh=0
        x+=w; sh=max(sh,h)
    return y+sh
for scale in ([float(a) for a in sys.argv[2:]] or [0.8,0.5]):
    print('scale',scale)
    for st in range(1,9):
        names=['sysparts','charaparts',f'stage_{st}_map',f'stage_{st}_cha']
        allr=[];opq=0
        for nm in names:
            r,o=blobs(nm,scale); allr+=r; opq+=o
        res={}
        for W in (256,512,1024):
            hh=shelf(allr,W); res[W]=hh
        bbox=sum(w*h for w,h in allr)
        print(f' stage {st}: blobs {len(allr)}, opaque {opq//1000}K px, blob boxes {bbox//1000}K px, shelf height @W=256:{res[256]} 512:{res[512]} 1024:{res[1024]}')

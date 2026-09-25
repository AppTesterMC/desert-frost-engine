"""Reference SAL room renderer. Usage: dune_room.py ROOMS.SAL SHEET.HSQ room out.png"""
import sys,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from dune_sprite_sheet import dat,unhsq,sprites,png
def fillpoly(scr,pts,col):
    ys=[p[1] for p in pts]
    for y in range(max(0,min(ys)),min(199,max(ys))+1):
        xs=[]
        for i in range(len(pts)):
            (x1,y1),(x2,y2)=pts[i],pts[(i+1)%len(pts)]
            if y1==y2: continue
            if min(y1,y2)<=y<max(y1,y2): xs.append(x1+(y-y1)*(x2-x1)/(y2-y1))
        xs.sort()
        for a,b in zip(xs[::2],xs[1::2]):
            for x in range(max(0,int(round(a))),min(319,int(round(b)))+1): scr[y*320+x]=col
def line(scr,x1,y1,x2,y2,col):
    n=max(abs(x2-x1),abs(y2-y1),1)
    for i in range(n+1):
        x=x1+(x2-x1)*i//n; y=y1+(y2-y1)*i//n
        if 0<=x<320 and 0<=y<200: scr[y*320+x]=col
def render(d,room,res,log=False):
    scr=bytearray(320*200); o=d[2*room]|d[2*room+1]<<8; o+=1
    while True:
        a,m=d[o],d[o+1]; o+=2
        if a==0xff and m==0xff: break
        if m&0x80:
            if m&0x40:
                v=[d[o+i]|(d[o+i+1]&15)<<8 for i in(0,2,4,6)]; o+=8; line(scr,v[0],v[1],v[2],v[3],a)
            else:
                hdr=d[o:o+2]; o+=2; e=0; pts=[]; other=[]
                while e<0xC0:  # side A top-to-bottom until flag 0x40, then side B until 0x80
                    x=d[o]|(d[o+1]&15)<<8; y=d[o+2]|(d[o+3]&15)<<8
                    (other if e&0x40 else pts).append((x,y)); e+=d[o+1]&0xf0; o+=4
                pts+=other[::-1]
                if log: print('poly col',a,'mod',hex(m),hdr.hex(),pts)
                fillpoly(scr,pts,a)
        elif a==1: o+=3
        else:
            x,y,po=d[o],d[o+1],d[o+2]; o+=3
            if m&2: x+=256
            if log: print('sprite',a-1,'mod',hex(m),x,y,'pal',po)
            if a-1<len(res) and res[a-1]:
                w,h,px=res[a-1]
                for j in range(h):
                    for i in range(w):
                        c=px[j][w-1-i if m&0x40 else i]
                        if c and 0<=x+i<320 and 0<=y+j<200: scr[(y+j)*320+x+i]=c
    return scr
if __name__=='__main__':
    f=dat(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','DUNE.DAT'))
    d=unhsq(f[sys.argv[1]]); pal,res=sprites(unhsq(f[sys.argv[2]]))
    scr=render(d,int(sys.argv[3]),res,log=True)
    rgb=bytearray()
    for c in scr: rgb+=bytes(pal.get(c,(c,c,c)))
    png(sys.argv[4],320,200,rgb)

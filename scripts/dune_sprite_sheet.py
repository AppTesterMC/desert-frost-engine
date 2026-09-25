import struct,sys,zlib
def dat(path):
    d=open(path,'rb').read(); n=struct.unpack_from('<H',d)[0]; out={}
    for i in range(n):
        o=2+i*25; name=d[o:o+16].split(b'\0')[0].decode().upper()
        if not name: break
        sz,off=struct.unpack_from('<II',d,o+16); out[name]=d[off:off+sz]
    return out
def unhsq(d):
    if len(d)<6 or sum(d[:6])&0xff!=0xAB: return d
    src=d[6:]; p=0; out=bytearray(); q=0; bits=0
    def bit():
        nonlocal q,bits,p
        if bits==0: q=src[p]|src[p+1]<<8; p+=2; bits=16
        b=q&1; q>>=1; bits-=1; return b
    while True:
        if bit(): out.append(src[p]); p+=1; continue
        if bit():
            a,b=src[p],src[p+1]; p+=2; c=a&7; off=((a>>3)|(b<<5))-0x2000
            if c==0:
                c=src[p]; p+=1
                if c==0: break
        else:
            c=bit()*2; c+=bit(); off=src[p]-256; p+=1
        for _ in range(c+2): out.append(out[len(out)+off])
    return bytes(out)
def sprites(d):
    pe=struct.unpack_from('<H',d)[0]; pal={}
    p=2
    while p+2<=pe:
        st,cn=d[p],d[p+1]; p+=2
        if st==0xff and cn==0xff: break
        for i in range(cn): pal[st+i]=tuple(min(255,v<<2) for v in d[p:p+3]); p+=3
    tsz=struct.unpack_from('<H',d,pe)[0]; n=(tsz)//2  # first offset = table size
    res=[]
    for i in range(n):
        o=pe+struct.unpack_from('<H',d,pe+2*i)[0]
        if o+4>len(d): break
        w0,w1=struct.unpack_from('<HH',d,o); w=w0&0x1ff; comp=w0&0x8000; h=w1&0xff; po=w1>>8; o+=4
        if not w or not h or w>320 or h>200: res.append(None); continue
        pw=(w+3)&~3; px=[[0]*w for _ in range(h)]
        try:
          for y in range(h):
            x=0
            while x<pw:
                cnt=1; fill=False
                if comp:
                    r=d[o]; o+=1; r=r-256 if r>127 else r; fill=r<0; cnt=abs(r)+1
                    if fill: v=d[o]; o+=1
                for _ in range(cnt):
                    if not fill: v=d[o]; o+=1
                    for nb in (v&15,v>>4):
                        if x<w and nb: px[y][x]=(nb+po)&0xff
                        x+=1
        except IndexError: pass
        res.append((w,h,px))
    return pal,res
def png(path,w,h,rgb):
    raw=b''.join(b'\0'+bytes(rgb[y*w*3:(y+1)*w*3]) for y in range(h))
    def ch(t,b): c=struct.pack('>I',len(b))+t+b; return c+struct.pack('>I',zlib.crc32(t+b))
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))
if __name__=='__main__':
    files=dat(sys.argv[1]); name=sys.argv[2]; d=unhsq(files[name]); pal,res=sprites(d)
    print(name,len(d),'bytes',len(res),'frames',[ (r[0],r[1]) if r else None for r in res], 'pal ranges',min(pal or [0]),max(pal or [0]),len(pal))
    W=1300; x=y=rowh=0; place=[]
    for r in res:
        if not r: continue
        if x+r[0]>W: x=0;y+=rowh+4;rowh=0
        place.append((x,y,r)); x+=r[0]+4; rowh=max(rowh,r[1])
    H=y+rowh; buf=bytearray([40,0,40]*(W*H))
    for X,Y,(w,h,px) in place:
        for j in range(h):
            for i in range(w):
                c=px[j][i]
                rgb=pal.get(c,(c,c,c)) if c else (0,0,0)
                buf[((Y+j)*W+X+i)*3:((Y+j)*W+X+i)*3+3]=bytes(rgb)
    png(sys.argv[3],W,H,buf)

"""Reference HNM1 decoder. Usage: dune_hnm_frames.py NAME.HNM [maxframes] [log|""] [frame ...] -> hnm-NAME-N.png in cwd."""
import sys,struct
import os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from dune_sprite_sheet import dat,unhsq,png
def le16(d,o): return d[o]|d[o+1]<<8
def palette(d,o,pal):
    while le16(d,o)!=0xFFFF:
        beg=d[o]; cnt=d[o+1] or 256; o+=2
        if cnt==1 and beg==0: o+=3; continue
        for i in range(cnt*3): pal[beg*3+i]=d[o+i]
        o+=cnt*3
    return o+2
def codebook(d,o,n,base):
    out=bytearray(); flip=0; q=0
    while len(out)<n:
        tag=d[o]; o+=1
        if tag&0x80:
            if not flip: q=d[o]; o+=1; ln=q>>4
            else: ln=q&15
            flip^=1
            ofs=((tag<<1)|(ln&1))&0xff; ln=(ln>>1)+2
            for _ in range(ln): out.append(out[len(out)-ofs-1])
        else:
            out.append((tag+base)&0xff if tag else 0)
    return out,o
def adunpack(d,o):
    fs,cs,flags=le16(d,o),le16(d,o+2),d[o+4]; o+=6
    x=y=0
    if not flags&4: x,y=le16(d,o),le16(d,o+2); o+=4
    cb,o=codebook(d,o,cs,0x80 if flags&0x40 else 0)
    st={'q':0x8000,'o':o,'flip':0,'n':0}
    def bit():
        if st['q']==0x8000:
            w=le16(d,st['o']); st['o']+=2; st['q']=((w<<1)|1)&0xffff; return w>>15
        b=st['q']>>15; st['q']=(st['q']<<1)&0xffff; return b
    def longlen():
        if not st['flip']: st['n']=d[st['o']]; st['o']+=1; ln=st['n']>>4
        else: ln=st['n']&15
        st['flip']^=1
        if not ln: ln=d[st['o']]+16; st['o']+=1
        return ln+4
    out=bytearray(); t=0; alt=flags&0x80
    while True:
        while not bit(): out.append(cb[t]); t+=1
        c=cb[t]; t+=1
        if not alt:
            if not bit(): n=2
            elif not bit(): n=3
            elif not bit(): n=4
            else:
                if len(out)>=fs: break
                n=longlen()
        else:
            if not bit(): n=longlen()
            elif not bit(): n=2
            elif not bit(): n=3
            else:
                if len(out)>=fs: break
                n=4
        out+=bytes([c])*n
    return out,x,y,fs,cs,flags
def decode(data,maxframes=9999,log=False):
    pal=bytearray(768); screen=bytearray(320*200); frames=[]; o=0; index=0; snd=0
    while o+2<=len(data) and len(frames)<maxframes:
        cl=le16(data,o)
        if cl==0: break
        ch=data[o+2:o+cl]
        if index==0:
            e=palette(ch,0,pal)
            if log: print('header chunk',cl,'pal end',e)
        else:
            p=0
            while p+4<=len(ch):
                tag=ch[p:p+2]; sz=le16(ch,p+2)
                if tag in(b'pt',b'kl',b'sd',b'pl',b'mm'):
                    if tag==b'pl': palette(ch,p+4,pal)
                    if tag==b'sd': snd+=sz-4
                    if log: print(' ',tag,sz)
                    p+=sz; continue
                w=le16(ch,p)&0x1ff; fl=le16(ch,p)>>9; h=ch[p+2]; mode=ch[p+3]
                info=''
                if h:
                    word=le16(ch,p); body=ch[p+4:]
                    if word&0x200: body=unhsq(body)
                    x=y=0
                    if not word&0x400: x,y=le16(body,0),le16(body,2); body=body[4:]
                    if word&0x8000:
                        pix=bytearray(); q=0
                        for _ in range(h):
                            n=0
                            while n<w:
                                c=body[q]; q+=1
                                if c&0x80: k=257-c; row=bytes([body[q]])*k; q+=1
                                else: k=c+1; row=body[q:q+k]; q+=k
                                pix+=row; n+=k
                            if n>w: del pix[len(pix)-(n-w):]
                        info='RLE used %d of %d'%(q,len(body))
                    else: pix=body; info='raw %d vs %d'%(len(body),w*h)
                    info+=' at %d,%d'%(x,y)
                    for yy in range(h):
                        for xx in range(w):
                            i=yy*w+xx
                            if i<len(pix) and (mode!=0xff or pix[i]) and y+yy<200 and x+xx<320: screen[(y+yy)*320+x+xx]=pix[i]
                if log: print(' frame',len(frames),w,h,'flags',fl,'mode',hex(mode),info)
                frames.append((bytes(screen),bytes(pal)))
                break
        index+=1; o+=cl
    return frames,snd
if __name__=='__main__':
    f=dat(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','DUNE.DAT')); name=sys.argv[1]; d=f[name]
    print(name,len(d),'bytes')
    frames,snd=decode(d,int(sys.argv[2]) if len(sys.argv)>2 else 9999,log=len(sys.argv)>3)
    print(len(frames),'frames, sound bytes',snd)
    for i in [int(a) for a in sys.argv[4:]] if len(sys.argv)>4 else [len(frames)-1]:
        s,pal=frames[i]; rgb=bytearray()
        for c in s: rgb+=bytes(((pal[c*3+k]&63)<<2)|((pal[c*3+k]&63)>>4) for k in range(3))
        png('hnm-%s-%d.png'%(name[:-4],i),320,200,rgb)

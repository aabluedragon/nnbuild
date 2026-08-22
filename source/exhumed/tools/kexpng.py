"""Minimal PNG reader/writer (8-bit, all colour types) -- no third-party deps.

Only what the Kex menu pipeline needs: decode an image to RGBA rows, crop it,
box-downsample it, and write it back out as RGBA8.
"""
import struct, zlib

def _paeth(a,b,c):
    p=a+b-c; pa=abs(p-a); pb=abs(p-b); pc=abs(p-c)
    return a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)

def read(path):
    d=open(path,'rb').read()
    assert d[:8]==b'\x89PNG\r\n\x1a\n', path
    pos=8; idat=b''; pal=None; trns=None
    w=h=bd=ct=0
    while pos < len(d):
        ln,typ = struct.unpack_from('>I4s', d, pos); pos+=8
        body=d[pos:pos+ln]; pos+=ln+4
        if typ==b'IHDR':
            w,h,bd,ct,comp,filt,interlace=struct.unpack('>IIBBBBB', body)
            assert bd==8 and interlace==0, (path,bd,interlace)
        elif typ==b'PLTE': pal=body
        elif typ==b'tRNS': trns=body
        elif typ==b'IDAT': idat+=body
        elif typ==b'IEND': break
    raw=zlib.decompress(idat)
    nch={0:1,2:3,3:1,4:2,6:4}[ct]
    stride=w*nch
    out=bytearray(h*stride)
    prev=bytearray(stride)
    p=0
    for y in range(h):
        f=raw[p]; p+=1
        line=bytearray(raw[p:p+stride]); p+=stride
        if f==1:
            for i in range(nch,stride): line[i]=(line[i]+line[i-nch])&255
        elif f==2:
            for i in range(stride): line[i]=(line[i]+prev[i])&255
        elif f==3:
            for i in range(stride):
                a=line[i-nch] if i>=nch else 0
                line[i]=(line[i]+((a+prev[i])>>1))&255
        elif f==4:
            for i in range(stride):
                a=line[i-nch] if i>=nch else 0
                c=prev[i-nch] if i>=nch else 0
                line[i]=(line[i]+_paeth(a,prev[i],c))&255
        out[y*stride:(y+1)*stride]=line
        prev=line
    # expand to RGBA
    rgba=bytearray(w*h*4)
    for i in range(w*h):
        if ct==6:   rgba[i*4:i*4+4]=out[i*4:i*4+4]
        elif ct==2: rgba[i*4:i*4+3]=out[i*3:i*3+3]; rgba[i*4+3]=255
        elif ct==0: v=out[i]; rgba[i*4:i*4+3]=bytes((v,v,v)); rgba[i*4+3]=255
        elif ct==4: v=out[i*2]; rgba[i*4:i*4+3]=bytes((v,v,v)); rgba[i*4+3]=out[i*2+1]
        elif ct==3:
            idx=out[i]; rgba[i*4:i*4+3]=pal[idx*3:idx*3+3]
            rgba[i*4+3]=trns[idx] if (trns and idx<len(trns)) else 255
    return w,h,rgba

def write(path, w, h, rgba):
    raw=bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y*w*4:(y+1)*w*4]
    z=zlib.compress(bytes(raw), 9)
    def chunk(t, b):
        c=struct.pack('>I',len(b))+t+b
        return c+struct.pack('>I', zlib.crc32(t+b)&0xffffffff)
    out=b'\x89PNG\r\n\x1a\n'
    out+=chunk(b'IHDR', struct.pack('>IIBBBBB', w,h,8,6,0,0,0))
    out+=chunk(b'IDAT', z)
    out+=chunk(b'IEND', b'')
    open(path,'wb').write(out)

def crop(w,h,rgba, x,y,cw,ch):
    out=bytearray(cw*ch*4)
    for j in range(ch):
        sy=y+j
        if sy<0 or sy>=h: continue
        s=(sy*w+x)*4; dlen=min(cw, w-x)*4
        out[j*cw*4:j*cw*4+dlen]=rgba[s:s+dlen]
    return out

def downsample(w,h,rgba, nw,nh):
    out=bytearray(nw*nh*4)
    for j in range(nh):
        y0=j*h//nh; y1=max(y0+1,(j+1)*h//nh)
        for i in range(nw):
            x0=i*w//nw; x1=max(x0+1,(i+1)*w//nw)
            r=g=b=a=n=0
            for y in range(y0,y1):
                base=(y*w)*4
                for x in range(x0,x1):
                    p=base+x*4
                    r+=rgba[p]; g+=rgba[p+1]; b+=rgba[p+2]; a+=rgba[p+3]; n+=1
            p=(j*nw+i)*4
            out[p]=r//n; out[p+1]=g//n; out[p+2]=b//n; out[p+3]=a//n
    return out

def resize(w,h,rgba, nw,nh):
    """Box-average when shrinking, bilinear when growing. Good enough for UI art."""
    if nw <= w and nh <= h:
        return downsample(w,h,rgba,nw,nh)
    out=bytearray(nw*nh*4)
    for j in range(nh):
        fy=(j+0.5)*h/nh-0.5
        y0=max(0,min(h-1,int(fy))); y1=min(h-1,y0+1); ty=max(0.0,min(1.0,fy-y0))
        for i in range(nw):
            fx=(i+0.5)*w/nw-0.5
            x0=max(0,min(w-1,int(fx))); x1=min(w-1,x0+1); tx=max(0.0,min(1.0,fx-x0))
            p=(j*nw+i)*4
            for c in range(4):
                a=rgba[(y0*w+x0)*4+c]; b=rgba[(y0*w+x1)*4+c]
                cc=rgba[(y1*w+x0)*4+c]; d=rgba[(y1*w+x1)*4+c]
                top=a+(b-a)*tx; bot=cc+(d-cc)*tx
                out[p+c]=int(top+(bot-top)*ty+0.5)
    return out

def tint(w,h,rgba, r,g,b, amount=1.0):
    """Multiply toward a colour, keeping the art's own shading and alpha."""
    out=bytearray(rgba)
    for i in range(w*h):
        p=i*4
        for c,t in ((0,r),(1,g),(2,b)):
            v=out[p+c]
            out[p+c]=int(v*(1.0-amount) + (v*t//255)*amount)
    return out

def blend(w,h,rgba, r,g,b, amount=0.65):
    """Blend toward a colour instead of multiplying by it.

    Multiplying the stone plate by blue gives a near-black plate, because the
    stone is already dark and warm; blending keeps the plate's grain but lands
    on the colour actually asked for.
    """
    out=bytearray(rgba)
    for i in range(w*h):
        p=i*4
        if out[p+3]==0: continue
        for c,t in ((0,r),(1,g),(2,b)):
            out[p+c]=int(out[p+c]*(1.0-amount) + t*amount)
    return out

def solid(w,h, r,g,b,a=255):
    return bytearray(bytes((r,g,b,a))*(w*h))

def swap_fill_outline(w,h,rgba, fill=(245,170,45), outline=(20,20,60)):
    """Recolour the Kex display font from blue-on-gold to gold-on-dark.

    bigfont.png ships with a blue letter body and a gold outline, which is what
    the remaster's save-slot labels use. Its menu entries are the other way
    round -- gold body, dark outline -- and Kex produces that at draw time. The
    two live on the same glyph, so the split is by hue: cool pixels are the
    body, warm pixels are the outline.

    The colours are assigned flat rather than scaled by luminance. Scaling was
    the obvious thing to try and it is wrong here: blue carries almost no
    luminance, so every body pixel came out as dark mud. The glyphs already
    carry their anti-aliasing in the alpha channel, which is left untouched.
    """
    out=bytearray(rgba)
    for i in range(w*h):
        p=i*4
        if out[p+3]==0: continue
        tgt = fill if out[p+2] > out[p] else outline
        out[p]=tgt[0]; out[p+1]=tgt[1]; out[p+2]=tgt[2]
    return out

def read_dds_dxt1(path, block_average=True):
    """Decode a DXT1 .dds to RGBA.

    The remaster's overworld is a 6000x3072 DXT1 texture -- 1.4 million blocks,
    which is more than a pure-Python per-pixel decode wants to chew through, and
    far more resolution than a Build tile needs. Each DXT1 block already stores
    two endpoint colours and 2-bit weights, so averaging the four texels of a
    block is a couple of lookups; doing that gives a correctly filtered
    quarter-size image in one pass and skips the full decode entirely.
    """
    d = open(path, 'rb').read()
    if d[:4] != b'DDS ':
        raise ValueError('not a DDS: %s' % path)
    h, w = struct.unpack_from('<II', d, 12)
    if d[84:88] != b'DXT1':
        raise ValueError('only DXT1 is supported (%s has %r)' % (path, d[84:88]))
    off = 4 + struct.unpack_from('<I', d, 4)[0]
    bw, bh = (w + 3) // 4, (h + 3) // 4
    if not block_average:
        raise NotImplementedError
    out = bytearray(bw * bh * 4)
    for by in range(bh):
        row = by * bw
        for bx in range(bw):
            p = off + (row + bx) * 8
            c0, c1, bits = struct.unpack_from('<HHI', d, p)
            r0 = ((c0 >> 11) & 31) * 255 // 31; g0 = ((c0 >> 5) & 63) * 255 // 63; b0 = (c0 & 31) * 255 // 31
            r1 = ((c1 >> 11) & 31) * 255 // 31; g1 = ((c1 >> 5) & 63) * 255 // 63; b1 = (c1 & 31) * 255 // 31
            if c0 > c1:
                pal = ((r0,g0,b0), (r1,g1,b1),
                       ((2*r0+r1)//3, (2*g0+g1)//3, (2*b0+b1)//3),
                       ((r0+2*r1)//3, (g0+2*g1)//3, (b0+2*b1)//3))
            else:
                pal = ((r0,g0,b0), (r1,g1,b1),
                       ((r0+r1)//2, (g0+g1)//2, (b0+b1)//2), (0,0,0))
            r = g = b = 0
            for i in range(16):
                c = pal[(bits >> (i*2)) & 3]
                r += c[0]; g += c[1]; b += c[2]
            q = (row + bx) * 4
            out[q] = r // 16; out[q+1] = g // 16; out[q+2] = b // 16; out[q+3] = 255
    return bw, bh, out

def blank(w,h):
    return bytearray(w*h*4)

def paste(dw,dh,dst, sw,sh,src, ox,oy):
    """Alpha-over one image onto another at (ox,oy)."""
    for y in range(sh):
        dy=oy+y
        if dy<0 or dy>=dh: continue
        for x in range(sw):
            dx=ox+x
            if dx<0 or dx>=dw: continue
            s=(y*sw+x)*4; a=src[s+3]
            if a==0: continue
            d=(dy*dw+dx)*4
            if a==255:
                dst[d:d+4]=src[s:s+4]
            else:
                ia=255-a
                for c in range(3):
                    dst[d+c]=(src[s+c]*a + dst[d+c]*ia)//255
                dst[d+3]=min(255, dst[d+3]+a)
    return dst

#!/usr/bin/env python3
"""Research decoder for Sega CD Dune bitmap sets: word 0 = offset of a word
table (2), whose first entry also gives its length; each entry points to a
bitmap: word width, word height, then the pixels, 4 bits each, high nibble
first, stored column-major in columns one byte (two pixels) wide: byte
(x / 2) * height + y (the word-RAM dot-image order). Verified on 1928, the
map's 60 x 60 portraits."""
import sys, struct
from PIL import Image

def parse(d):
    t = struct.unpack_from('>H', d, 0)[0]
    n = struct.unpack_from('>H', d, t)[0] // 2
    out = []
    for i in range(n):
        o = t + struct.unpack_from('>H', d, t + 2 * i)[0]
        w, h = struct.unpack_from('>HH', d, o)
        cols = (w + 1) // 2
        p = d[o + 4:o + 4 + cols * h]
        rows = [[0] * w for _ in range(h)]
        for c in range(cols):
            for y in range(h):
                i = c * h + y
                if i >= len(p): continue
                v = p[i]
                rows[y][2 * c] = v >> 4
                if 2 * c + 1 < w: rows[y][2 * c + 1] = v & 15
        out.append((w, h, rows))
    return out

def sheet(bitmaps, scale=2, cols=8):
    cw = max(w for w, h, _ in bitmaps) + 4; ch = max(h for w, h, _ in bitmaps) + 4
    img = Image.new('L', (cols * cw, ((len(bitmaps) + cols - 1) // cols) * ch), 40)
    px = img.load()
    for i, (w, h, rows) in enumerate(bitmaps):
        ox, oy = (i % cols) * cw, (i // cols) * ch
        for y in range(h):
            for x in range(w):
                px[ox + x, oy + y] = rows[y][x] * 17
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    b = parse(d)
    print(len(b), [(w, h) for w, h, _ in b][:40])
    sheet(b).save(sys.argv[2])

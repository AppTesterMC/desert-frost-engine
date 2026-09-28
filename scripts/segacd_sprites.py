#!/usr/bin/env python3
"""Research decoder for Sega CD Dune sprite banks (files 1782-1850 and others).

Hypothesis under test (see FINDINGS.md): +0 tile block offset T; +2 frame
table offset F; +4..+9 unknown; +0xa 15 CRAM colours (colour 0 transparent),
0xffff; F: word offsets (relative to F) of the frames; a frame is one word
(anchor / size, not decoded) then pieces of two words: byte x (pixels),
byte (y in tiles << 4 | VDP size hs<<2|vs), word VDP attribute
(palette, flips, tile). T: word tile count, then 32-byte 4bpp tiles."""
import sys, struct
from PIL import Image

def cram(w):
    return tuple(((w >> s) & 7) * 255 // 7 for s in (1, 5, 9))

def parse(d):
    T, F = struct.unpack_from('>HH', d, 0)
    pal = [(0, 0, 0)] + [cram(struct.unpack_from('>H', d, 0xa + 2 * i)[0]) for i in range(15)]
    offs = []
    p = F
    first = struct.unpack_from('>H', d, F)[0]
    n = first // 2
    offs = [F + struct.unpack_from('>H', d, F + 2 * i)[0] for i in range(n)]
    ends = offs[1:] + [T]
    frames = []
    for a, e in zip(offs, ends):
        head = struct.unpack_from('>H', d, a)[0]
        pieces = []
        for q in range(a + 2, e - 3, 4):
            x, ys, attr = d[q], d[q + 1], struct.unpack_from('>H', d, q + 2)[0]
            pieces.append((x, ys >> 4, (ys >> 2) & 3, ys & 3, attr))
        frames.append((head, pieces))
    ntiles = struct.unpack_from('>H', d, T)[0]
    tiles = d[T + 2:T + 2 + 32 * ntiles]
    return pal, frames, tiles, ntiles

def draw(pal, pieces, tiles):
    img = Image.new('RGBA', (256, 128), (40, 40, 60, 255)); px = img.load()
    for x, yt, hs, vs, attr in pieces:
        w, h = hs + 1, vs + 1; t0 = attr & 0x7ff
        hf, vf = attr & 0x800, attr & 0x1000
        for cx in range(w):
            for cy in range(h):
                t = t0 + cx * h + cy  # VDP sprites are column-major
                tb = tiles[32 * t: 32 * t + 32]
                if len(tb) < 32: continue
                for yy in range(8):
                    for xx in range(8):
                        b = tb[yy * 4 + xx // 2]; c = b >> 4 if xx % 2 == 0 else b & 15
                        if not c: continue
                        X = x + ((w - 1 - cx) * 8 + 7 - xx if hf else cx * 8 + xx)
                        Y = yt * 8 + ((h - 1 - cy) * 8 + 7 - yy if vf else cy * 8 + yy)
                        if 0 <= X < 256 and 0 <= Y < 128: px[X, Y] = pal[c] + (255,)
    return img

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    pal, frames, tiles, n = parse(d)
    print('frames', len(frames), 'tiles', n, 'extra bytes', len(d) - (struct.unpack_from('>H', d, 0)[0] + 2 + 32 * n))
    cols = 4
    sheet = Image.new('RGBA', (256 * cols, 128 * ((len(frames) + cols - 1) // cols)), (0, 0, 0, 255))
    for i, (head, pieces) in enumerate(frames):
        sheet.paste(draw(pal, pieces, tiles), ((i % cols) * 256, (i // cols) * 128))
    sheet.save(sys.argv[2])

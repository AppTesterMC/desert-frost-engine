#!/usr/bin/env python3
"""Reference decoder for Sega CD Dune tile screens (research tool, mirrors
engines/dune/segacd_gfx.cpp).

Screen file layout (big-endian), found by inspection of DUNE.DAT (see FINDINGS.md):
  +0    word  offset T of the tile block
  +2    64 words: four CRAM lines of 16 colours (0000 BBB0 GGG0 RRR0)
  +0x82 word  L = length of the plane list that starts here (2: one plane,
              4: two planes; the second word is plane 2's offset from 0x82)
  +0x82+L     plane 1: byte width, byte height (tiles), then w*h name-table words
  plane 2 (if any) at 0x82 + word(0x84): same layout
  +T    word  tile count, then count * 32 bytes of 4bpp 8x8 tiles
Name-table words are the VDP's: priority 0x8000, palette 0x6000, vflip 0x1000,
hflip 0x0800, tile 0x07ff.
"""
import sys, struct
from PIL import Image

def cram(w):
    return tuple(((w >> s) & 7) * 255 // 7 for s in (1, 5, 9))

def parse(d):
    toff = struct.unpack_from('>H', d, 0)[0]
    pal = [cram(struct.unpack_from('>H', d, 2 + 2 * i)[0]) for i in range(64)]
    L = struct.unpack_from('>H', d, 0x82)[0]
    offs = [0x82 + L] + [0x82 + struct.unpack_from('>H', d, 0x82 + 2 + 2 * k)[0] for k in range((L - 2) // 2)]
    planes = []
    for o in offs:
        w, h = d[o], d[o + 1]
        planes.append((w, h, [struct.unpack_from('>H', d, o + 2 + 2 * i)[0] for i in range(w * h)]))
    n = struct.unpack_from('>H', d, toff)[0]
    return pal, planes, d[toff + 2: toff + 2 + 32 * n]

def valid(d):
    try:
        toff = struct.unpack_from('>H', d, 0)[0]
        pal, planes, tiles = parse(d)
        return all(w == 40 and 0 < h <= 32 for w, h, _ in planes) and len(tiles) % 32 == 0 and len(tiles) > 0
    except Exception:
        return False

def draw_plane(px, pal, tiles, plane, prio, first_tile=0):
    w, h, nt = plane
    for ty in range(h):
        for tx in range(w):
            e = nt[ty * w + tx]
            if bool(e & 0x8000) != prio: continue
            idx = (e & 0x7ff) - first_tile; line = (e >> 13) & 3
            t = tiles[idx * 32: idx * 32 + 32]
            if len(t) < 32: continue
            for y in range(8):
                for x in range(8):
                    b = t[y * 4 + x // 2]; c = (b >> 4) if x % 2 == 0 else b & 15
                    if c == 0: continue
                    X = 7 - x if e & 0x800 else x; Y = 7 - y if e & 0x1000 else y
                    px[tx * 8 + X, ty * 8 + Y] = pal[line * 16 + c]

def render(files):
    """files: back-to-front list of screen byte strings (e.g. companion, room)."""
    layers = [parse(d) for d in files]
    h = max(p[1] for l in layers for p in l[1]) * 8
    img = Image.new('RGB', (320, h), layers[-1][0][0])
    px = img.load()
    # VDP order: backdrop, low-priority B, low A, high B, high A. Here each file's
    # plane list is back (last) to front (first); files are back to front.
    order = [(l, p) for l in layers for p in reversed(l[1])]
    for prio in (False, True):
        for (pal, _, tiles), plane in order:
            draw_plane(px, pal, tiles, plane, prio)
    return img

if __name__ == '__main__':
    render([open(p, 'rb').read() for p in sys.argv[2:]]).save(sys.argv[1])

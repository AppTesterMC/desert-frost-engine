#!/usr/bin/env python3
"""Identify Sega CD portrait banks and measure where their frames sit, by
masked template matching of each bank's frames against 320x224 video frames
(research tool; the program's own placement table is not decoded yet).
Usage: segacd_portrait_match.py <files dir> <frames dir> identify|place <bank> <shot>"""
import sys, os, struct
import numpy as np, cv2
sys.path.insert(0, os.path.dirname(__file__))
from segacd_sprites import parse, draw

BG = (40, 40, 60)

def frames(path):
    d = open(path, 'rb').read(); pal, fr, tiles, n = parse(d)
    out = []
    for i, (h, pcs) in enumerate(fr):
        if not pcs: continue
        im = np.asarray(draw(pal, pcs, tiles))[:, :, :3].astype(np.float32)
        mask = (im != BG).any(2)
        ys, xs = np.nonzero(mask)
        if len(xs) < 30: continue
        y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        out.append((i, h, im[y0:y1, x0:x1], mask[y0:y1, x0:x1].astype(np.float32), x0, y0, int(mask.sum())))
    return out

def best(ref, spr, m):
    if spr.shape[0] > ref.shape[0] or spr.shape[1] > ref.shape[1]: return None
    r = cv2.matchTemplate(ref, spr, cv2.TM_SQDIFF, mask=np.dstack([m] * 3))
    r[~np.isfinite(r)] = 1e18
    v, _, loc, _ = cv2.minMaxLoc(r)
    return v / (m.sum() * 3), loc

if __name__ == '__main__':
    files, shots, mode = sys.argv[1:4]
    if mode == 'identify':
        names = sys.argv[4].split(',')
        refs = {n: np.asarray(cv2.cvtColor(cv2.imread(os.path.join(shots, f'{n}_320.png')), cv2.COLOR_BGR2RGB)).astype(np.float32) for n in names}
        for bank in range(1782, 1851):
            fr = frames(os.path.join(files, f'{bank:04d}.bin'))
            if not fr: continue
            big = max(fr, key=lambda f: f[6])
            scores = sorted(((best(refs[n], big[2], big[3]) or (1e9, 0))[0], n) for n in names)
            print(bank, 'frame', big[0], ' '.join(f'{n}:{s:.0f}' for s, n in scores[:3]))
    else:
        bank, shot = int(sys.argv[4]), sys.argv[5]
        ref = np.asarray(cv2.cvtColor(cv2.imread(os.path.join(shots, f'{shot}_320.png')), cv2.COLOR_BGR2RGB)).astype(np.float32)
        for i, h, spr, m, x0, y0, n in frames(os.path.join(files, f'{bank:04d}.bin')):
            b = best(ref, spr, m)
            if b: print(f'{bank} frame {i:2d} {h:04x} {spr.shape[1]}x{spr.shape[0]} err {b[0]:7.0f} origin {b[1][0] - x0} {b[1][1] - y0}')

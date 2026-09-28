#!/usr/bin/env python3
"""Build measured rest poses for Sega CD portraits: per bank, the frames that
match a video frame well at one place (best of each same-size slot), largest
first; render the composite beside the video frame and print a C++ table."""
import sys, os
import numpy as np, cv2
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
from segacd_portrait_match import frames, best
from segacd_sprites import parse, draw

def pose(files, shots, bank, shot, limit=1400):
    ref = np.asarray(cv2.cvtColor(cv2.imread(os.path.join(shots, f'{shot}_320.png')), cv2.COLOR_BGR2RGB)).astype(np.float32)
    rows = []
    for i, h, spr, m, x0, y0, n in frames(os.path.join(files, f'{bank:04d}.bin')):
        b = best(ref, spr, m)
        if b and b[0] < limit and n >= 40: rows.append((b[0], i, b[1][0] - x0, b[1][1] - y0, spr.shape[1] * spr.shape[0]))
    chosen = {}
    for err, i, x, y, area in sorted(rows):
        key = (x // 4, y // 4, area // 64)
        if key not in chosen: chosen[key] = (area, i, x, y, err)
    parts = sorted(chosen.values(), key=lambda c: -c[0])
    # Second pass: holes (a mouth or jaw whose rest frame the video does not
    # show) take the best-placed remaining frame that mostly lands on
    # uncovered pixels inside the composite's box.
    cover = np.zeros((224, 320), bool)
    fr = {f[0]: f for f in frames(os.path.join(files, f'{bank:04d}.bin'))}
    def stamp(i, x, y):
        _, _, spr, m, x0, y0, _ = fr[i]
        for yy, xx in zip(*np.nonzero(m)):
            X, Y = x + x0 + xx, y + y0 + yy
            if 0 <= X < 320 and 0 <= Y < 224: cover[Y, X] = True
    for a, i, x, y, e in parts: stamp(i, x, y)
    ys, xs = np.nonzero(cover)
    if len(xs):
        box = (xs.min(), ys.min(), xs.max(), ys.max())
        used = {c[1] for c in parts}
        extra = []
        for i, h, spr, m, x0, y0, n in fr.values():
            if i in used or n < 20: continue
            b = best(ref, spr, m)
            if not b or b[0] > 4000: continue
            x, y = b[1][0] - x0, b[1][1] - y0
            pts = [(x + x0 + xx, y + y0 + yy) for yy, xx in zip(*np.nonzero(m))]
            inside = [(X, Y) for X, Y in pts if box[0] <= X <= box[2] and box[1] <= Y <= box[3] and 0 <= X < 320 and 0 <= Y < 224]
            if len(inside) < 0.9 * len(pts): continue
            empty = sum(1 for X, Y in inside if not cover[Y, X])
            if empty > 0.5 * len(pts): extra.append((b[0], i, x, y, spr.shape[0] * spr.shape[1]))
        for err, i, x, y, area in sorted(extra):
            pts_empty = True
            parts.append((area, i, x, y, err)); stamp(i, x, y)
    return parts, ref

def render(files, bank, parts):
    d = open(os.path.join(files, f'{bank:04d}.bin'), 'rb').read(); pal, fr, tiles, n = parse(d)
    img = Image.new('RGB', (320, 224), (60, 0, 60))
    for area, i, x, y, err in parts:
        f = draw(pal, fr[i][1], tiles); a = np.asarray(f)[:, :, :3]
        mask = Image.fromarray(((a != (40, 40, 60)).any(2) * 255).astype(np.uint8))
        img.paste(f.convert('RGB'), (x, y), mask)
    return img

if __name__ == '__main__':
    files, shots = sys.argv[1], sys.argv[2]
    out = []
    sheet = []
    for line in sys.stdin:
        if not line.strip(): continue
        who, bank, shot = line.split()
        parts, ref = pose(files, shots, int(bank), shot)
        comp = render(files, int(bank), parts)
        pair = Image.new('RGB', (640, 224)); pair.paste(comp, (0, 0)); pair.paste(Image.fromarray(ref.astype(np.uint8)), (320, 0))
        sheet.append(pair)
        out.append(f'\t{{ {who}, {bank}, {{ ' + ', '.join(f'{{ {i}, {x}, {y} }}' for a, i, x, y, e in parts[:8]) + ' } },')
    S = Image.new('RGB', (640, 224 * len(sheet)))
    for k, p in enumerate(sheet): S.paste(p, (0, 224 * k))
    S.save(sys.argv[3])
    print('\n'.join(out))

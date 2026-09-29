#!/usr/bin/env python3
"""The original's globe renderer, for checking SEE RESULTS and STANDARD VISION.

A transcription of the floppy DUNEVGA driver's globe routine (DUNEVGA CS:1B90
vga_globe_init, CS:1BE1 vga_globe_setup, colours at CS:1D26 and CS:1DA3; the CD
DNVGA has the same code at 1CB6 / 1D07 / 1E4C / 1EC9). It reads the tables the
game keeps in its data segment (floppy: TABLAT at ds:448F with the rotation
phase at ds:4493, GLOBDATA right after it, the tilt window table at
ds:86BE, the results flag at ds:FEF2) and the live map, so it draws exactly the
pixels the original drew for an autoplay checkpoint taken with --memdump-full.

Usage:
  globe_ref.py check <checkpoint> [...]      compare with the captured PNG
  globe_ref.py render <checkpoint> <out.png> [--mode results|standard]
  globe_ref.py extract <checkpoint>          write <checkpoint>.map.bin from .mem.bin
  globe_ref.py expect <map.bin> <phase> <tilt> results|standard <out.bin> <data dir>
      the 320x200 palette indices the original draws for that map, phase
      (0..397) and tilt (-98..98; negative = the north towards the viewer),
      0xFF outside the globe; the tables come from TABLAT.BIN and GLOBDATA.HSQ
      in <data dir>. For the engine's regression check.
  globe_ref.py engine <globe dump dir> <checkpoint dir> <data dir>
      the engine's dune_globe_dump output (globe-NNN.idx/.map/.txt): every
      globe it drew is compared with this renderer at its own phase, tilt and
      mode (0 differing pixels expected), and each checkpoint PNG is paired
      with the last globe drawn before it. Prints one line per checkpoint:
      name phase tilt mode differing-pixels atreides-cells harkonnen-cells
      sprouting-cells greens(0x20-0x23) reds(0x24-0x2F) blues(0x34-0x3F).

<checkpoint> is the path without extension: <name>.ds.bin, <name>.pal, <name>.png
and either <name>.map.bin or <name>.mem.bin must exist. The screen shows the
pass drawn with the phase before the one in ds:4493 (the frame task advances
the phase after presenting), so `check` tries phases -8..+2 and reports the best.
"""
import os
import struct
import sys

from PIL import Image

T = 0x448F           # floppy RESOURCE_TABLAT (DUNEPRG CS:B876 mov bp, 448Fh)
RESULTS_FLAG = 0xFEF2
MAP_PTR = 0xFEEE     # far pointer to the map's centre cell (offset 0x62FC)
MAP_CENTRE = 0x62FC
MAP_SIZE = 50681


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    return v - 0x100 if v & 0x80 else v


def colour(v, results):
    """The palette index of one map cell (bits 0-3 terrain, bits 4-5 stage)."""
    t, stage = v & 0x0F, v & 0x30
    if results:                       # CS:1DA3 (CD 1EC9)
        if stage == 0:
            return 0x10 + t           # neutral: the terrain ramp
        if stage == 0x30:
            return 0x30 + t           # Harkonnen: the blue ramp
        return 0x20 + t               # 0x10 sprouting / 0x20 Atreides: the red ramp
    if stage == 0x10 and t < 8:       # CS:1D26 (CD 1E4C): sprouting sand turns green
        t += 12
    return 0x10 + t


def draw(ds, mapbytes, results, phase=None):
    """{screen offset: palette index} for every globe pixel, as the driver stores them."""
    ds = bytearray(ds)
    if phase is not None:             # recalculate_globe_rotation_table (CD seg000:B9F6)
        struct.pack_into('<HH', ds, T + 4, phase, 0)
        bx = ((phase << 16) + 0x8000) // 398
        for i in range(1, 99):
            ln = struct.unpack_from('<H', ds, T + i * 8 + 2)[0]
            fp = (2 * bx * ln) & 0xFFFFFFFF
            struct.pack_into('<HH', ds, T + i * 8 + 4, fp >> 16, fp & 0xFFFF)
    sel = T + 0x319 + 0xCD9          # per-x column tables (row selector, cell offset)
    tw = sel + 0x3301                # tilt window table, middle entry
    out = {}

    def pixel(b, si, mirror):
        ax = -b if mirror else b
        w = s16(struct.unpack_from('<H', ds, (tw + 2 * ax) & 0xFFFF)[0])
        al = s8(w & 0xFF)
        far = al < 0
        bp = -al if far else al
        bl = ds[bp + si]
        a = ds[bp + si + 0x64]
        e = T + bl * 4
        row_start = s16(struct.unpack_from('<H', ds, e)[0])
        length = struct.unpack_from('<H', ds, e + 2)[0]
        dx = s16(struct.unpack_from('<H', ds, e + 4)[0])
        if far:
            a = length - a
        if w < 0:                     # northern sections
            row_start = -row_start
        cells = length * 2
        p = dx - a
        if p < 0:
            p += cells
        west = mapbytes[(p + row_start + MAP_CENTRE) & 0xFFFF]
        dx += a
        p = dx - cells
        if p < 0:
            p += cells
        east = mapbytes[(p + row_start + MAP_CENTRE) & 0xFFFF]
        return west, east

    def half(di, pos, step, mirror):
        while True:
            b = ds[di]
            di += 1
            if b & 0x80:
                return
            wcur, ecur, si = pos - 1, pos, sel
            while True:
                w, e = pixel(b, si, mirror)
                out[wcur] = colour(w, results)
                out[ecur] = colour(e, results)
                wcur -= 1
                ecur += 1
                si += 0xC8
                b = ds[di]
                di += 1
                if b & 0x80:
                    break
            pos += step

    half(T + 0x319, 0x6360, -320, False)                     # row 79 upwards
    half(T + 0x319 - s8(ds[T + 0x318]), 0x64A0, 320, True)  # row 80 downwards
    return out


TILT_TABLE = 0x86BE  # floppy globe_tilt_window_table (196 words; middle = T + 0x42F3)
TILT = 0x297C        # floppy globe tilt word (CD _word_21910)


def tilt_table(ds, tilt):
    """build_globe_tilt_window_table (floppy CS:B92C, CD seg000:BA2D) for a tilt."""
    words = []
    a = tilt + 98
    if a > 98:                        # far south first: tilt - 99 down to -98
        al = tilt - 98
        while True:
            al -= 1
            words.append((0, al))
            if al <= -98:
                break
        a = 98
    while True:                       # near south: a down to 0
        words.append((0, a))
        a -= 1
        if a < 0:
            break
    a = 1
    while len(words) < 196 and a <= 98:   # near north 1..98
        words.append((0xFF, a))
        a += 1
    a = -98
    while len(words) < 196:           # far north: 98, 97, ... (stored negative)
        words.append((0xFF, a))
        a += 1
    for i, (hi, lo) in enumerate(words[:196]):
        struct.pack_into('<BB', ds, TILT_TABLE + 2 * i, lo & 0xFF, hi)
    struct.pack_into('<h', ds, TILT, tilt)


def synth(tablat, globdata, tilt, phase=0):
    """A data segment image with the globe tables as the game builds them from
    TABLAT.BIN (big-endian words, byte-swapped in memory) and GLOBDATA.HSQ."""
    ds = bytearray(0x10000)
    for i in range(99):
        off, ln = struct.unpack_from('>HH', tablat, i * 8)
        struct.pack_into('<HH', ds, T + i * 8, off, ln)
    ds[T + 0x318:T + 0x318 + len(globdata)] = globdata
    tilt_table(ds, tilt)
    struct.pack_into('<H', ds, T + 4, phase)
    return ds


def rgb(pal, c):
    return tuple(round(v * 255 / 63) for v in pal[3 * c:3 * c + 3])


def same(p, q):
    return all(abs(x - y) <= 1 for x, y in zip(p, q))


def load(cp):
    ds = open(cp + '.ds.bin', 'rb').read()
    if os.path.exists(cp + '.map.bin'):
        m = open(cp + '.map.bin', 'rb').read()
    else:
        m = extract(cp)
    return ds, m


def extract(cp):
    ds = open(cp + '.ds.bin', 'rb').read()
    mem = open(cp + '.mem.bin', 'rb').read()
    ofs, seg = struct.unpack_from('<HH', ds, MAP_PTR)
    base = seg * 16 + ofs - MAP_CENTRE
    m = mem[base:base + MAP_SIZE]
    open(cp + '.map.bin', 'wb').write(m)
    return m


def check(cp):
    ds, m = load(cp)
    pal = open(cp + '.pal', 'rb').read()
    px = Image.open(cp + '.png').convert('RGB').load()
    res = ds[RESULTS_FLAG] != 0
    ph = struct.unpack_from('<H', ds, T + 4)[0]
    best = None
    for k in range(-8, 3):
        o = draw(ds, m, res, (ph + k) % 398)
        sc = sum(1 for a, c in o.items() if same(px[a % 320, a // 320], rgb(pal, c)))
        if best is None or sc > best[0]:
            best = (sc, k, len(o))
    tilt = struct.unpack_from('<h', ds, 0x297C)[0]
    print('%s: %s, phase %d%+d, tilt %d: %d of %d globe pixels match' % (
        os.path.basename(cp), 'results' if res else 'standard', ph, best[1], tilt, best[0], best[2]))


def render(cp, out, mode=None):
    ds, m = load(cp)
    pal = open(cp + '.pal', 'rb').read()
    res = (ds[RESULTS_FLAG] != 0) if mode is None else mode == 'results'
    ph = (struct.unpack_from('<H', ds, T + 4)[0] - 1) % 398
    img = Image.open(cp + '.png').convert('RGB')
    for a, c in draw(ds, m, res, ph).items():
        img.putpixel((a % 320, a // 320), rgb(pal, c))
    img.save(out)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'check':
        for cp in sys.argv[2:]:
            check(cp)
    elif cmd == 'render':
        mode = sys.argv[sys.argv.index('--mode') + 1] if '--mode' in sys.argv else None
        render(sys.argv[2], sys.argv[3], mode)
    elif cmd == 'expect':
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from dune_dat import load_file
        mapfile, phase, tilt, mode, out, data = sys.argv[2:8]
        tl = open(os.path.join(data, 'TABLAT.BIN'), 'rb').read()
        gd = load_file(os.path.join(data, 'GLOBDATA.HSQ'))
        ds = synth(tl, gd, int(tilt))
        buf = bytearray(b'\xff' * 64000)
        for a, c in draw(ds, open(mapfile, 'rb').read(), mode == 'results', int(phase) % 398).items():
            buf[a] = c
        open(out, 'wb').write(buf)
    elif cmd == 'engine':
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from dune_dat import load_file
        dump, frames, data = sys.argv[2:5]
        tl = open(os.path.join(data, 'TABLAT.BIN'), 'rb').read()
        gd = load_file(os.path.join(data, 'GLOBDATA.HSQ'))
        cache = {}
        results = []
        bad_total = 0
        for name in sorted(f[:-4] for f in os.listdir(dump) if f.endswith('.idx')):
            base = os.path.join(dump, name)
            words = open(base + '.txt').read().split()
            phase, tilt, mode = int(words[1]), int(words[3]), words[4]
            m = open(base + '.map', 'rb').read()
            idx = open(base + '.idx', 'rb').read()
            if tilt not in cache:
                cache[tilt] = synth(tl, gd, tilt)
            exp = draw(cache[tilt], m, mode == 'results', phase)
            bad = sum(1 for a, c in exp.items() if a < len(idx) and idx[a] != c)
            bad_total += bad
            px = [idx[a] for a in exp if a < len(idx)]
            stages = [0, 0, 0, 0]
            for b in m[:MAP_SIZE]:  # the save's last flag byte covers 3 cells past the map
                stages[(b >> 4) & 3] += 1
            results.append((os.path.getmtime(base + '.idx'), name, phase, tilt, mode, bad, stages[2], stages[3], stages[1],
                            sum(1 for c in px if 0x20 <= c <= 0x23), sum(1 for c in px if 0x24 <= c <= 0x2F),
                            sum(1 for c in px if 0x34 <= c <= 0x3F)))
        print('globes %d differing %d' % (len(results), bad_total))
        pngs = sorted((os.path.getmtime(os.path.join(frames, f)), f[:-4]) for f in os.listdir(frames) if f.endswith('.png'))
        for t, cp in pngs:
            before = [r for r in results if r[0] <= t]
            if not before:
                continue
            r = before[-1]
            print('%s %s phase %d tilt %d %s differing %d atreides %d harkonnen %d sprouting %d greens %d reds %d blues %d'
                  % (cp, r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11]))
    elif cmd == 'extract':
        for cp in sys.argv[2:]:
            extract(cp)
    else:
        sys.exit(__doc__)

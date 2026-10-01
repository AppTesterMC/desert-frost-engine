#!/usr/bin/env python3
"""Dump DIALOGUE.HSQ entries with their CONDIT expressions and sentences.

Usage: dune_dialogue_dump.py DUNE.DAT|FLOPPY_DIR [regex]
Use --release cd for extracted CD files or --release amiga for Amiga files.
Entry layout and operators as in engines/dune/dialogue.cpp (sub_19F9E,
sub_1A396): b0 bit7 said, bit6 repeatable, bits0-3 action; condition =
(b2 >> 6) << 8 | b1; sentence = (b2 & 3) << 8 | b3 (1-based); entries from
the word at 0x60 (CD) / 0x70 (DOS floppy / Amiga) on use PHRASEx2.
Original loaders: CD CS:D00F, DOS floppy CS:CA75, Amiga hunk 0 FB16.
"""
import argparse, re, struct, sys
from pathlib import Path
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import dune_sprite_sheet as s

OPS = {0x00: '==', 0x02: '<', 0x04: '>', 0x06: '!=', 0x08: '<=s', 0x0a: '>=s', 0x0c: '+', 0x0e: '-', 0x10: '&', 0x12: '|'}

def strings(d):
    n = struct.unpack_from('<H', d, 0)[0] // 2
    out = []
    for i in range(n):
        o = struct.unpack_from('<H', d, 2 * i)[0]
        e = d.find(b'\xff', o)
        out.append(d[o:e].decode('latin-1') if e >= 0 else '')
    return out

def condition(c, index):
    if index == 0:
        return 'true'
    p = struct.unpack_from('<H', c, (index - 1) * 2)[0]
    def operand():
        nonlocal p
        k = c[p]; p += 1
        if k < 0x80:
            v = c[p]; p += 1
            return ('b' if k == 1 else 'w') + '[%#x]' % v
        if k == 0x80:
            v = c[p]; p += 1
            return '%#x' % v
        v = struct.unpack_from('<H', c, p)[0]; p += 2
        return '%#x' % v
    parts = [operand()]
    while c[p] != 0xff:
        op = c[p]; p += 1
        parts.append(('(' if op & 0x80 else '') + OPS.get(op & 0x1f, '?%x' % op))
        parts.append(operand())
    return ' '.join(parts)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    parser.add_argument('regex', nargs='?')
    parser.add_argument('--release', choices=('floppy', 'cd', 'amiga'),
                        help='defaults to floppy for a directory, CD for DUNE.DAT')
    args = parser.parse_intermixed_args()
    if args.data.is_dir():
        f = {p.name.upper(): p.read_bytes() for p in args.data.iterdir()
             if p.is_file() and p.suffix.lower() == '.hsq'}
    else:
        f = s.dat(args.data)
    release = args.release or ('floppy' if args.data.is_dir() else 'cd')
    pat = re.compile(args.regex, re.I) if args.regex else None
    dlg = s.unhsq(f['DIALOGUE.HSQ']); cond = s.unhsq(f['CONDIT.HSQ'])
    lang = 2 if release == 'amiga' else 1
    p1 = strings(s.unhsq(f['PHRASE%d1.HSQ' % lang])); p2 = strings(s.unhsq(f['PHRASE%d2.HSQ' % lang]))
    split = struct.unpack_from('<H', dlg, 0x60 if release == 'cd' else 0x70)[0]
    for idx in range(17 * 8):
        o = struct.unpack_from('<H', dlg, idx * 2)[0]
        while o + 4 <= len(dlg) and not (dlg[o] == 0xff and dlg[o + 1] == 0xff):
            b0, b1, b2, b3 = dlg[o:o + 4]
            cnum = ((b2 >> 6) << 8) | b1
            snum = ((b2 & 3) << 8) | b3
            text = (p2 if o >= split else p1)[snum - 1] if snum else ''
            if not pat or pat.search(text):
                print('char %2d list %d @%4d act %2d %s%s cond %3d [%s] s%d: %s' % (idx // 8, idx % 8, o, b0 & 15,
                      'R' if b0 & 0x40 else '-', 'S' if b0 & 0x80 else '-', cnum, condition(cond, cnum), snum - 1, text[:110]))
            o += 4

if __name__ == '__main__':
    main()

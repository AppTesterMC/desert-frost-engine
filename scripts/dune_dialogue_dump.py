#!/usr/bin/env python3
"""Dump DIALOGUE.HSQ entries with their CONDIT expressions and sentences.

Usage: dune_dialogue_dump.py DUNE.DAT [regex]   (lines whose text matches)
Entry layout and operators as in engines/dune/dialogue.cpp (sub_19F9E,
sub_1A396): b0 bit7 said, bit6 repeatable, bits0-3 action; condition =
(b2 >> 6) << 8 | b1; sentence = (b2 & 3) << 8 | b3 (1-based); entries from
the word at 0x60 on use PHRASEx2.
"""
import re, struct, sys
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
    f = s.dat(sys.argv[1])
    pat = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
    dlg = s.unhsq(f['DIALOGUE.HSQ']); cond = s.unhsq(f['CONDIT.HSQ'])
    p1 = strings(s.unhsq(f['PHRASE11.HSQ'])); p2 = strings(s.unhsq(f['PHRASE12.HSQ']))
    split = struct.unpack_from('<H', dlg, 0x60)[0]
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

main()

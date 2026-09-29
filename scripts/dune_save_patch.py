#!/usr/bin/env python3
"""Read or patch the data segment of an original Dune save (DUNE21Sn.SAV floppy,
DUNE37Sn.SAV CD), for late-game test states the original can't reach quickly.

The format is the engine's saves.cpp: word game time, word RLE marker (0xf7),
word length - 2, then an RLE body (marker, count, value) whose LAST 4718
(floppy) or 4705 (CD) bytes are the data segment from ds:0000. Patched saves
say so in their scenario/check (they are not states the original produced).

usage: dune_save_patch.py SAVE [--out OUT] [--show ADDR[:N] ...] [--set ADDR=BYTE[,BYTE...] ...]
       ADDR hex ds offset (the floppy's shift of +13 above ds:1190 is not applied).
"""
import argparse, struct, sys

def unpack(packed):
    marker = packed[2]
    body = bytearray(); i = 6
    while i < len(packed):
        b = packed[i]; i += 1
        if b == marker and i + 1 < len(packed):
            body += bytes([packed[i + 1]]) * packed[i]; i += 2
        else:
            body.append(b)
    return body

def pack(body, head):
    out = bytearray(head[:6]); i = 0
    while i < len(body):
        v = body[i]; run = 1
        while i + run < len(body) and body[i + run] == v and run < 255:
            run += 1
        if run > 2 or v == 0xf7:
            out += bytes([0xf7, run, v])
        else:
            out += bytes([v]) * run
        i += run
    struct.pack_into('<H', out, 4, len(out) - 2)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('save'); ap.add_argument('--out')
    ap.add_argument('--show', action='append', default=[])
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--cd', action='store_true', help='CD layout (4705-byte data segment)')
    a = ap.parse_args()
    packed = open(a.save, 'rb').read()
    body = unpack(packed)
    size = 4705 if a.cd or 'DUNE37' in a.save.upper() else 4718
    base = len(body) - size
    for s in a.show:
        addr, _, n = s.partition(':'); addr = int(addr, 16); n = int(n or '1', 16 if n.startswith('0x') else 10)
        print('ds:%04x %s' % (addr, body[base + addr:base + addr + n].hex(' ')))
    for s in a.set:
        addr, vals = s.split('='); addr = int(addr, 16)
        for k, v in enumerate(vals.split(',')):
            body[base + addr + k] = int(v, 16)
    if a.set:
        out = pack(body, packed)
        open(a.out or a.save, 'wb').write(out)
        print('written', a.out or a.save, len(out), 'bytes')

if __name__ == '__main__':
    main()

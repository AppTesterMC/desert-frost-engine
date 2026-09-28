#!/usr/bin/env python3
"""Prototype of World::loadSegaCdData (segacd_world.cpp): rebuild the PC CD
data-segment layout (0x0000-0x1260 plus the room tables) from the Sega CD
program (file 0) and, when a PC executable is given, list where the result
differs from the PC's own initial data. Usage:
  segacd_ds_convert.py <file0.bin> [DNCDPRG.EXE]"""
import sys
HEAD = bytes.fromhex('00000002200a01802000000a')
REGIONS = [  # (pc start, pc end, sega shift); None = fill (see FILLS)
    (0x0000, 0x08aa, 0),
    # troops: 68 records, 27 bytes on the PC, 28 on the Sega CD (pad at the end)
    (0x0fd6, 0x10d8, 0x44),   # 2 bytes + characters (16 x 16)
    # smugglers: 6 records, 17 bytes -> 18 (pad at the end)
    (0x113e, 0x113f, 0x4a),   # their 0xff terminator
    (0x1141, 0x1156, 0x48),   # 0x113f: a PC pointer the Sega CD does not keep
    (0x1158, 0x116b, 0x46),   # 0x1156: a word the Sega CD does not keep
    (0x116b, 0x1171, 0x121),
    (0x1171, 0x11b9, 0xbe),
    (0x11b9, 0x11bb, 0xe9),
    (0x11cf, 0x11d3, 0xdc),
    (0x11eb, 0x120b, 0x10f),  # the name table
    (0x1225, 0x1261, 0xf5),   # the palace room table
]
FILLS = [(0x113f, 0x1141, 0x00), (0x1156, 0x1158, 0xff), (0x1223, 0x1225, 0xff)]
WORDS = [0x2, 0x4, 0x6, 0xa8, 0xac, 0x115e, 0x1160, 0x116b, 0x116d, 0x116f, 0x1172, 0x1174, 0x1176] + \
    list(range(0x11eb, 0x120b, 2))

def convert(f0):
    b = f0.find(HEAD); assert b >= 0
    sg = f0[b:]
    out = bytearray(0x1500)
    src = {}
    def put(pc, s):
        out[pc] = sg[s]; src[pc] = s
    for a, e, sh in REGIONS:
        for i in range(a, e): put(i, i + sh)
    for k in range(68):
        for i in range(27): put(0x8aa + 27 * k + i, 0x8aa + 28 * k + i)
    for k in range(6):
        for i in range(17): put(0x10d8 + 17 * k + i, 0x10d8 + 0x44 + 18 * k + i)
    for a, e, v in FILLS:
        for i in range(a, e): out[i] = v
    words = list(WORDS)
    for k in range(70): words += [0x100 + 28 * k + 2, 0x100 + 28 * k + 4, 0x100 + 28 * k + 6]
    for k in range(68): words += [0x8aa + 27 * k + 16, 0x8aa + 27 * k + 18]
    for k in range(16): words += [0xfd8 + 16 * k, 0xfd8 + 16 * k + 2]
    for w in words: out[w], out[w + 1] = out[w + 1], out[w]
    return b, out, src

if __name__ == '__main__':
    f0 = open(sys.argv[1], 'rb').read()
    b, out, src = convert(f0)
    print(f'Sega data segment at file offset {b:#x} (sub-CPU {b + 0x7000:#x})')
    if len(sys.argv) > 2:
        pc = open(sys.argv[2], 'rb').read()[63152:63152 + 0x1500]
        diff = [i for i in range(0x1261) if out[i] != pc[i]]
        print(len(diff), 'bytes differ in 0x0000-0x1260')
        runs = []
        for i in diff:
            if runs and i - runs[-1][1] <= 2: runs[-1][1] = i
            else: runs.append([i, i])
        for a, e in runs:
            print(f'  {a:#06x}-{e:#06x} pc {pc[a:e+1].hex()} sega {bytes(out[a:e+1]).hex()}')

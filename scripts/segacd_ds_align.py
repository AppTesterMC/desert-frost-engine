#!/usr/bin/env python3
"""Align the Sega CD data segment (file 0, sub-CPU 0x90c8) with the PC CD one
(DNCDPRG.EXE file offset 63152): report the PC->Sega offset map and the word
fields stored big-endian. Research tool behind segacd_world.cpp's tables.

Alignment: for every PC offset keep the running shift while the bytes agree
either directly or as a byte-swapped word; when they stop agreeing, search the
next shift (0..0x200) that makes the following 16 bytes agree best."""
import sys
exe = open(sys.argv[1], 'rb').read(); pc = exe[63152:63152 + 0x1500]
f0 = open(sys.argv[2], 'rb').read(); B = 0x20c8
sg = f0[B:B + 0x1800]

def agree(i, s):
    j = i + s
    if j < 0 or j + 1 >= len(sg) or i + 1 >= len(pc): return False
    if pc[i] == sg[j]: return True
    # part of a swapped word: (i, i+1) <-> (j+1, j) or (i-1, i) <-> (j, j-1)
    if (j % 2 == 0 and pc[i] == sg[j + 1] and pc[i + 1] == sg[j]) or \
       (j % 2 == 1 and pc[i] == sg[j - 1] and pc[i - 1] == sg[j]): return True
    return False

def score(i, s, n=16):
    return sum(agree(i + k, s) for k in range(n) if i + k < len(pc))

shift = 0; m = []
i = 0
while i < len(pc):
    if agree(i, shift) or score(i, shift) >= 12:
        m.append(shift); i += 1; continue
    best = max(range(-2, 0x200), key=lambda s: (score(i, s), -abs(s - shift)))
    if score(i, best) > score(i, shift) + 3: shift = best
    m.append(shift); i += 1

runs = []
for i, s in enumerate(m):
    if not runs or runs[-1][1] != s: runs.append([i, s])
print('shift runs (pc offset, sega = pc + shift):')
for a, s in runs: print(f'  {a:#06x} +{s:#x}')
swap = []
for i in range(len(pc) - 1):
    j = i + m[i]
    if m[i + 1] != m[i] or j % 2 or j + 1 >= len(sg): continue
    if pc[i] != pc[i + 1] and sg[j] == pc[i + 1] and sg[j + 1] == pc[i]: swap.append(i)
print('big-endian words with asymmetric values (pc offsets):', len(swap))
print(' '.join(f'{x:#x}' for x in swap))

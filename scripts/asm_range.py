#!/usr/bin/env python3
"""Print OpenRakis DNCDPRG_RECENT.ASM between two seg000 offsets (hex).

Labels in that listing are named sub_1XXXX / loc_1XXXX / locret_1XXXX for seg000
offset XXXX. Usage: asm_range.py START END [asm path]. Blank lines are dropped.
"""
import re, sys
start, end = int(sys.argv[1], 16), int(sys.argv[2], 16)
path = sys.argv[3] if len(sys.argv) > 3 else __import__('os').environ.get('DUNE_OPENRAKIS_ASM', 'DNCDPRG_RECENT.ASM')  # OpenRakis asm/cd listing
lines = open(path, encoding='latin-1').read().split('\n')
label = re.compile(r'^(?:sub|loc|locret|nullsub_\d+|_sub)_1([0-9A-F]{4})\b')
on = False
for l in lines:
    m = label.match(l)
    if m:
        off = int(m.group(1), 16)
        if off >= end:
            if on: break
        on = off >= start or on
    if on and l.strip() and not l.startswith('; \xff'):
        print(l)

#!/usr/bin/env python3
"""Disassemble seg000 code of the CD DNCDPRG.EXE (v3.7) with capstone.

The OpenRakis listing drops some instructions (seen at seg000:2595, where the
`cmp al, 0c0h` family is missing), so exact rules are read from the binary.
Usage: dune_disasm.py START END [exe]   (hex seg000 offsets; header is 512 bytes)
Requires `pip install capstone`.
"""
import os, struct, sys
import capstone
start, end = int(sys.argv[1], 16), int(sys.argv[2], 16)
exe = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ.get("TMPDIR", "/tmp"), "dune-data", "DNCDPRG.EXE")
d = open(exe, 'rb').read()
hdr = struct.unpack_from('<H', d, 8)[0] * 16
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
for ins in md.disasm(d[hdr + start:hdr + end], start):
    tgt = ''
    print('%04x  %-7s %s' % (ins.address, ins.mnemonic, ins.op_str.replace('0xffff', '0x')))

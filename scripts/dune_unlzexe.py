#!/usr/bin/env python3
"""Unpack an LZEXE 0.91 DOS executable (the floppy's DUNEPRG.EXE) to its load image.

The algorithm follows UNLZEXE (Mitugu Kurizono), as World::unpackLzexe does:
a 16-bit bit queue reloaded after the sixteenth bit; bit 1 = literal byte;
00xx = short match (count 2-5, offset byte - 256); 01 = long match (word:
offset high 5 bits + byte, count in the low 3 bits or an extra byte).
Usage: dune_unlzexe.py DUNEPRG.EXE out.bin  -> prints the data segment offset.
"""
import struct
import sys


def unpack(exe):
    header_paras = struct.unpack_from('<H', exe, 8)[0]
    cs, ip = struct.unpack_from('<HH', exe, 0x16)[::-1][::-1]
    ip = struct.unpack_from('<H', exe, 0x14)[0]
    cs = struct.unpack_from('<H', exe, 0x16)[0]
    image = exe[header_paras * 16:]
    # The packed data ends where the decompressor's segment (CS) starts.
    packed = image[:cs * 16]
    pos = 0
    out = bytearray()
    bits = struct.unpack_from('<H', packed, pos)[0]; pos += 2
    count = 16

    def getbit():
        nonlocal bits, count, pos
        b = bits & 1
        bits >>= 1
        count -= 1
        if count == 0:
            bits = struct.unpack_from('<H', packed, pos)[0]; pos += 2
            count = 16
        return b

    while True:
        if getbit():
            out.append(packed[pos]); pos += 1
            continue
        if not getbit():
            length = (getbit() << 1 | getbit()) + 2
            span = packed[pos] - 256; pos += 1
        else:
            lo, hi = packed[pos], packed[pos + 1]; pos += 2
            span = (lo | ((hi & 0xf8) << 5)) - 8192
            length = hi & 7
            if length:
                length += 2
            else:
                length = packed[pos]; pos += 1
                if length == 0:
                    break
                if length == 1:
                    continue
                length += 1
        for _ in range(length):
            out.append(out[len(out) + span])
    return bytes(out)


if __name__ == '__main__':
    exe = open(sys.argv[1], 'rb').read()
    img = unpack(exe)
    open(sys.argv[2], 'wb').write(img)
    sig = bytes.fromhex('000002000a20800120 00000a'.replace(' ', ''))
    print(len(img), 'bytes; data segment at', img.find(sig))

#!/usr/bin/env python3
"""DUNE.DAT reader, HSQ unpacker, sprite-sheet palette/toc helpers and .SAL parser.

Ported from madmoose's dune-rust (crates/dune/src/hsq.rs, sprite_sheet.rs) and
the engine's Room::draw; used by the analysis one-liners in the evidence log.
"""
import struct


class Dat:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        n = struct.unpack_from('<H', self.d, 0)[0]
        self.entries = {}
        for i in range(n):
            e = self.d[2 + i * 25:2 + (i + 1) * 25]
            name = e[:16].split(b'\0')[0].decode('ascii', 'replace')
            size, off = struct.unpack_from('<II', e, 16)
            if name:
                self.entries[name.upper()] = (off, size)

    def raw(self, name):
        off, size = self.entries[name.upper()]
        return self.d[off:off + size]

    def load(self, name):
        return unhsq(self.raw(name))


def load_file(path):
    return unhsq(open(path, 'rb').read())


def unhsq(data):
    if len(data) < 6 or (sum(data[:6]) & 0xff) != 0xAB:
        return data
    packed = data[3] | (data[4] << 8)
    if packed != len(data):
        return data
    size = data[0] | (data[1] << 8) | (data[2] << 16)
    out = bytearray()
    pos = 6
    queue = 0

    def rd16():
        nonlocal pos
        v = data[pos] | (data[pos + 1] << 8); pos += 2; return v

    def rd8():
        nonlocal pos
        v = data[pos]; pos += 1; return v

    def bit():
        nonlocal queue
        q = queue
        b = q & 1
        q >>= 1
        if q == 0:
            q = rd16()
            b = q & 1
            q = 0x8000 | (q >> 1)
        queue = q
        return b

    while True:
        if bit():
            out.append(rd8())
        else:
            if bit():
                word = rd16()
                count = word & 7
                offset = 8192 - (word >> 3)
                if count == 0:
                    count = rd8()
                if count == 0:
                    break
            else:
                b0 = bit(); b1 = bit()
                count = 2 * b0 + b1
                offset = 256 - rd8()
            for _ in range(count + 2):
                out.append(out[len(out) - offset])
    return bytes(out[:size]) if size else bytes(out)


def palette_chunks(sheet):
    """(start, count) ranges the sheet's palette chunk writes."""
    end = sheet[0] | (sheet[1] << 8)
    pos = 2
    ranges = []
    while pos + 2 <= end:
        start, count = sheet[pos], sheet[pos + 1]
        pos += 2
        if start == 0xff and count == 0xff:
            break
        if start == 0 and count == 1:
            pos += 3
            continue
        if count == 0:
            count = 256
        ranges.append((start, count))
        pos += 3 * count
    return ranges


def toc(sheet):
    t = sheet[0] | (sheet[1] << 8)
    first = sheet[t] | (sheet[t + 1] << 8)
    return [t + (sheet[t + 2 * i] | (sheet[t + 2 * i + 1] << 8)) for i in range(first // 2)]


def strings(command_file):
    """The 0xFF-terminated records of a COMMAND/PHRASE file, 0-based."""
    d = command_file
    n = struct.unpack_from('<H', d, 0)[0] // 2
    out = []
    for i in range(n):
        o = struct.unpack_from('<H', d, 2 * i)[0]
        e = d.find(b'\xff', o)
        out.append(d[o:e].decode('latin-1'))
    return out


if __name__ == '__main__':
    import sys
    dat = Dat(sys.argv[1])
    for i, s in enumerate(strings(dat.load(sys.argv[2] if len(sys.argv) > 2 else 'COMMAND1.HSQ'))):
        print(i + 1, repr(s))

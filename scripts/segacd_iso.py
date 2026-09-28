#!/usr/bin/env python3
"""Read the ISO 9660 file system of a Sega CD / Mega CD data track stored as
MODE1/2352 raw sectors (a .bin from a bin/cue rip) and list or extract it.

Each raw sector is 2352 bytes: 12 sync + 4 header + 2048 user data + 288 EDC/ECC.
Usage:
  segacd_iso.py list    <track1.bin>
  segacd_iso.py extract <track1.bin> <outdir>
  segacd_iso.py boot    <track1.bin> <out.bin>   # sectors 0..15 (IP/SP boot area)
"""
import os, struct, sys

RAW, DATA_OFF, USER = 2352, 16, 2048

class Track:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.f.seek(0, 2)
        self.raw = (self.f.tell() % RAW == 0 and self._sync())
        self.f.seek(0)
    def _sync(self):
        self.f.seek(0); return self.f.read(12) == b'\x00' + b'\xff' * 10 + b'\x00'
    def sector(self, lba):
        if self.raw:
            self.f.seek(lba * RAW + DATA_OFF); return self.f.read(USER)
        self.f.seek(lba * USER); return self.f.read(USER)
    def read(self, lba, size):
        out = bytearray(); n = (size + USER - 1) // USER
        for i in range(n): out += self.sector(lba + i)
        return bytes(out[:size])

def walk(t, lba, size, prefix=''):
    data = t.read(lba, size); pos = 0
    while pos < len(data):
        ln = data[pos]
        if ln == 0:
            pos = (pos // USER + 1) * USER; continue
        rec = data[pos:pos + ln]
        elba = struct.unpack_from('<I', rec, 2)[0]
        esize = struct.unpack_from('<I', rec, 10)[0]
        flags = rec[25]; nl = rec[32]; name = rec[33:33 + nl]
        pos += ln
        if name in (b'\x00', b'\x01'): continue
        name = name.decode('latin1').split(';')[0]
        path = prefix + name
        if flags & 2:
            yield (path + '/', elba, esize, True)
            yield from walk(t, elba, esize, path + '/')
        else:
            yield (path, elba, esize, False)

def entries(path):
    t = Track(path)
    pvd = t.sector(16)
    assert pvd[1:6] == b'CD001', 'no ISO 9660 PVD at sector 16'
    root = pvd[156:156 + 34]
    return t, list(walk(t, struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]))

if __name__ == '__main__':
    cmd, src = sys.argv[1], sys.argv[2]
    t, ents = entries(src)
    if cmd == 'list':
        for p, lba, size, d in ents: print(f'{lba:7d} {size:10d} {p}')
    elif cmd == 'extract':
        out = sys.argv[3]
        for p, lba, size, d in ents:
            dst = os.path.join(out, p)
            if d: os.makedirs(dst, exist_ok=True); continue
            os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
            with open(dst, 'wb') as fo: fo.write(t.read(lba, size))
    elif cmd == 'boot':
        with open(sys.argv[3], 'wb') as fo: fo.write(t.read(0, 16 * USER))

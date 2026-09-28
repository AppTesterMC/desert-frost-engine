#!/usr/bin/env python3
"""Extract Cryo's Dune (Amiga, 1992) from its three disk images into loose files.

Usage: dune_amiga_extract.py DISK1.adf DISK2.adf DISK3.adf OUTPUT_DIR

The output is what the game's own installer (disk_to_hd on disk 1) writes to
a hard disk: the executable "dune" and the data files with lower-case names
(leto.hsq, palace.sam, ...). Point ScummVM at OUTPUT_DIR.

Disk layout (from disk_to_hd, which ships with its symbol table):
- DUNE1 holds a small OFS file system: dune, dir.0, dir.1, disk_to_hd, tiroir.
- dir.0 is 103 big-endian records of 14 bytes: unpacked size, stored size,
  first sector on disk 1, 2 and 3 (0 = not on that disk). The file names are
  the table in disk_to_hd (index order below).
- The data sits in raw sectors outside the file system. A 512-byte sector is
  a 16-bit checksum (the sum of the following 255 big-endian words) and 510
  data bytes; a file fills consecutive sectors.
Only the standard library is needed.
"""
import os
import struct
import sys

NAMES = """icone fresk leto jess hawa idah gurn stil kyne chan hara baro feyd empr hark smug
frm1 frm2 frm3 por prouge comm equi balcon corr siet0 sas dunes2 fort bunk harko serre bota
palplan sun prim1 prim2 dunes onmap pers chankiss sky ornypan ornytk attack stars intds sunrs
paul back mois book orny ornycab generic cryo shai dunes3 ver map2 death siet1 siet2 siet3
siet4 siet5 siet6 siet7 siet8 siet9 siet10 siet11 siet12 vilg1 vilg2 vilg3 vilg4 vilg5 vilg6
stars1 back1 final credits shai1 shai2 mirror tablat.bin dunechar condit dialogue siet.sam
palace.sam vilg.sam hark.sam map globdata phrase21 phrase22 command2 dune10s0.sav m1 m2 m3""".split()
NAMES = [n if '.' in n else n + '.hsq' for n in NAMES]


def ofs_files(image):
    """Name -> bytes for the files in the root directory of an OFS disk."""
    def block(n):
        return image[n * 512:(n + 1) * 512]

    def be32(b, o):
        return struct.unpack_from('>I', b, o)[0]

    root = block(880)
    files = {}
    for i in range(72):
        key = be32(root, 24 + 4 * i)
        while key:
            header = block(key)
            name = header[433:433 + header[432]].decode('latin-1')
            if struct.unpack_from('>i', header, 508)[0] == -3:  # a file
                size = be32(header, 324)
                data = bytearray()
                nxt = be32(header, 16)
                while nxt and len(data) < size:
                    b = block(nxt)
                    data += b[24:24 + be32(b, 12)]
                    nxt = be32(b, 16)
                files[name.lower()] = bytes(data[:size])
            key = be32(header, 496)  # hash chain
    return files


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    disks = [open(p, 'rb').read() for p in sys.argv[1:4]]
    out = sys.argv[4]
    os.makedirs(out, exist_ok=True)
    files = ofs_files(disks[0])
    for name in ('dune', 'dir.0'):
        if name not in files:
            print('disk 1 has no %s: is the first image DUNE1?' % name)
            return 1
    open(os.path.join(out, 'dune'), 'wb').write(files['dune'])
    directory = files['dir.0']
    bad = 0
    for i in range(len(directory) // 14):
        unpacked, stored, *sectors = struct.unpack_from('>IIHHH', directory, i * 14)
        data = None
        for d, first in enumerate(sectors):
            if not first:
                continue
            buf = bytearray()
            ok = True
            for k in range((stored + 509) // 510):
                sector = disks[d][(first + k) * 512:(first + k + 1) * 512]
                if len(sector) < 512 or sum(struct.unpack('>255H', sector[2:])) & 0xffff != \
                        struct.unpack('>H', sector[:2])[0]:
                    ok = False
                    break
                buf += sector[2:]
            if ok:
                data = bytes(buf[:stored])
                break
        if data is None:
            print('%s: no readable copy on any disk' % NAMES[i])
            bad += 1
            continue
        open(os.path.join(out, NAMES[i]), 'wb').write(data)
    print('%d files written to %s%s' % (len(directory) // 14 - bad + 1, out,
                                       (', %d unreadable' % bad) if bad else ''))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

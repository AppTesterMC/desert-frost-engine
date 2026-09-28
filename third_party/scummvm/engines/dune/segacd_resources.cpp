/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This file is part of the Dune engine.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "common/archive.h"
#include "common/endian.h"

#include "dune/debug.h"
#include "dune/segacd_resources.h"

namespace Dune {

namespace {

const uint32 kSector = 2048;
const uint32 kRawSector = 2352;
const uint32 kRawHeader = 16;

/**
 * PC resource names with a Sega CD counterpart, found by comparing every
 * Sega CD file with the PC CD release's unpacked DUNE.DAT (FINDINGS.md):
 * "exact" files are byte-identical; the others hold the same records with
 * small edits (CONDIT has one condition more, DIALOGUE 291 bytes changed at
 * its end, COMMAND1 a few names). Languages follow the PC numbering:
 * 1 English, 2 French, 3 German, 4 English (second copy), 5 Italian,
 * 6 Spanish.
 */
struct NamedFile {
	const char *name;
	uint16 file;
};

const NamedFile kNamedFiles[] = {
	{ "TABLAT.BIN", 2 },     // exact
	{ "MAP.HSQ", 3 },        // exact
	{ "CONDIT.HSQ", 7 },     // edited
	{ "DIALOGUE.HSQ", 8 },   // edited
	{ "DNCHAR.BIN", 9 },     // exact
	{ "DNCHAR2.BIN", 10 },   // exact
	{ "COMMAND1.HSQ", 12 },
	{ "COMMAND2.HSQ", 13 },
	{ "COMMAND3.HSQ", 14 },  // exact
	{ "COMMAND4.HSQ", 15 },
	{ "COMMAND5.HSQ", 16 },  // exact
	{ "COMMAND6.HSQ", 17 },  // exact
	{ "PHRASE11.HSQ", 18 },  // exact
	{ "PHRASE12.HSQ", 19 },
	{ "PHRASE21.HSQ", 20 },
	{ "PHRASE22.HSQ", 21 },
	{ "PHRASE31.HSQ", 22 },  // exact
	{ "PHRASE32.HSQ", 23 },  // exact
	{ "PHRASE41.HSQ", 24 },  // exact
	{ "PHRASE42.HSQ", 25 },
	{ "PHRASE51.HSQ", 26 },  // exact
	{ "PHRASE52.HSQ", 27 },  // exact
	{ "PHRASE61.HSQ", 28 },  // exact
	{ "PHRASE62.HSQ", 29 },  // exact
	{ "MAP2.HSQ", 1857 },    // exact
	{ "GLOBDATA.HSQ", 1859 } // exact
};

} // namespace

SegaCdArchive::SegaCdArchive() : _open(false), _raw(false), _datSector(0) {
}

int SegaCdArchive::fileForName(const Common::String &requested) {
	Common::String name = requested;
	name.toUppercase();
	for (uint i = 0; i < ARRAYSIZE(kNamedFiles); ++i)
		if (name == kNamedFiles[i].name)
			return kNamedFiles[i].file;
	return -1;
}

bool SegaCdArchive::readSectors(uint32 sector, uint32 size, byte *out) const {
	if (!_raw) {
		if (!_file.seek((int64)(_datSector + sector) * kSector))
			return false;
		return _file.read(out, size) == size;
	}
	while (size) {
		const uint32 chunk = MIN<uint32>(size, kSector);
		if (!_file.seek((int64)(_datSector + sector) * kRawSector + kRawHeader) || _file.read(out, chunk) != chunk)
			return false;
		out += chunk;
		size -= chunk;
		++sector;
	}
	return true;
}

// A data track image: find DUNE.DAT in its ISO 9660 root directory.
bool SegaCdArchive::locateInImage(StartupLog &log) {
	byte sync[12];
	_file.seek(0);
	if (_file.read(sync, 12) != 12)
		return false;
	static const byte kSync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
	_raw = memcmp(sync, kSync, 12) == 0;
	_datSector = 0;
	byte pvd[kSector];
	if (!readSectors(16, kSector, pvd) || memcmp(pvd + 1, "CD001", 5) != 0)
		return false;
	const uint32 rootSector = READ_LE_UINT32(pvd + 156 + 2);
	const uint32 rootSize = MIN<uint32>(READ_LE_UINT32(pvd + 156 + 10), 16 * kSector);
	Common::Array<byte> dir(rootSize);
	if (!readSectors(rootSector, rootSize, dir.data()))
		return false;
	for (uint32 p = 0; p + 33 < rootSize;) {
		const byte length = dir[p];
		if (!length) {
			p = (p / kSector + 1) * kSector;
			continue;
		}
		const byte nameLength = dir[p + 32];
		if (p + 33 + nameLength <= rootSize && nameLength >= 8 &&
				scumm_strnicmp((const char *)dir.data() + p + 33, "DUNE.DAT", 8) == 0) {
			_datSector = 0;
			_bootArea.resize(16 * kSector); // IP and SP, for spLayout()
			if (!readSectors(0, _bootArea.size(), _bootArea.data()))
				_bootArea.clear();
			_datSector = READ_LE_UINT32(dir.data() + p + 2);
			log.line(Common::String::format("Sega CD: DUNE.DAT at sector %u of the %s track image", _datSector,
					_raw ? "raw (2352)" : "2048-byte"));
			return true;
		}
		p += length;
	}
	return false;
}

bool SegaCdArchive::open(StartupLog &log) {
	_open = false;
	bool found = false;
	if (_file.open(Common::Path("DUNE.DAT")) && _file.size() == kDatSize) {
		_raw = false;
		_datSector = 0;
		found = true;
		log.line("Sega CD: using DUNE.DAT");
	} else {
		_file.close();
		// The data track itself: any .bin/.img/.iso beside the game.
		static const char *const kPatterns[] = { "*.bin", "*.img", "*.iso" };
		for (uint k = 0; k < ARRAYSIZE(kPatterns) && !found; ++k) {
			Common::ArchiveMemberList members;
			SearchMan.listMatchingMembers(members, kPatterns[k]);
			for (Common::ArchiveMemberList::const_iterator it = members.begin(); it != members.end() && !found; ++it) {
				if (!_file.open((*it)->getPathInArchive()))
					continue;
				if (_file.size() > 100 * 1024 * 1024 && locateInImage(log))
					found = true;
				else
					_file.close();
			}
		}
	}
	if (!found) {
		log.line("Sega CD: neither DUNE.DAT nor a data track image was found");
		return false;
	}

	// Where file 0 ends and where its index sits differ per revision. With a
	// track image the boot area is at hand and the sub-CPU program says it
	// (see spLayout); for a bare DUNE.DAT the known revisions are tried. The
	// first candidate whose index tiles DUNE.DAT wins.
	Layout candidates[3];
	uint count = 0;
	if (_bootArea.size() && spLayout(candidates[count]))
		++count;
	candidates[count++] = Layout(0x1ec44, 0x202d2, 0x25444); // USA (T-70065, 1994-08-30)
	candidates[count++] = Layout(0x1ecc6, 0x20354, 0x254c6); // Europe (1994-04)
	for (uint c = 0; c < count && !_open; ++c)
		_open = readIndex(candidates[c], log);
	if (!_open) {
		log.line("Sega CD: no index fits this DUNE.DAT; unknown disc revision");
		return false;
	}
	return true;
}

bool SegaCdArchive::spLayout(Layout &layout) const {
	// The sub-CPU program ("MAIN DUNESP" at boot-area offset 0x800) sets up
	// the index with, in this order: clr.w (a0)+ / move.l #size0,(a0) (entry
	// 0), lea index.l,a0 / add.w d0,d0 / move.w d0,d1 / add.w d0,d0 /
	// add.w d1,d0 (entry = number * 6) and move.l #end,$c(a6).
	static const byte kSize0[4] = { 0x42, 0x58, 0x20, 0xbc };
	static const byte kIndexTail[8] = { 0xd0, 0x40, 0x32, 0x00, 0xd0, 0x40, 0xd0, 0x41 };
	uint32 size0 = 0, index = 0, end = 0;
	const byte *sp = _bootArea.data();
	const uint32 n = _bootArea.size();
	for (uint32 p = 0x800; p + 12 <= n; p += 2) {
		if (!size0 && memcmp(sp + p, kSize0, 4) == 0)
			size0 = READ_BE_UINT32(sp + p + 4);
		if (!index && sp[p] == 0x41 && sp[p + 1] == 0xf9 && memcmp(sp + p + 6, kIndexTail, 8) == 0)
			index = READ_BE_UINT32(sp + p + 2);
		if (!end && sp[p] == 0x2d && sp[p + 1] == 0x7c && sp[p + 6] == 0x00 && sp[p + 7] == 0x0c)
			end = READ_BE_UINT32(sp + p + 2);
	}
	if (!size0 || !index || end <= index)
		return false;
	layout = Layout(size0, index, end);
	return true;
}

bool SegaCdArchive::readIndex(const Layout &layout, StartupLog &log) {
	_program.resize(layout.programSize);
	if (!readSectors(0, layout.programSize, _program.data()))
		return false;
	if (layout.index < kProgramBase || layout.end - kProgramBase > layout.programSize)
		return false;
	const uint32 first = layout.index - kProgramBase, last = layout.end - kProgramBase;
	_entries.clear();
	for (uint32 p = first; p + 6 <= last; p += 6) {
		Entry e;
		e.sector = ((uint32)_program[p] << 16) | ((uint32)_program[p + 1] << 8) | _program[p + 2];
		e.size = ((uint32)_program[p + 3] << 16) | ((uint32)_program[p + 4] << 8) | _program[p + 5];
		_entries.push_back(e);
	}
	if (_entries.size() < 2)
		return false;
	_entries[0].sector = 0; // the SP writes entry 0 itself
	_entries[0].size = layout.programSize;
	// Each entry must start where the previous one ends (sectors are whole).
	for (uint i = 1; i < _entries.size(); ++i) {
		const uint32 end = _entries[i - 1].sector + (_entries[i - 1].size + kSector - 1) / kSector;
		if (_entries[i].sector != end ||
				(uint64)(_entries[i].sector + (_entries[i].size + kSector - 1) / kSector) * kSector > kDatSize) {
			_entries.clear();
			return false;
		}
	}
	log.line(Common::String::format("Sega CD: %u files, index at sub-CPU %#x, program %u bytes", _entries.size(),
			layout.index, layout.programSize));
	return true;
}

bool SegaCdArchive::load(uint index, Common::Array<byte> &data) const {
	data.clear();
	if (!_open || index >= _entries.size())
		return false;
	data.resize(_entries[index].size);
	if (!readSectors(_entries[index].sector, _entries[index].size, data.data())) {
		data.clear();
		return false;
	}
	return true;
}

bool SegaCdArchive::loadNamed(const Common::String &name, Common::Array<byte> &data) const {
	const int file = fileForName(name);
	return file >= 0 && load((uint)file, data);
}

} // namespace Dune

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

#ifndef ENGINES_DUNE_SEGACD_RESOURCES_H
#define ENGINES_DUNE_SEGACD_RESOURCES_H

#include "common/array.h"
#include "common/file.h"
#include "common/str.h"

namespace Dune {

class StartupLog;

/**
 * The Sega CD / Mega CD release's storage (Dune, Cryo/Virgin, 1993/94).
 *
 * The disc's data track is ISO 9660 with one file, DUNE.DAT (471,040,000
 * bytes). DUNE.DAT has no header and no names: it is a plain run of 2048-byte
 * sectors, and the index lives inside the game program.
 *
 * - The boot area's sub-CPU program ("MAIN DUNESP", disc sector 0 + 0x800,
 *   loaded at sub-CPU 0x6000) writes index entry 0 = (sector 0, 0x1ec44
 *   bytes), loads file 0 to sub-CPU 0x7000, file 1 (the main-CPU loader) to
 *   word RAM 0xc0000, and jumps to 0xa69e.
 * - File 0 is the game program. Its index starts at sub-CPU 0x202d2 (file
 *   offset 0x192d2) and ends at 0x25444 on the USA disc (0x20354-0x254c6 on
 *   the European one, whose program is 0x82 bytes longer): 3475 entries of
 *   six bytes, a 24-bit
 *   sector relative to the start of DUNE.DAT and a 24-bit byte size, both
 *   big-endian. (The SP adds 0x15, DUNE.DAT's first disc sector, before
 *   reading.) Files are sector-aligned and back to back.
 *
 * The files have no names, so the PC resources that survive byte for byte or
 * with small edits (text, dialogue, map) are reached through a name table
 * built by content comparison with the PC CD release (FINDINGS.md, "Sega CD").
 * Nothing is compressed: there is no HSQ on the disc.
 *
 * Accepted inputs: DUNE.DAT extracted from the disc (scripts/segacd_iso.py),
 * or the raw data track itself (a MODE1/2352 .bin or a 2048-byte .iso), in
 * which case DUNE.DAT is found through the ISO 9660 directory.
 */
class SegaCdArchive {
public:
	enum {
		kDatSize = 471040000,
		kProgramFile = 0,
		kProgramBase = 0x7000 ///< sub-CPU address of file 0
	};

	SegaCdArchive();

	/** Find and open the data (see the class comment). */
	bool open(StartupLog &log);
	bool isOpen() const { return _open; }

	uint fileCount() const { return _entries.size(); }
	uint32 fileSize(uint index) const { return index < _entries.size() ? _entries[index].size : 0; }
	bool load(uint index, Common::Array<byte> &data) const;

	/** A PC resource name that has a Sega CD counterpart ("PHRASE11.HSQ"). */
	bool loadNamed(const Common::String &name, Common::Array<byte> &data) const;
	static int fileForName(const Common::String &name);

	/** File 0 as the sub CPU sees it from kProgramBase. */
	const Common::Array<byte> &program() const { return _program; }

private:
	struct Entry {
		uint32 sector;
		uint32 size;
	};

	struct Layout {
		Layout() : programSize(0), index(0), end(0) {}
		Layout(uint32 s, uint32 i, uint32 e) : programSize(s), index(i), end(e) {}
		uint32 programSize, index, end; ///< file 0's size; the index's sub-CPU start and end
	};

	bool readSectors(uint32 sector, uint32 size, byte *out) const;
	bool spLayout(Layout &layout) const;
	bool readIndex(const Layout &layout, StartupLog &log);
	bool locateInImage(StartupLog &log);

	mutable Common::File _file;
	bool _open;
	bool _raw;          ///< 2352-byte sectors with 16-byte headers
	uint32 _datSector;  ///< first sector of DUNE.DAT in _file
	Common::Array<Entry> _entries;
	Common::Array<byte> _program;
	Common::Array<byte> _bootArea; ///< disc sectors 0-15 when a track image is used
};

} // namespace Dune

#endif // ENGINES_DUNE_SEGACD_RESOURCES_H

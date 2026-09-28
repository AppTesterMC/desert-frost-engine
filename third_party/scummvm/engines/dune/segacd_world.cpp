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

/*
 * World::loadSegaCdData: the Sega CD release's initial game data, rebuilt in
 * the PC CD data-segment layout that the rest of the engine uses.
 *
 * Where the knowledge comes from (FINDINGS.md, "Sega CD data segment"):
 * the Sega CD game program (disc file 0, loaded at sub-CPU 0x7000) keeps the
 * PC's data segment at sub-CPU 0x90c8, found by its first bytes. Aligning it
 * with DNCDPRG.EXE's (scripts/segacd_ds_align.py, then checked field by field
 * with scripts/segacd_ds_convert.py, the prototype of this file) shows:
 *
 * - it is big-endian, so every word field is byte-swapped;
 * - records with an odd length gained a pad byte at their end for the
 *   68000's word alignment: troops 27 -> 28 bytes, smugglers 17 -> 18;
 * - after the smugglers the variables were reordered (two PC-only words
 *   dropped, a new 120-byte table added), so that part is mapped variable by
 *   variable (kRegions);
 * - the room tables and their pointer table are not in the data segment but
 *   in the program's own data (the pointer table has 32-bit sub-CPU
 *   addresses). The tables themselves keep the PC's 5-byte records: the
 *   first byte is now a screen number (Sega CD screen 1678 + code, see
 *   segacd_game.cpp) instead of the PC's sheet slot and .SAL room; the four
 *   exits are the PC's.
 *
 * Checked against the PC CD executable, 78 bytes of 0x0000-0x1260 differ
 * after conversion, all explained: a few location bytes the Sega CD changed
 * (Arrakeen starts with one ornithopter; two statuses, two troop bytes),
 * the characters' +4 word (a PC data pointer; the Sega CD stores an index),
 * the palace room codes (screen numbers), and PC-only constants of
 * 0x11bd-0x1221 that the engine does not read (left zero).
 */

#include "common/endian.h"

#include "dune/debug.h"
#include "dune/dialogue.h"
#include "dune/world.h"

namespace Dune {

namespace {

/** The Sega CD data segment's first bytes (the PC's kDataSegmentHead, big-endian). */
const byte kSegaHead[12] = { 0x00, 0x00, 0x00, 0x02, 0x20, 0x0a, 0x01, 0x80, 0x20, 0x00, 0x00, 0x0a };

struct Region {
	uint16 start, end; ///< PC offsets
	uint16 shift;      ///< Sega CD offset = PC offset + shift
};

const Region kRegions[] = {
	{ 0x0000, 0x08aa, 0x000 }, // variables and the 70 locations
	{ 0x0fd6, 0x10d8, 0x044 }, // after the troops: the 16 characters
	{ 0x113e, 0x113f, 0x04a }, // the smugglers' 0xff terminator
	{ 0x1141, 0x1156, 0x048 }, // 0x113f-0x1140: a PC pointer (to 0x10d8) not kept
	{ 0x1158, 0x116b, 0x046 }, // 0x1156: a word not kept (the PC's 0xffff is used)
	{ 0x116b, 0x1171, 0x121 },
	{ 0x1171, 0x11b9, 0x0be },
	{ 0x11b9, 0x11bb, 0x0e9 },
	{ 0x11cf, 0x11d3, 0x0dc },
	{ 0x11eb, 0x120b, 0x10f }, // the name table
	{ 0x1225, 0x1261, 0x0f5 }  // the palace room table
};

struct Fill {
	uint16 start, end;
	byte value;
};

const Fill kFills[] = {
	{ 0x1156, 0x1158, 0xff },
	{ 0x1223, 0x1225, 0xff } // the 0xffff before the palace table
};

/** Word fields outside the record tables (PC offsets). */
const uint16 kWords[] = {
	0x0002, 0x0004, 0x0006, 0x00a8, 0x00ac, 0x115e, 0x1160, 0x116b, 0x116d, 0x116f, 0x1172, 0x1174, 0x1176
};

enum {
	kTroops = 68,
	kSmugglers = 0x10d8,
	kPcPalaceTable = 0x1225,
	kPcRoomTables = 0x1261,  ///< where the PC keeps the other room tables
	kPcPointerTable = 0x13c4,
	kPointerEntries = 0x34,
	kNameTableWords = 16
};

} // namespace

bool World::loadSegaCdData() {
	const Common::Array<byte> &program = *_segaCdProgram;
	_floppy = false;
	uint base = 0;
	bool found = false;
	for (uint p = 0; p + 0x1600 <= program.size() && !found; p += 2)
		if (memcmp(program.data() + p, kSegaHead, sizeof(kSegaHead)) == 0) {
			base = p;
			found = true;
		}
	if (!found) {
		_log.line("World: the Sega CD data segment was not found in the program");
		return findTables();
	}
	const byte *sega = program.data() + base;
	const uint segaSize = program.size() - base;
	byte *pc = _state.vars;
	memset(pc, 0, GameState::kSize);
	auto copy = [&](uint pcOffset, uint segaOffset) {
		if (pcOffset < GameState::kSize && segaOffset < segaSize)
			pc[pcOffset] = sega[segaOffset];
	};
	auto swap = [&](uint pcOffset) {
		if (pcOffset + 1 < GameState::kSize)
			SWAP(pc[pcOffset], pc[pcOffset + 1]);
	};

	for (uint r = 0; r < ARRAYSIZE(kRegions); ++r)
		for (uint i = kRegions[r].start; i < kRegions[r].end; ++i)
			copy(i, i + kRegions[r].shift);
	for (uint t = 0; t < kTroops; ++t)
		for (uint i = 0; i < kTroopSize; ++i)
			copy(kTroopTable + kTroopSize * t + i, kTroopTable + (kTroopSize + 1) * t + i);
	for (uint s = 0; s < 6; ++s)
		for (uint i = 0; i < 17; ++i)
			copy(kSmugglers + 17 * s + i, kSmugglers + 0x44 + 18 * s + i);
	for (uint f = 0; f < ARRAYSIZE(kFills); ++f)
		memset(pc + kFills[f].start, kFills[f].value, kFills[f].end - kFills[f].start);

	for (uint w = 0; w < ARRAYSIZE(kWords); ++w)
		swap(kWords[w]);
	for (uint w = 0; w < kNameTableWords; ++w)
		swap(GameState::kNameTable + 2 * w);
	for (uint l = 0; l < 70; ++l) // longitude, latitude, map offset
		for (uint f = 2; f <= 6; f += 2)
			swap(Location::kTableOffset + Location::kRecordSize * l + f);
	for (uint t = 0; t < kTroops; ++t) {
		swap(kTroopTable + kTroopSize * t + 16);
		swap(kTroopTable + kTroopSize * t + 18);
	}
	for (uint c = 0; c < kCharacters; ++c) { // (room, place type) and (0x80, location + 1)
		swap(kCharacterTable + kCharacterSize * c);
		swap(kCharacterTable + kCharacterSize * c + 2);
	}

	// The room tables. The program's pointer table holds 32-bit sub-CPU
	// addresses: entry 0x20 (the palace) points into the data segment,
	// the others into one block of tables just before the pointer table.
	const uint32 programBase = 0x7000;
	const uint32 palaceAddress = programBase + base + kPcPalaceTable + 0x0f5;
	uint pointerTable = 0;
	uint32 tablesStart = 0;
	for (uint p = 0; p + 4 * kPointerEntries <= program.size() && !pointerTable; p += 2) {
		if (READ_BE_UINT32(program.data() + p + 4 * Location::kPalace) != palaceAddress)
			continue;
		// Entry 0 (sietch 0) is the first table of the block, which ends
		// where the pointer table begins.
		const uint32 first = READ_BE_UINT32(program.data() + p);
		if (first < programBase + p && first + 0x200 > programBase + p) {
			pointerTable = p;
			tablesStart = first;
		}
	}
	if (!pointerTable) {
		_log.line("World: the Sega CD room pointer table was not found");
		return findTables();
	}
	// The block is 356 bytes on the USA disc; the PC's is 355 (0x1261-0x13c3).
	const uint tablesLength = MIN<uint>(programBase + pointerTable - tablesStart, kPcPointerTable - kPcRoomTables);
	memcpy(pc + kPcRoomTables, program.data() + (tablesStart - programBase), tablesLength);
	for (uint i = 0; i < kPointerEntries; ++i) {
		const uint32 address = READ_BE_UINT32(program.data() + pointerTable + 4 * i);
		uint16 offset = 0xffff;
		if (address == palaceAddress)
			offset = kPcPalaceTable;
		else if (address >= tablesStart && address < tablesStart + tablesLength)
			offset = (uint16)(kPcRoomTables + address - tablesStart);
		WRITE_LE_UINT16(pc + kPcPointerTable + 2 * i, offset);
	}

	_log.line(Common::String::format("World: Sega CD initial data from the program at %#x (sub-CPU %#x), rooms at %#x",
			base, programBase + base, tablesStart));
	return findTables();
}

} // namespace Dune

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

#ifndef ENGINES_DUNE_SAVES_H
#define ENGINES_DUNE_SAVES_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Dune {

class Dialogue;
class Resource;
class StartupLog;
class World;

/**
 * The original's save files (DUNE21S<n>.SAV on the floppy, DUNE37S<n>.SAV on
 * the CD), written and read in their own format so they stay exchangeable
 * with the DOS game (layout after madmoose's dune-rust crates/savegame and
 * the CD executable's sub_1B427/sub_1B473):
 *
 *   word  game time
 *   word  RLE marker byte (0xf7) in the low byte
 *   word  file length - 2
 *   RLE   marker, count, value = a run; other bytes literal
 *
 * Unpacked, the body is: the two flag bits of every MAP.HSQ pixel packed
 * four to a byte (the spice fields change as they are mined), 162 (CD) or
 * 198 (floppy) bytes of the executable's own variables, DIALOGUE.HSQ with
 * its said flags, and the data segment (4705 or 4718 bytes).
 */
class SaveGame {
public:
	enum {
		kSlots = 4, ///< Log 1, Log 2, last place entered, last new sietch
		kMapPixels = 50684,
		kMapFlagBytes = 0x317f,
		kExtraSize = 0xa2,            ///< the block between the map flags and the dialogue table (both releases)
		kDialogueSlack = 36,          ///< the floppy's dialogue buffer: 36 bytes past the table, zero
		kDialogueSlackCd = 0x68,      ///< the CD's: 104 bytes, the first 64 zero (checked on DUNE37S0 by the SwiftDune session)
		kDialoguePointerBase = 0xcfe9, ///< the floppy's list header in a save: file offset + this
		kDialoguePointerBaseCd = 0xaa76,
		kRleMarker = 0xf7
	};

	SaveGame(World &world, Dialogue &dialogue, Resource &resources, StartupLog &log);

	/** The DOS file name of a slot, also used in ScummVM's save directory. */
	static Common::String fileName(uint slot, bool floppy);

	bool save(uint slot);
	bool load(uint slot);
	/** The game time stored in a slot, or -1 when it holds nothing. */
	int slotTime(uint slot) const;

	/** The map with the flags of the last loaded save (or MAP.HSQ). */
	const Common::Array<byte> &map();

private:
	bool loadMap();
	void unpack(const Common::Array<byte> &packed, Common::Array<byte> &body) const;
	void pack(const Common::Array<byte> &body, Common::Array<byte> &packed) const;
	bool readFile(uint slot, Common::Array<byte> &packed) const;

	World &_world;
	Dialogue &_dialogue;
	Resource &_resources;
	StartupLog &_log;
	Common::Array<byte> _extra;
	Common::Array<byte> _slack; ///< the bytes after the dialogue table, kept as loaded ///< The executable's own block, kept from the last load.
};

} // namespace Dune

#endif // ENGINES_DUNE_SAVES_H

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

#ifndef ENGINES_DUNE_SEGACD_PANEL_H
#define ENGINES_DUNE_SEGACD_PANEL_H

#include "common/array.h"
#include "common/str.h"

#include "dune/segacd_gfx.h"

namespace Graphics {
class ManagedSurface;
struct Surface;
}

namespace Dune {

class Panel;
class SegaCdArchive;
class StartupLog;

/**
 * The Sega CD's control panel: the lower 56 lines of the 320x224 screen.
 *
 * Where the pieces are (FINDINGS.md, "Sega CD panel"; found from the code
 * at sub-CPU 0xab56, which loads file 5 at 0x3a844 and draws file 30):
 *
 * - file 30, a tile screen: the command box and the direction pad's frame,
 *   29 x 7 cells drawn from x = 88;
 * - file 5, "tile blocks": word 0 = 2, a table of word offsets (relative to
 *   the table), and per block a byte width, a byte height (in tiles) and the
 *   tiles, 32 bytes each, column-major. Block 0 is the left part (the book,
 *   the day box, two companion slots; 88 x 56); 3-5 the open "STORY" book;
 *   7-15 the companions' 16 x 16 faces; 16, 17 the sun and the moon;
 *   19-34 the direction pad for each exit mask (19 + mask, bit 0 up, 1
 *   right, 2 down, 3 left; open exits lavender) with a red centre; 35-47
 *   variants without it; 36 and 48 the map's pad.
 *
 * Colours: the panel uses VDP palette line 0 for its frame (block 0 and
 * file 30), and every room screen's CRAM puts its place's panel tint in line
 * 0 (bronze in the palace, violet outdoors). Pads and faces use line 1,
 * which file 30 supplies. The command text is the PC font's small set
 * (DNCHAR.BIN, byte-identical on the disc), as the recording shows.
 */
class SegaCdPanel {
public:
	enum {
		kTop = 168,
		kFrameX = 88,       ///< file 30's left edge
		kPadX = 256, kPadY = 184, kPadW = 40, kPadH = 32,
		kTextX = 100, kTextY = 177, kRowHeight = 8, kRows = 5,
		kTextRight = 231,
		kDayX = 8, kDayY = 200,
		kFaceX0 = 40, kFaceX1 = 64, kFaceY = 200,
		kColours = 64
	};

	bool load(SegaCdArchive &archive, StartupLog &log);
	bool loaded() const { return _loaded; }

	/** The panel's 64 colours: line 0 from the room screen, lines 1-3 from file 30. */
	void palette(const SegaCdScreen *room, byte rgb[kColours * 3]) const;

	struct State {
		uint exitMask = 0;       ///< bit 0 up, 1 right, 2 down, 3 left
		bool canLeave = false;   ///< a 252-254 exit: the pad with the red centre
		int pressedArrow = -1;
		uint day = 1;
		bool night = false;
		byte companions[2] = { 0xff, 0xff }; ///< character indices
		Common::Array<Common::String> rows;
		int highlightedRow = -1;
		bool map = false;        ///< the map screen: block 1 (the globe) and the map's pad (48)
	};

	/**
	 * Draw into an 8-bit surface whose panel colours start at @p base.
	 * @param font the engine's Panel, for DNCHAR.BIN's small set
	 */
	void draw(Graphics::ManagedSurface &target, byte base, const State &state, const Panel &font) const;

	/** The arrow (0-3) at a screen point, or -1. */
	static int arrowAt(int x, int y);
	/** The command row (0-4) at a screen point, or -1. */
	static int rowAt(int x, int y);

private:
	struct Block {
		uint width = 0, height = 0; ///< in tiles
		uint32 offset = 0;          ///< of the first tile in _blocks
	};

	void drawBlock(Graphics::Surface &target, uint index, int x, int y, byte base, byte line, bool opaque) const;
	uint padFor(uint mask, bool canLeave) const;

	bool _loaded = false;
	Common::Array<byte> _blocks;
	Common::Array<Block> _index;
	SegaCdScreen _frame;
	byte _textColour = 15;
};

} // namespace Dune

#endif // ENGINES_DUNE_SEGACD_PANEL_H

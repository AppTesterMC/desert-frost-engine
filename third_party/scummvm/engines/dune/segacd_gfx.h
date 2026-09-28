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

#ifndef ENGINES_DUNE_SEGACD_GFX_H
#define ENGINES_DUNE_SEGACD_GFX_H

#include "common/array.h"

namespace Graphics {
struct Surface;
}

namespace Dune {

/**
 * A Sega CD tile screen: the release's pictures (rooms, desert views, the
 * map console, credit cards) are stored as ready-made VDP data rather than
 * the PC's sprite sheets and .SAL polygons.
 *
 * Layout, big-endian, found by inspection of DUNE.DAT and checked on all 175
 * screen files (FINDINGS.md, "Sega CD tile screens"; reference decoder
 * scripts/segacd_img.py):
 *
 *   +0     word   offset T of the tile block
 *   +2     64 words: CRAM, four palette lines of 16 colours (0000 BBB0 GGG0 RRR0)
 *   +0x82  word   L, the length of the plane list starting here: 2 for one
 *                 plane, 4 for two, 6 for three (the language screen); each
 *                 further word is that plane's offset from 0x82
 *   plane  byte width, byte height (in tiles; 40 x 21, 40 x 25 or 40 x 28),
 *          then width * height VDP name-table words (priority 0x8000,
 *          palette line 0x6000, vflip 0x1000, hflip 0x0800, tile 0x07ff)
 *   +T     word   tile count, then 32 bytes per 8x8 tile, 4 bits per pixel,
 *                 high nibble first
 *
 * Colour 0 of every line is transparent; the backdrop is colour 0 of line 0.
 * The first plane is drawn in front of the second, and high-priority cells in
 * front of low-priority ones, as the VDP does with planes A and B.
 */
class SegaCdScreen {
public:
	enum {
		kColours = 64,
		kMaxPlanes = 4
	};

	SegaCdScreen() : _planeCount(0) {}

	/** Parse a screen file; false (and an empty screen) when it is not one. */
	bool parse(const Common::Array<byte> &data);
	static bool isScreen(const Common::Array<byte> &data);

	uint planeCount() const { return _planeCount; }
	/** Keep only the first @p count planes (the language screen's extra planes are its highlights). */
	void limitPlanes(uint count) { _planeCount = MIN(_planeCount, count); }
	uint width() const { return _planeCount ? _planes[0].width * 8 : 0; }
	uint height() const;

	/** The CRAM as 8-bit RGB triplets (3-bit levels scaled by 255 / 7). */
	void palette(byte rgb[kColours * 3]) const;
	uint16 cram(uint index) const { return index < kColours ? _cram[index] : 0; }

	/**
	 * Draw into an 8-bit surface: pixel = colourBase + 16 * line + colour.
	 * @param priority draw only the cells with (true) or without the VDP
	 *                 priority bit; call twice to get the hardware order
	 * @param plane    0 front, higher further back
	 */
	void drawPlane(Graphics::Surface &target, uint plane, bool priority, byte colourBase, int x = 0, int y = 0) const;

	/**
	 * Compose screens back to front with the VDP's order (all low-priority
	 * cells of every plane, back to front, then the high-priority ones), after
	 * filling the target with the last screen's backdrop.
	 * colourBases[i] is where screen i's palette goes in the 8-bit palette.
	 */
	static void compose(const SegaCdScreen *const screens[], const byte colourBases[], uint count,
						Graphics::Surface &target, int x = 0, int y = 0);

private:
	struct Plane {
		uint width, height;
		Common::Array<uint16> cells;
	};

	uint16 _cram[kColours];
	Plane _planes[kMaxPlanes];
	uint _planeCount;
	Common::Array<byte> _tiles;
};

/**
 * A Sega CD sprite bank: talking portraits (files 1782-1850), figures,
 * the ornithopter, small icon sets. Verified layout (FINDINGS.md, "Sprite
 * banks"; reference decoder scripts/segacd_sprites.py), big-endian:
 *
 *   +0  word  offset T of the tile block
 *   +2  word  offset F of the frame table
 *   +4  (when F > 4) word 1, two words not decoded, 15 CRAM colours for
 *       colours 1-15, 0xffff
 *   F   words: frame offsets relative to F; the first gives the count
 *   frame: word size ((width << 8) | height, not needed to draw), then
 *       pieces of 4 bytes: x in pixels; y in tiles << 4 | VDP sprite size
 *       ((width - 1) << 2 | (height - 1), in tiles); the VDP attribute word
 *       (tile 0x07ff, hflip 0x0800, vflip 0x1000); a piece's tiles are
 *       column-major, as the VDP's
 *   T   word  tile count, then 32-byte 4-bit tiles
 *
 * Frames carry no position; where the parts of a portrait go is measured
 * (segacd_game.cpp, kPortraits).
 */
class SegaCdSpriteBank {
public:
	bool parse(const Common::Array<byte> &data);
	uint frameCount() const { return _frames.size(); }
	bool hasPalette() const { return _hasPalette; }
	/** Colours 1-15 as RGB triplets (colour 0 is transparent). */
	void palette(byte rgb[15 * 3]) const;
	/** Draw frame @p index with its top-left at (x, y): pixel = colourBase + colour. */
	void drawFrame(Graphics::Surface &target, uint index, int x, int y, byte colourBase) const;

private:
	struct Piece {
		byte x, yTiles, widthTiles, heightTiles;
		uint16 attribute;
	};
	Common::Array<Common::Array<Piece> > _frames;
	Common::Array<byte> _tiles;
	uint16 _cram[15];
	bool _hasPalette = false;
};

} // namespace Dune

#endif // ENGINES_DUNE_SEGACD_GFX_H

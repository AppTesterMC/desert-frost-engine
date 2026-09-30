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

#ifndef ENGINES_DUNE_ROOM_H
#define ENGINES_DUNE_ROOM_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

namespace Graphics {
struct Surface;
}

namespace Dune {

class Sprite;

/**
 * Renderer for the room descriptions in the .SAL files (PALACE, SIET, VILG,
 * HARK). Which sprite sheet a room is drawn with is decided by the location
 * table (see palace.h), not by the .SAL file.
 *
 * A file starts with a table of 16-bit room offsets. A room is a marker count
 * followed by command words until FFFF:
 *
 *   bit 15 clear      sprite or character marker: bits 0-8 sprite + 1 (1 =
 *                     marker), bit 9 x + 256, bits 10-12 scale, bit 13 flip y,
 *                     bit 14 flip x; then x, y, palette offset
 *   bits 15, !14      polygon filled with a gradient and LFSR noise
 *   bits 15, 14       line
 *
 * The layout and the polygon rasteriser follow madmoose's dune-rust, which was
 * derived from the original's sub_13B59. FINDINGS.md has the details,
 * including what is missing (the sky).
 */
/**
 * vga_draw_line (segvga:1a07 / bresenham_line 1adc, the gfx vtable's line;
 * the same code in both releases): a line from (x0, y0) to (x1, y1) through
 * a 16-bit pattern that rotates left one bit a step, a pixel drawn where the
 * rotated-out bit is set, each pixel clipped to @p clip. A horizontal or
 * vertical line runs from its left or top end and draws both ends; any other
 * line steps max(|dx|, |dy|) times from the start with the error seeded at
 * half the major delta, never drawing the start pixel. Room lines use the
 * pattern 0xffff (CD 13bdb), the map's route 0x5555 (CD 81c5).
 */
void drawVgaLine(Graphics::Surface &target, int x0, int y0, int x1, int y1, byte colour, uint16 pattern,
		const Common::Rect &clip);

class Room {
public:
	explicit Room(const Common::Array<byte> &data) : _data(data) {}

	uint roomCount() const;
	/** The number of character markers a room declares (its first byte). */
	uint markerCount(uint room) const;
	/** The markers' top-left positions, in order. */
	void markerPositions(uint room, Common::Array<Common::Point> &points) const;
	bool draw(uint room, Sprite &sprites, Graphics::Surface &target, Sprite *characters = nullptr,
			const Common::Array<uint16> *markerSprites = nullptr) const;

private:
	const Common::Array<byte> &_data;
};

} // namespace Dune

#endif // ENGINES_DUNE_ROOM_H

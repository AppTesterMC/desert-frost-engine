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

#include "common/endian.h"
#include "common/rect.h"
#include "common/util.h"

#include "graphics/surface.h"

#include "dune/room.h"
#include "dune/sprite.h"

namespace Dune {

void drawVgaLine(Graphics::Surface &target, int x0, int y0, int x1, int y1, byte colour, uint16 pattern,
		const Common::Rect &clip) {
	auto put = [&](int x, int y) {
		if (x >= 0 && x < target.w && y >= 0 && y < target.h)
			*(byte *)target.getBasePtr(x, y) = colour;
	};
	auto bit = [&]() {
		const bool b = (pattern & 0x8000) != 0;
		pattern = (uint16)((pattern << 1) | (pattern >> 15));
		return b;
	};
	const int dx = x1 - x0, dy = y1 - y0;
	if (dy == 0) {
		// segvga:1a3a: from the left end, |dx| + 1 pixels; the row clipped once.
		if (y0 < clip.top || y0 >= clip.bottom)
			return;
		const int sx = dx < 0 ? x1 : x0;
		for (int i = 0; i <= ABS(dx); ++i)
			if (bit() && sx + i >= clip.left && sx + i < clip.right)
				put(sx + i, y0);
		return;
	}
	const int ystep = dy < 0 ? -1 : 1;
	if (dx == 0) {
		// segvga:1a86: from the top end, the start row clamped into 0..199.
		int count = ABS(dy);
		int y = ystep < 0 ? y0 - count : y0;
		if (y >= 200)
			return;
		if (y < 0) {
			count += y;
			y = 0;
		}
		if (x0 < clip.left || x0 >= clip.right)
			return;
		for (int i = 0; i <= count; ++i)
			if (bit() && y + i >= clip.top && y + i < clip.bottom)
				put(x0, y + i);
		return;
	}
	// segvga:1afb: the general case.
	const int xstep = dx < 0 ? -1 : 1;
	const int adx = ABS(dx), ady = ABS(dy);
	const bool xMajor = adx > ady;
	const int major = xMajor ? adx : ady, minor = xMajor ? ady : adx;
	int err = major >> 1;
	int x = x0, y = y0;
	for (int i = 0; i < major; ++i) {
		err += minor;
		if (err >= major) {
			err -= major;
			x += xstep;
			y += ystep;
		} else if (xMajor) {
			x += xstep;
		} else {
			y += ystep;
		}
		if (bit() && x >= clip.left && x < clip.right && y >= clip.top && y < clip.bottom)
			put(x, y);
	}
}

namespace {

// Rasterise one polygon edge into a per-scanline x table, exactly as the
// original does. This follows draw_edge() in Thomas Fach-Pedersen's dune-rust
// (crates/dune/src/room_renderer/mod.rs), his transcription of the original
// routine. See CREDITS.md for its licensing status.
void traceEdge(int x0, int y0, int x1, int y1, int16 *xs, uint &count, uint limit) {
	const int dx = ABS(x1 - x0), dy = ABS(y1 - y0);
	if (!dx && !dy)
		return;
	if (!dy) {
		if (count < limit)
			xs[count++] = MIN(x0, x1);
		return;
	}
	if (!dx) {
		for (int y = y0; y <= y1 && count < limit; ++y)
			xs[count++] = x0;
		return;
	}

	const int signX = x0 < x1 ? 1 : -1, signY = y0 < y1 ? 1 : -1;
	int stepX = signX, stepY = signY, minorDelta = dy, majorDelta = dx;
	if (dx > dy) {
		stepY = 0;
	} else {
		SWAP(minorDelta, majorDelta);
		stepX = 0;
	}

	int accumulator = majorDelta / 2;
	for (int i = 0; i < majorDelta; ++i) {
		accumulator += minorDelta;
		int moveX, moveY;
		if (accumulator >= majorDelta) {
			accumulator -= majorDelta;
			moveX = signX;
			moveY = signY;
		} else {
			moveX = stepX;
			moveY = stepY;
		}
		if (moveY == 1 && count < limit)
			xs[count++] = x0;
		x0 += moveX;
	}
}

} // namespace

uint Room::roomCount() const {
	if (_data.size() < 2)
		return 0;
	return READ_LE_UINT16(_data.data()) / 2;
}

uint Room::markerCount(uint room) const {
	if (room >= roomCount())
		return 0;
	const uint32 position = READ_LE_UINT16(_data.data() + room * 2);
	return position < _data.size() ? _data[position] : 0;
}

void Room::markerPositions(uint room, Common::Array<Common::Point> &points) const {
	points.clear();
	if (room >= roomCount())
		return;
	const byte *data = _data.data();
	const uint32 size = _data.size();
	uint32 position = READ_LE_UINT16(data + room * 2) + 1;
	while (position + 2 <= size) {
		const byte id = data[position], modifier = data[position + 1];
		position += 2;
		if (id == 0xff && modifier == 0xff)
			return;
		if (modifier & 0x80) {
			if (modifier & 0x40) {
				position += 8;
			} else {
				// A polygon: gradients, start point, right side up to 0x4000, left side up to 0x8000.
				position += 2 + 4;
				uint16 x;
				do {
					if (position + 4 > size)
						return;
					x = READ_LE_UINT16(data + position);
					position += 4;
				} while (!(x & 0x4000));
				if (!(x & 0x8000)) {
					do {
						if (position + 4 > size)
							return;
						x = READ_LE_UINT16(data + position);
						position += 4;
					} while (!(x & 0x8000));
				}
			}
		} else {
			if (position + 3 > size)
				return;
			if (((id | (modifier << 8)) & 0x01ff) == 1)
				points.push_back(Common::Point(data[position] + ((modifier & 0x02) ? 256 : 0), data[position + 1]));
			position += 3;
		}
	}
}

bool Room::draw(uint room, Sprite &sprites, Graphics::Surface &target, Sprite *characters,
		const Common::Array<uint16> *markerSprites) const {
	if (room >= roomCount())
		return false;

	const byte *data = _data.data();
	const uint32 size = _data.size();
	uint32 position = READ_LE_UINT16(data + room * 2) + 1; // Skip the marker count.
	uint markerIndex = 0;

	while (position + 2 <= size) {
		const byte id = data[position];
		const byte modifier = data[position + 1];
		position += 2;
		if (id == 0xff && modifier == 0xff)
			return true;

		if (modifier & 0x80) {
			if (modifier & 0x40) {
				if (position + 8 > size)
					return false;
				// loc_13BC9 (floppy likewise): the raw words, the pattern 0xffff
				// (bp), clipped to the game area (ds:20920: 0, 0, 320, 152).
				const int x1 = (int16)READ_LE_UINT16(data + position);
				const int y1 = (int16)READ_LE_UINT16(data + position + 2);
				const int x2 = (int16)READ_LE_UINT16(data + position + 4);
				const int y2 = (int16)READ_LE_UINT16(data + position + 6);
				position += 8;
				drawVgaLine(target, x1, y1, x2, y2, id, 0xffff, Common::Rect(0, 0, 320, 152));
			} else {
				// Filled polygon: colour (8.8 fixed point) plus horizontal and
				// vertical gradients, textured with 2 bits of Galois LFSR noise.
				// The right side runs from the start point to the vertex
				// flagged 0x4000; the left side from the start point through
				// the following vertices (up to the one flagged 0x8000) and on
				// to the last right vertex.
				if (position + 6 > size)
					return false;
				const uint16 command = id | (modifier << 8);
				uint16 noiseState = (command & 0x3e00) ? 1 : 0;
				const uint16 noiseMask = (command & 0x3e00) | 2;
				const bool reverseGradient = (command & 0x0100) != 0;
				const int hGradient = 16 * (int8)data[position];
				const int vGradient = 16 * (int8)data[position + 1];
				position += 2;

				Common::Array<Common::Point> right, left;
				right.push_back(Common::Point(READ_LE_UINT16(data + position) & 0x3fff, READ_LE_UINT16(data + position + 2)));
				position += 4;
				uint16 x;
				do {
					if (position + 4 > size)
						return false;
					x = READ_LE_UINT16(data + position);
					right.push_back(Common::Point(x & 0x3fff, READ_LE_UINT16(data + position + 2)));
					position += 4;
				} while (!(x & 0x4000));
				if (!(x & 0x8000)) {
					do {
						if (position + 4 > size)
							return false;
						x = READ_LE_UINT16(data + position);
						left.push_back(Common::Point(x & 0x3fff, READ_LE_UINT16(data + position + 2)));
						position += 4;
					} while (!(x & 0x8000));
				}

				int16 rightXs[200], leftXs[200];
				memset(rightXs, 0, sizeof(rightXs));
				memset(leftXs, 0, sizeof(leftXs));
				uint rightCount = 0, leftCount = 0;
				for (uint i = 1; i < right.size(); ++i)
					traceEdge(right[i - 1].x, right[i - 1].y, right[i].x, right[i].y, rightXs, rightCount, 200);
				Common::Point last = right[0];
				for (uint i = 0; i < left.size(); ++i) {
					traceEdge(last.x, last.y, left[i].x, left[i].y, leftXs, leftCount, 200);
					last = left[i];
				}
				traceEdge(last.x, last.y, right.back().x, right.back().y, leftXs, leftCount, 200);

				uint16 lineColour = (uint16)id << 8;
				const int rows = MIN<int>(right.back().y - right[0].y, 200);
				for (int row = 0; row < rows; ++row) {
					int x0 = leftXs[row], x1 = rightXs[row];
					if (x0 > x1)
						SWAP(x0, x1);
					const int y = right[0].y + row;
					uint16 colour = lineColour;
					for (int px = x0; px <= x1; ++px) {
						const bool lsb = noiseState & 1;
						noiseState >>= 1;
						if (lsb)
							noiseState ^= noiseMask;
						const int drawX = reverseGradient ? x0 + (x1 - px) : px;
						if (drawX >= 0 && drawX < target.w && y >= 0 && y < target.h)
							*(byte *)target.getBasePtr(drawX, y) = (byte)((noiseState & 3) + (colour >> 8) - 1);
						colour += hGradient;
					}
					lineColour += vGradient;
				}
			}
		} else {
			if (position + 3 > size)
				return false;
			if (((id | (modifier << 8)) & 0x01ff) != 1) {
				// Command word: bits 0-8 sprite + 1, bit 9 x + 256, bits 10-12
				// scale, bit 13 flip y, bit 14 flip x; then x, y, palette offset.
				const uint16 command = id | (modifier << 8);
				const int x = data[position] + ((command & 0x0200) ? 256 : 0);
				const int y = data[position + 1];
				sprites.drawFrame((command & 0x01ff) - 1, &target, x, y, (command & 0x4000) != 0, (command & 0x2000) != 0,
						(command >> 10) & 7, data[position + 2]);
			} else if (characters && markerSprites && markerIndex < markerSprites->size()) {
				// Marker positions are the top-left of a standing character. The
				// marker number is room-local; the game maps it to a PERS frame.
				characters->drawFrame((*markerSprites)[markerIndex], &target,
						data[position] + ((modifier & 0x02) ? 256 : 0), data[position + 1],
						(modifier & 0x40) != 0, (modifier & 0x20) != 0,
						(modifier >> 2) & 7, data[position + 2]);
			}
			if (((id | (modifier << 8)) & 0x01ff) == 1)
				++markerIndex;
			position += 3;
		}
	}

	return false;
}

} // namespace Dune

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
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "dune/debug.h"
#include "dune/panel.h"
#include "dune/segacd_panel.h"
#include "dune/segacd_resources.h"

namespace Dune {

namespace {

const uint kPanelBlocksFile = 5;
const uint kPanelFrameFile = 30;
const uint kLeftBlock = 0;
const uint kMapBlock = 1;    ///< the globe between two figures (the recording's map panel)
const uint kMapPad = 48;
const uint kFirstFace = 7;   ///< faces 7-15: characters 0-8 in DIALOGUE order (guessed from the pictures)
const uint kSun = 16, kMoon = 17;
const uint kPadWithCentre = 19;

// Pads without the red centre, by exit mask (measured on the tiles; the
// set has no variant for the other masks).
const int8 kPadNoCentre[16] = { 35, -1, 37, -1, 39, 40, 41, 42, 43, -1, -1, -1, 47, -1, -1, 44 };

const int16 kArrowRects[4][4] = {
	{ 272, 184, 280, 196 }, // up: the pad's top middle
	{ 284, 194, 296, 206 }, // right
	{ 272, 204, 280, 216 }, // down
	{ 256, 194, 268, 206 }  // left
};

} // namespace

bool SegaCdPanel::load(SegaCdArchive &archive, StartupLog &log) {
	_loaded = false;
	Common::Array<byte> frame;
	if (!archive.load(kPanelBlocksFile, _blocks) || !archive.load(kPanelFrameFile, frame) || !_frame.parse(frame) ||
			_blocks.size() < 8) {
		log.line("Sega CD: the panel files (5, 30) could not be read");
		return false;
	}
	const uint32 table = READ_BE_UINT16(_blocks.data());
	if (table + 2 > _blocks.size())
		return false;
	const uint count = READ_BE_UINT16(_blocks.data() + table) / 2;
	_index.clear();
	for (uint i = 0; i < count; ++i) {
		if (table + 2 * i + 2 > _blocks.size())
			return false;
		const uint32 at = table + READ_BE_UINT16(_blocks.data() + table + 2 * i);
		Block b;
		if (at + 2 > _blocks.size())
			return false;
		b.width = _blocks[at];
		b.height = _blocks[at + 1];
		b.offset = at + 2;
		if (b.offset + 32u * b.width * b.height > _blocks.size())
			return false;
		_index.push_back(b);
	}
	if (_index.size() <= kPadWithCentre + 15)
		return false;
	_loaded = true;
	log.line(Common::String::format("Sega CD: panel loaded (%u tile blocks)", _index.size()));
	return true;
}

void SegaCdPanel::palette(const SegaCdScreen *room, byte rgb[kColours * 3]) const {
	// Start from file 30's own CRAM, then take line 0 from the room.
	_frame.palette(rgb);
	if (room) {
		byte roomRgb[SegaCdScreen::kColours * 3];
		room->palette(roomRgb);
		memcpy(rgb, roomRgb, 16 * 3);
	}
	// The text: the brightest colour of line 0 (measured: the recording's
	// command text is the lightest tone of the panel's tint).
	uint best = 0, bestLevel = 0;
	for (uint i = 1; i < 16; ++i) {
		const uint level = rgb[3 * i] * 3 + rgb[3 * i + 1] * 6 + rgb[3 * i + 2];
		if (level > bestLevel) {
			bestLevel = level;
			best = i;
		}
	}
	const_cast<SegaCdPanel *>(this)->_textColour = (byte)best;
}

void SegaCdPanel::drawBlock(Graphics::Surface &target, uint index, int x0, int y0, byte base, byte line,
		bool opaque) const {
	if (index >= _index.size())
		return;
	const Block &b = _index[index];
	for (uint t = 0; t < b.width * b.height; ++t) {
		const uint tx = t / b.height, ty = t % b.height; // column-major
		const byte *tile = _blocks.data() + b.offset + 32 * t;
		for (int y = 0; y < 8; ++y) {
			const int py = y0 + (int)ty * 8 + y;
			if (py < 0 || py >= target.h)
				continue;
			for (int x = 0; x < 8; ++x) {
				const byte pair = tile[y * 4 + x / 2];
				const byte c = (x & 1) ? (pair & 0x0f) : (pair >> 4);
				const int px = x0 + (int)tx * 8 + x;
				if ((c || opaque) && px >= 0 && px < target.w)
					*(byte *)target.getBasePtr(px, py) = (byte)(base + 16 * line + c);
			}
		}
	}
}

uint SegaCdPanel::padFor(uint mask, bool canLeave) const {
	// Guess (FINDINGS.md): the red centre marks a room with a way out of the
	// place; the recording's palace room without one shows pad 39.
	mask &= 15;
	if (!canLeave && kPadNoCentre[mask] >= 0)
		return (uint)kPadNoCentre[mask];
	return kPadWithCentre + mask;
}

void SegaCdPanel::draw(Graphics::ManagedSurface &view, byte base, const State &state, const Panel &font) const {
	if (!_loaded)
		return;
	Graphics::Surface &target = *view.surfacePtr();
	// The left block and the frame (file 30's first plane, from x = 88).
	drawBlock(target, state.map ? kMapBlock : kLeftBlock, 0, kTop, base, 0, true);
	for (int priority = 0; priority < 2; ++priority)
		_frame.drawPlane(target, 0, priority != 0, base, kFrameX, kTop);

	// The pad; a pressed arrow is shown by the pad without that exit, for
	// the moment the original's pressed-state tiles are not decoded.
	uint mask = state.exitMask;
	if (state.pressedArrow >= 0)
		mask &= ~(1u << state.pressedArrow);
	drawBlock(target, state.map ? kMapPad : padFor(mask, state.canLeave), kPadX, kPadY, base, 1, true);
	if (state.map) {
		for (uint r = 0; r < state.rows.size() && r < kRows; ++r)
			font.drawText(view, state.rows[r].c_str(), kTextX, kTextY + (int)r * kRowHeight, (byte)(base + _textColour), true);
		return; // the map's panel has no day box or companions
	}

	// Day box: sun or moon, then the day number in its lower half.
	drawBlock(target, state.night ? kMoon : kSun, kDayX + (state.night ? 14 : 2), kDayY, base, 1, false);
	const Common::String day = Common::String::format("%u", state.day);
	font.drawText(view, day.c_str(), kDayX + 22 - font.textWidth(day.c_str(), true), kDayY + 8,
			(byte)(base + _textColour), true);

	// Companions.
	for (uint i = 0; i < 2; ++i)
		if (state.companions[i] < 9)
			drawBlock(target, kFirstFace + state.companions[i], i ? kFaceX1 : kFaceX0, kFaceY, base, 1, true);

	// Commands.
	for (uint r = 0; r < state.rows.size() && r < kRows; ++r) {
		Common::String text = state.rows[r];
		while (text.size() > 1 && kTextX + font.textWidth(text.c_str(), true) > kTextRight)
			text.deleteLastChar();
		font.drawText(view, text.c_str(), kTextX, kTextY + (int)r * kRowHeight,
				(byte)(base + ((int)r == state.highlightedRow ? 1 : _textColour)), true);
	}
}

int SegaCdPanel::arrowAt(int x, int y) {
	for (int d = 0; d < 4; ++d)
		if (x >= kArrowRects[d][0] && x < kArrowRects[d][2] && y >= kArrowRects[d][1] && y < kArrowRects[d][3])
			return d;
	return -1;
}

int SegaCdPanel::rowAt(int x, int y) {
	if (x < kTextX - 4 || x > kTextRight || y < kTextY - 1)
		return -1;
	const int row = (y - (kTextY - 1)) / kRowHeight;
	return row < kRows ? row : -1;
}

} // namespace Dune

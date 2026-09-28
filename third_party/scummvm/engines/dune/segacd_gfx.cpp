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
#include "graphics/surface.h"

#include "dune/segacd_gfx.h"

namespace Dune {

bool SegaCdScreen::isScreen(const Common::Array<byte> &data) {
	SegaCdScreen screen;
	return screen.parse(data);
}

bool SegaCdScreen::parse(const Common::Array<byte> &data) {
	_planeCount = 0;
	_tiles.clear();
	const uint32 size = data.size();
	if (size < 0x8a)
		return false;
	const byte *d = data.data();
	const uint32 tileBlock = READ_BE_UINT16(d);
	for (uint i = 0; i < kColours; ++i)
		_cram[i] = READ_BE_UINT16(d + 2 + 2 * i);
	const uint listLength = READ_BE_UINT16(d + 0x82);
	if (listLength < 2 || listLength > 2 * kMaxPlanes || (listLength & 1))
		return false;
	const uint planes = listLength / 2;
	for (uint p = 0; p < planes; ++p) {
		const uint32 at = p == 0 ? 0x82 + listLength : 0x82 + READ_BE_UINT16(d + 0x82 + 2 * p);
		if (at + 2 > size)
			return false;
		Plane &plane = _planes[p];
		plane.width = d[at];
		plane.height = d[at + 1];
		if (plane.width != 40 || !plane.height || plane.height > 32 || at + 2 + 2u * plane.width * plane.height > size)
			return false;
		plane.cells.resize(plane.width * plane.height);
		for (uint i = 0; i < plane.cells.size(); ++i)
			plane.cells[i] = READ_BE_UINT16(d + at + 2 + 2 * i);
	}
	if (tileBlock + 2 > size)
		return false;
	const uint32 tileCount = READ_BE_UINT16(d + tileBlock);
	if (!tileCount || tileBlock + 2 + 32 * tileCount != size)
		return false;
	_tiles.resize(32 * tileCount);
	memcpy(_tiles.data(), d + tileBlock + 2, _tiles.size());
	_planeCount = planes;
	return true;
}

uint SegaCdScreen::height() const {
	uint h = 0;
	for (uint p = 0; p < _planeCount; ++p)
		h = MAX<uint>(h, _planes[p].height * 8);
	return h;
}

void SegaCdScreen::palette(byte rgb[kColours * 3]) const {
	for (uint i = 0; i < kColours; ++i) {
		const uint16 c = _cram[i];
		rgb[3 * i + 0] = (byte)(((c >> 1) & 7) * 255 / 7);
		rgb[3 * i + 1] = (byte)(((c >> 5) & 7) * 255 / 7);
		rgb[3 * i + 2] = (byte)(((c >> 9) & 7) * 255 / 7);
	}
}

void SegaCdScreen::drawPlane(Graphics::Surface &target, uint planeIndex, bool priority, byte colourBase, int x0,
		int y0) const {
	if (planeIndex >= _planeCount || target.format.bytesPerPixel != 1)
		return;
	const Plane &plane = _planes[planeIndex];
	const uint tileCount = _tiles.size() / 32;
	for (uint ty = 0; ty < plane.height; ++ty) {
		for (uint tx = 0; tx < plane.width; ++tx) {
			const uint16 cell = plane.cells[ty * plane.width + tx];
			if (((cell & 0x8000) != 0) != priority)
				continue;
			const uint tile = cell & 0x7ff;
			if (tile >= tileCount)
				continue;
			const byte *t = _tiles.data() + 32 * tile;
			const byte line = (byte)(((cell >> 13) & 3) * 16);
			const bool hflip = (cell & 0x0800) != 0, vflip = (cell & 0x1000) != 0;
			for (int y = 0; y < 8; ++y) {
				const int py = y0 + (int)ty * 8 + (vflip ? 7 - y : y);
				if (py < 0 || py >= target.h)
					continue;
				byte *row = (byte *)target.getBasePtr(0, py);
				for (int x = 0; x < 8; ++x) {
					const byte pair = t[y * 4 + x / 2];
					const byte c = (x & 1) ? (pair & 0x0f) : (pair >> 4);
					if (!c)
						continue;
					const int px = x0 + (int)tx * 8 + (hflip ? 7 - x : x);
					if (px >= 0 && px < target.w)
						row[px] = (byte)(colourBase + line + c);
				}
			}
		}
	}
}

void SegaCdScreen::compose(const SegaCdScreen *const screens[], const byte colourBases[], uint count,
		Graphics::Surface &target, int x, int y) {
	if (!count)
		return;
	// The backdrop: colour 0 of the front screen's first line.
	const uint16 h = (uint16)screens[count - 1]->height();
	for (int row = MAX(0, y); row < MIN<int>(target.h, y + h); ++row)
		memset(target.getBasePtr(0, row), colourBases[count - 1], target.w);
	for (int priority = 0; priority < 2; ++priority)
		for (uint s = 0; s < count; ++s)
			for (int p = (int)screens[s]->planeCount() - 1; p >= 0; --p)
				screens[s]->drawPlane(target, (uint)p, priority != 0, colourBases[s], x, y);
}

bool SegaCdSpriteBank::parse(const Common::Array<byte> &data) {
	_frames.clear();
	_tiles.clear();
	_hasPalette = false;
	const uint32 size = data.size();
	if (size < 8)
		return false;
	const byte *d = data.data();
	const uint32 tileBlock = READ_BE_UINT16(d), frameTable = READ_BE_UINT16(d + 2);
	if (frameTable + 2 > size || tileBlock + 2 > size || frameTable >= tileBlock)
		return false;
	if (frameTable >= 0x28 && READ_BE_UINT16(d + 4) == 1) {
		for (uint i = 0; i < 15; ++i)
			_cram[i] = READ_BE_UINT16(d + 0x0a + 2 * i);
		_hasPalette = true;
	}
	const uint count = READ_BE_UINT16(d + frameTable) / 2;
	if (!count || frameTable + 2 * count > tileBlock)
		return false;
	for (uint f = 0; f < count; ++f) {
		const uint32 start = frameTable + READ_BE_UINT16(d + frameTable + 2 * f);
		const uint32 end = f + 1 < count ? frameTable + READ_BE_UINT16(d + frameTable + 2 * (f + 1)) : tileBlock;
		Common::Array<Piece> pieces;
		for (uint32 p = start + 2; start < end && end <= size && p + 4 <= end; p += 4) {
			Piece piece;
			piece.x = d[p];
			piece.yTiles = d[p + 1] >> 4;
			piece.widthTiles = ((d[p + 1] >> 2) & 3) + 1;
			piece.heightTiles = (d[p + 1] & 3) + 1;
			piece.attribute = READ_BE_UINT16(d + p + 2);
			pieces.push_back(piece);
		}
		_frames.push_back(pieces);
	}
	const uint32 tiles = READ_BE_UINT16(d + tileBlock);
	const uint32 available = MIN<uint32>(32 * tiles, size - tileBlock - 2);
	_tiles.resize(available);
	memcpy(_tiles.data(), d + tileBlock + 2, available);
	return true;
}

void SegaCdSpriteBank::palette(byte rgb[15 * 3]) const {
	for (uint i = 0; i < 15; ++i) {
		const uint16 c = _hasPalette ? _cram[i] : (uint16)(i * 0x111 & 0xeee);
		rgb[3 * i + 0] = (byte)(((c >> 1) & 7) * 255 / 7);
		rgb[3 * i + 1] = (byte)(((c >> 5) & 7) * 255 / 7);
		rgb[3 * i + 2] = (byte)(((c >> 9) & 7) * 255 / 7);
	}
}

void SegaCdSpriteBank::drawFrame(Graphics::Surface &target, uint index, int x0, int y0, byte colourBase) const {
	if (index >= _frames.size() || target.format.bytesPerPixel != 1)
		return;
	const uint tileCount = _tiles.size() / 32;
	for (uint p = 0; p < _frames[index].size(); ++p) {
		const Piece &piece = _frames[index][p];
		const bool hflip = (piece.attribute & 0x0800) != 0, vflip = (piece.attribute & 0x1000) != 0;
		const uint w = piece.widthTiles, h = piece.heightTiles;
		for (uint cx = 0; cx < w; ++cx) {
			for (uint cy = 0; cy < h; ++cy) {
				const uint tile = (piece.attribute & 0x7ff) + cx * h + cy;
				if (tile >= tileCount)
					continue;
				const byte *t = _tiles.data() + 32 * tile;
				for (int yy = 0; yy < 8; ++yy) {
					for (int xx = 0; xx < 8; ++xx) {
						const byte pair = t[yy * 4 + xx / 2];
						const byte c = (xx & 1) ? (pair & 0x0f) : (pair >> 4);
						if (!c)
							continue;
						const int px = x0 + piece.x + (hflip ? (int)(w - 1 - cx) * 8 + 7 - xx : (int)cx * 8 + xx);
						const int py = y0 + piece.yTiles * 8 + (vflip ? (int)(h - 1 - cy) * 8 + 7 - yy : (int)cy * 8 + yy);
						if (px >= 0 && px < target.w && py >= 0 && py < target.h)
							*(byte *)target.getBasePtr(px, py) = (byte)(colourBase + c - 1);
					}
				}
			}
		}
	}
}

} // namespace Dune

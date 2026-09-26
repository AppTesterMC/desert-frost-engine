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
 * MapRenderer is a transcription of madmoose's dune-rust map_renderer.rs
 * (reuse permitted); the icon placement follows the CD executable's
 * sub_15DCE / sub_1B647 / sub_1C343 (OpenRakis' DNCDPRG_RECENT.ASM) and the
 * panel layout dune-rust's wasm_map and wasm_globe front-ends.
 */

#include "dune/map.h"

#include "common/endian.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/debug.h"
#include "dune/panel.h"
#include "dune/resource.h"
#include "dune/scene.h"
#include "dune/sprite.h"
#include "dune/text.h"
#include "dune/world.h"

namespace Dune {

// ---- MapRenderer --------------------------------------------------------------

MapRenderer::MapRenderer(const Common::Array<byte> &map, const Common::Array<byte> &tablat) : _map(map) {
	memset(_tablat, 0, sizeof(_tablat));
	for (uint i = 0; i < 99 && (i + 1) * 8 <= tablat.size(); ++i) {
		_tablat[i].offset = READ_BE_UINT16(tablat.data() + 8 * i);
		_tablat[i].length = READ_BE_UINT16(tablat.data() + 8 * i + 2);
	}
	memset(_buffer, 0, sizeof(_buffer));
}

uint16 MapRenderer::rowLength(uint16 row) const {
	const uint index = row < 99 ? 98 - row : row - 98;
	return 2 * _tablat[MIN<uint>(index, 98)].length;
}

uint16 MapRenderer::rowOffset(uint16 row) const {
	const uint index = row < 99 ? 98 - row : row - 98;
	const uint16 offset = _tablat[MIN<uint>(index, 98)].offset;
	return row < 99 ? (uint16)(kMapBase - offset) : (uint16)(kMapBase + offset);
}

byte MapRenderer::mapPixel(uint offset) const {
	if (offset >= _map.size())
		return 0;
	return (byte)((((_map[offset] & 0x0f) << 1) + 1) << 3);
}

void MapRenderer::draw(Graphics::Surface &view, int16 latitude, uint16 longitude) {
	// A latitude is map row latitude + 98 (map_func, seg000:b58b: the TABLAT
	// row is |latitude|; the globe does the same). The rows were once taken
	// as latitude + 80, which drew the terrain 18 rows (72 pixels) north of
	// the places (the floppy recordings: craters beside the palace).
	const uint16 top = (uint16)(latitude + 98);
	for (uint16 i = 0; i < kViewRows; ++i)
		drawBand(view, i, top + i, longitude);
}

void MapRenderer::drawBand(Graphics::Surface &view, uint16 band, uint16 row, uint16 longitude) {
	if (row < kBandBegin || row >= kBandEnd)
		return;
	memset(_buffer, 0, sizeof(_buffer));
	interpolateLine(row, longitude, 140);
	interpolateLine(row + 1, longitude, 1740);
	interpolateVertically(row);
	postProcess(row);
	for (int y = 0; y < 4; ++y) {
		const int screenY = kViewY + 4 * band + y;
		if (screenY >= view.h)
			break;
		for (int x = 0; x < kViewWidth; ++x) {
			const byte b = _buffer[y * 400 + x + 160];
			*(byte *)view.getBasePtr(kViewX + x, screenY) = (byte)(((b >> 4) & 0x0f) + 0x10);
		}
	}
}

void MapRenderer::interpolateLine(uint16 row, uint16 longitude, uint16 output) {
	const int len = rowLength(row);
	const uint base = rowOffset(row);
	if (!len)
		return;
	const uint32 rotation = (uint32)longitude * (uint32)len; // 16.16: longitude / 65536 * len
	const int subpixel = (rotation >> 14) & 3;
	int rotationOffset = (int)(rotation >> 16);
	int out = (int)output - subpixel;

	int len1 = len, len2;
	if (len1 < 88) {
		out += 2 * (88 - len1);
		rotationOffset -= len1 / 2;
		if (rotationOffset < 0)
			rotationOffset += len1;
		const int len0 = len1;
		len1 -= rotationOffset;
		len2 = (len0 + 1) - len1;
	} else {
		rotationOffset -= 88 / 2;
		if (rotationOffset < 0)
			rotationOffset += len1;
		len1 -= rotationOffset;
		len2 = 88 + 1 - len1;
		if (len2 < 0)
			len2 = 0;
	}

	int p0 = mapPixel(base + (rotationOffset > 0 ? rotationOffset - 1 : len - 1));
	auto emit = [&](int p1) {
		const int d = (p1 - p0) / 4;
		for (int k = 0; k < 4; ++k) {
			if (out >= 0 && out < (int)sizeof(_buffer))
				_buffer[out] = (byte)p0;
			++out;
			p0 += d;
		}
		p0 = p1;
	};
	for (int i = 0; i < len1; ++i)
		emit(mapPixel(base + i + rotationOffset));
	for (int i = 0; i < len2; ++i)
		emit(mapPixel(base + i));
}

void MapRenderer::interpolateVertically(uint16 row) {
	uint32 l0Length = rowLength(row) / 2;
	uint32 l4Length = rowLength(row + 1) / 2;
	const bool south = row >= kBands / 2;
	if (south)
		SWAP(l0Length, l4Length);
	if (!l0Length || l4Length < l0Length)
		return;

	for (int direction = 0; direction < 2; ++direction) {
		// Right of the centre first, then left of it.
		const int step = direction == 0 ? 1 : -1;
		int l0 = direction == 0 ? 320 - 4 : 320 - 4 - 1;
		int l1 = l0 + 400, l2 = l1 + 400, l3 = l2 + 400, l4 = l3 + 400;
		if (south) {
			SWAP(l0, l4);
			SWAP(l1, l3);
		}
		const uint16 err = (uint16)(((l4Length - l0Length) << 16) / l0Length);
		const uint16 half = err / 2, quarter = err / 4;
		const byte err1 = (byte)((quarter + 0x80) >> 8);
		const byte err2 = (byte)((half + 0x80) >> 8);
		const byte err3 = (byte)((half + quarter + 0x80) >> 8);
		byte acc1 = err1, acc2 = err2, acc3 = err3;
		uint16 acc4 = err;

		auto at = [&](int index) -> byte & {
			static byte sink;
			if (index < 0 || index >= (int)sizeof(_buffer))
				return sink;
			return _buffer[index];
		};
		for (int n = 0; n < 176; ++n) {
			const byte v0 = at(l0);
			l0 += step;
			const byte v4 = at(l4);
			l4 += step;
			const int8 d = (int8)(((int)v4 - (int)v0) / 4);

			const byte v1 = (byte)(v0 + d);
			at(l1) = v1;
			l1 += step;
			if ((uint)acc1 + err1 > 0xff) {
				at(l1) = v1;
				l1 += step;
			}
			acc1 = (byte)(acc1 + err1);

			const byte v2 = (byte)(v1 + d);
			at(l2) = v2;
			l2 += step;
			if ((uint)acc2 + err2 > 0xff) {
				at(l2) = v2;
				l2 += step;
			}
			acc2 = (byte)(acc2 + err2);

			const byte v3 = (byte)(v2 + d);
			at(l3) = v3;
			l3 += step;
			if ((uint)acc3 + err3 > 0xff) {
				at(l3) = v3;
				l3 += step;
			}
			acc3 = (byte)(acc3 + err3);

			if ((uint32)acc4 + err > 0xffff)
				l4 += step;
			acc4 = (uint16)(acc4 + err);
		}
	}
}

void MapRenderer::postProcess(uint16 row) {
	static const byte kEdges[28][2] = {
		{ 138, 18 }, { 119, 37 }, { 107, 49 }, { 97, 59 }, { 89, 67 }, { 81, 75 }, { 74, 82 },
		{ 68, 88 }, { 63, 93 }, { 57, 99 }, { 52, 104 }, { 48, 108 }, { 44, 112 }, { 39, 117 },
		{ 36, 120 }, { 32, 124 }, { 28, 128 }, { 25, 131 }, { 22, 134 }, { 18, 138 }, { 16, 140 },
		{ 13, 143 }, { 11, 145 }, { 8, 148 }, { 5, 151 }, { 3, 153 }, { 2, 154 }, { 1, 155 }
	};
	static const byte kLeft[4] = { 0xc0, 0x90, 0x80, 0x70 };
	static const byte kRight[4] = { 0x70, 0x80, 0x90, 0xc0 };
	const bool north = row < kBands / 2;
	for (int i = 0; i < 4; ++i) {
		const int index = north ? 4 * (row - kBandBegin) + i : 4 * (kBandEnd - row - 1) + (3 - i);
		if (index < 0 || index >= 28)
			continue;
		const int w = kEdges[index][0], v = kEdges[index][1];
		int dst = 160 + 400 * i;
		if (w > 4)
			for (int k = 0; k < w - 4; ++k)
				_buffer[dst++] = 0;
		const int n = MIN(4, w);
		for (int k = 0; k < n; ++k)
			_buffer[dst++] = kLeft[4 - n + k];
		dst += 2 * v;
		for (int k = 0; k < n && dst < (int)sizeof(_buffer); ++k)
			_buffer[dst++] = kRight[k];
		if (w > 4)
			for (int k = 4; k < w && dst < (int)sizeof(_buffer); ++k)
				_buffer[dst++] = 0;
	}
}

bool MapRenderer::unproject(int16 latitude, uint16 longitude, int x, int y, int16 &placeLatitude,
		uint16 &placeLongitude) const {
	const int band = (y - kViewY - 2) / 4;
	if (band < 0 || band >= kViewRows)
		return false;
	placeLatitude = (int16)(latitude + band);
	const int row = placeLatitude + 98; // as draw() and project()
	if (row < kBandBegin || row >= kBandEnd)
		return false;
	const int len = rowLength((uint16)row);
	if (!len)
		return false;
	const uint32 rotation = (uint32)longitude * (uint32)len;
	const int subpixel = (rotation >> 14) & 3;
	int out = 140 - subpixel;
	int first = (int)(rotation >> 16);
	if (len < 88) {
		out += 2 * (88 - len);
		first -= len / 2;
	} else {
		first -= 44;
	}
	if (first < 0)
		first += len;
	const int column = (x - kViewX - out - 2 + 160) / 4;
	const int cell = ((first + column) % len + len) % len;
	placeLongitude = (uint16)(((uint32)cell << 16) / (uint32)len);
	return true;
}

bool MapRenderer::project(int16 latitude, uint16 longitude, int16 placeLatitude, uint16 placeLongitude,
		int &x, int &y) const {
	const int top = latitude + 98;
	const int row = placeLatitude + 98;
	const int band = row - top;
	if (band < 0 || band >= kViewRows || row < kBandBegin || row >= kBandEnd)
		return false;
	const int len = rowLength((uint16)row);
	if (!len)
		return false;
	const uint32 rotation = (uint32)longitude * (uint32)len;
	const int subpixel = (rotation >> 14) & 3;
	int out = 140 - subpixel;
	int first = (int)(rotation >> 16);
	if (len < 88) {
		out += 2 * (88 - len);
		first -= len / 2;
	} else {
		first -= 44;
	}
	if (first < 0)
		first += len;
	int column = (int)(((uint32)placeLongitude * (uint32)len) >> 16) - first;
	if (column < 0)
		column += len;
	if (column > 88)
		return false;
	x = kViewX + (out + 4 * column + 2) - 160;
	y = kViewY + 4 * band + 2;
	return x >= 0 && x < 320;
}

// ---- MapScreen ----------------------------------------------------------------

MapScreen::MapScreen(OSystem *system, Resource &resources, StartupLog &log, World &world) :
		_system(system), _resources(resources), _log(log), _world(world), _mode(kFlat), _selecting(false),
		_destination(-1), _renderer(nullptr), _icons(nullptr), _fresk(nullptr), _font(nullptr), _latitude(-4),
		_longitude(0x1915), _rotation(0), _tilt(0), _density(false), _flying(false), _flightLongitude(0),
		_flightLatitude(0), _flightDestination(-1), _results(0) {
	memset(_index, 0xff, sizeof(_index));
}

MapScreen::~MapScreen() {
	delete _renderer;
	delete _icons;
	delete _fresk;
}

bool MapScreen::loadFlat() {
	if (_renderer)
		return true;
	// The live map is the world's: its stage bits (vegetation, areas) change.
	if (_world.map().size() < MapRenderer::kMapSize || !_resources.load("TABLAT.BIN", _tablatData) ||
			_tablatData.size() < MapRenderer::kTablatSize) {
		_log.line("Map: MAP.HSQ or TABLAT.BIN missing");
		return false;
	}
	_renderer = new MapRenderer(_world.map(), _tablatData);
	return true;
}

bool MapScreen::loadGlobe() {
	if (!_icons) {
		Common::Array<byte> data;
		if (!_resources.load("ONMAP.HSQ", data)) {
			_log.line("Map: ONMAP.HSQ missing");
			return false;
		}
		_icons = new Sprite(_system, data);
	}
	if (!_fresk) {
		Common::Array<byte> data;
		if (_resources.load("FRESK.HSQ", data))
			_fresk = new Sprite(_system, data);
	}
	return true;
}

bool MapScreen::open(Mode mode, bool selectDestination) {
	_mode = mode;
	_selecting = selectDestination;
	_destination = -1;
	_density = false;
	_caption = true;
	_captionStart = _system->getMillis();
	_flying = false;
	_trail.clear();
	_results = 0;
	if (!loadGlobe())
		return false;
	if (mode == kFlat && !loadFlat())
		return false;
	centreOn(_world.currentLocation());
	return true;
}

bool MapScreen::selectPoint(int x, int y) {
	if (_mode != kFlat || !_renderer)
		return false;
	int16 lat;
	uint16 lng;
	if (!_renderer->unproject(_latitude, _longitude, x, y, lat, lng))
		return false;
	_pointLatitude = lat;
	_pointLongitude = lng;
	_destination = -2;
	return true;
}

void MapScreen::centreOn(uint locationIndex) {
	const Location l = _world.location(locationIndex);
	// The renderer's first band is the view latitude; eighteen bands down is
	// the middle of the view, where the place should sit.
	_latitude = CLIP<int16>((int16)(l.latitude - 18), -75, 75);
	_longitude = l.longitude;
	_rotation = l.longitude;
	_tilt = CLIP<int>(l.latitude, -96, 96);
}

void MapScreen::scroll(int dx, int dy) {
	_longitude = (uint16)(_longitude + dx * 0x1002);
	_latitude = CLIP<int16>((int16)(_latitude + dy * 12), -75, 75);
}

void MapScreen::rotate(int deltaRotation, int deltaTilt) {
	_rotation = (uint16)(_rotation + deltaRotation);
	_tilt = CLIP<int>(_tilt + deltaTilt, -96, 96);
}

int MapScreen::hitLocation(int x, int y) const {
	if (x < 0 || x >= 320 || y < 0 || y >= 152)
		return -1;
	const byte index = _index[y * 320 + x];
	return index == 0xff ? -1 : (int)index;
}

int MapScreen::hitArrow(int x, int y) const {
	struct Zone {
		int16 x0, y0, x1, y1;
	};
	// dune-rust wasm_map.click / wasm_globe.click.
	static const Zone flat[5] = { { 267, 162, 284, 171 }, { 285, 171, 297, 184 }, { 267, 184, 284, 193 },
								  { 254, 171, 266, 184 }, { 266, 171, 285, 184 } };
	static const Zone globe[5] = { { 38, 159, 54, 172 }, { 54, 168, 72, 185 }, { 38, 183, 54, 199 },
								   { 20, 168, 37, 185 }, { 36, 172, 57, 182 } };
	const Zone *zones = _mode == kFlat ? flat : globe;
	// The centre zone overlaps the arrows: test it last.
	for (int i = 0; i < 4; ++i)
		if (x >= zones[i].x0 && x < zones[i].x1 && y >= zones[i].y0 && y < zones[i].y1)
			return i;
	if (x >= zones[4].x0 && x < zones[4].x1 && y >= zones[4].y0 && y < zones[4].y1)
		return 4;
	return -1;
}

bool MapScreen::placeOnScreen(const Location &l, int &x, int &y) const {
	if (_mode == kFlat)
		return _renderer && _renderer->project(_latitude, _longitude, l.latitude, l.longitude, x, y);
	// sub_1B647 in globe mode: four screen pixels per unit, the row
	// length scaling the longitude, centred on (160, 76).
	const int row = CLIP<int>(l.latitude + 98, 0, 196);
	const int deltaLongitude = (int16)(l.longitude - _rotation);
	if (deltaLongitude < -12000 || deltaLongitude > 12000)
		return false;
	const int length = _renderer ? _renderer->rowLength((uint16)row) : 88;
	x = 160 + ((deltaLongitude * 2 * length) / 65536) * 4;
	y = 76 + (l.latitude - _tilt) * 4;
	return (x - 160) * (x - 160) + (y - 76) * (y - 76) <= 76 * 76;
}

void MapScreen::drawIcons(Graphics::ManagedSurface &surface) {
	memset(_index, 0xff, sizeof(_index));
	if (!_icons)
		return;
	Graphics::Surface *target = surface.surfacePtr();
	const uint count = _world.locationCount();
	for (uint i = 0; i < count; ++i) {
		const Location l = _world.location(i);
		if (l.hidden())
			continue;
		// _sub_15E4F_calc_SAL_index: sietch 0, palace 1, village 2, fortress 3, Harkonnen palace 4.
		const uint kind = l.type < 0x20 ? 0 : l.type < 0x21 ? 1 : l.type < 0x28 ? 2 : l.type < 0x30 ? 3 : 4;
		int x, y;
		if (!placeOnScreen(l, x, y))
			continue;
		if (_density && _mode == kFlat && l.isSietch() && l.spiceDensity) {
			// ONMAP 133-140: eight rings from a dot to 19 pixels, one per
			// 32 of density (the original's scale is not decoded).
			const uint16 ring = (uint16)(133 + MIN<uint>(7, l.spiceDensity / 32));
			_icons->drawFrame(ring, target, x - 9, y - 9);
		}
		// ONMAP 122-126: the sietch mound, the red Atreides palace, a village,
		// a fortress, the blue Harkonnen palace (58-70 are the troops' green
		// ornithopters). The far-sietch variant (127, +5) needs the distance
		// threshold the executable keeps at 0x1176, not decoded.
		const uint16 frame = (uint16)(122 + kind);
		uint32 offset;
		uint16 width, height;
		bool compressed;
		int8 paletteOffset;
		if (!_icons->getFrameInfo(frame, offset, width, height, compressed, paletteOffset))
			continue;
		const int left = x - width / 2, top = y - height / 2;
		_icons->drawFrame(frame, target, left, top);
		// Hit map: the icon's box, first come first served.
		for (int py = MAX(0, top); py < MIN(152, top + (int)height); ++py)
			for (int px = MAX(0, left); px < MIN(320, left + (int)width); ++px)
				if (_index[py * 320 + px] == 0xff)
					_index[py * 320 + px] = (byte)i;
	}
}

void MapScreen::setFlight(bool active, uint16 longitude, int16 latitude, int destination) {
	_flying = active;
	_flightLongitude = longitude;
	_flightLatitude = latitude;
	_flightDestination = destination;
	if (!active) {
		_trail.clear();
		return;
	}
	// The minimap recentres on the position (travel_minimap_setup).
	_longitude = longitude;
	_latitude = CLIP<int16>((int16)(latitude - 18), -75, 75);
}

void MapScreen::drawMinimap(Graphics::ManagedSurface &surface, const Common::Rect &box, const Panel &panel) {
	if (!_renderer && !loadFlat())
		return;
	Graphics::ManagedSurface full;
	full.create(320, 152, Graphics::PixelFormat::createFormatCLUT8());
	Graphics::Surface view = full.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
	const int16 latitude = CLIP<int16>((int16)(_flightLatitude - 18), -75, 75);
	_renderer->draw(view, latitude, _flightLongitude);
	int cx, cy;
	if (!_renderer->project(latitude, _flightLongitude, _flightLatitude, _flightLongitude, cx, cy)) {
		cx = 160;
		cy = 76;
	}
	const int w = box.width() - 4, h = box.height() - 4;
	const int sx = CLIP<int>(cx - w / 2, 4, 316 - w), sy = CLIP<int>(cy - h / 2, 4, 148 - h);
	surface.fillRect(box, 0xfc);
	surface.frameRect(Common::Rect(box.left + 1, box.top + 1, box.right - 1, box.bottom - 1), 0xfa);
	surface.blitFrom(full, Common::Rect(sx, sy, sx + w, sy + h), Common::Point(box.left + 2, box.top + 2));
	full.free();
	int x, y;
	for (uint i = 0; i < _trail.size(); ++i)
		if (_renderer->project(latitude, _flightLongitude, (int16)(_trail[i] & 0xffff), (uint16)(_trail[i] >> 16), x, y)) {
			x += box.left + 2 - sx;
			y += box.top + 2 - sy;
			if (x > box.left + 2 && x < box.right - 4 && y > box.top + 2 && y < box.bottom - 4)
				panel.drawIcon(surface, 0x2f, x - 1, y - 1);
		}
	panel.drawIcon(surface, 0x30, cx + box.left + 2 - sx - 1, cy + box.top + 2 - sy - 1);
}

void MapScreen::addFlightTrail(uint16 longitude, int16 latitude) {
	if (_trail.size() >= 23) // travel_trail_ring holds 23 positions
		_trail.remove_at(0);
	_trail.push_back(((uint32)longitude << 16) | (uint16)latitude);
}

void MapScreen::drawLocationPopup(Graphics::ManagedSurface &surface, const Panel &panel, const SentenceBank *sentences,
		uint index) {
	// map_place_location_panel / map_place_info_panel (seg000:5ee4, 5f25) and
	// map_draw_location_popup (600e): a 106-pixel panel beside the place's
	// marker, vertically centred on it within [4, 148 - height], on its right
	// unless that crosses x 210 (then 130 to the left). For a place seen from
	// afar (class 2, 30 rows) it holds the kind in yellow ("Sietch:",
	// "Palace:", "Village:", "Fort:", COMMAND 0x44-0x47) and the name in
	// white on black inside a green frame, as the recordings show.
	if (!sentences || !_renderer)
		return;
	const Location l = _world.location(index);
	int mx, my;
	if (!placeOnScreen(l, mx, my))
		return;
	const char *kinds[4] = { "Sietch: ", "Palace: ", "Village: ", "Fort: " };
	const uint kind = l.type < 0x20 ? 0 : l.type == 0x20 || l.type == 0x30 ? 1 : l.type < 0x28 ? 2 : 3;
	const uint16 kindId = panel.findCommand(kinds[kind]);
	const Common::String kindText = kindId != 0xffff ? panel.commandString(kindId) : Common::String(kinds[kind]);
	// The popup's colours, found in the palette on screen.
	byte pal[256 * 3];
	_system->getPaletteManager()->grabPalette(pal, 0, 256);
	auto nearest = [&](int r, int g, int b) -> byte {
		uint best = 0, bestD = 0xffffffff;
		for (uint i = 1; i < 256; ++i) {
			const int dr = pal[3 * i] - r, dg = pal[3 * i + 1] - g, db = pal[3 * i + 2] - b;
			const uint d = (uint)(dr * dr + dg * dg + db * db);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		}
		return (byte)best;
	};
	const byte green = nearest(40, 200, 60), yellow = nearest(250, 240, 90), white = nearest(250, 250, 250);
	const int height = 30;
	int x = mx + 15;
	if (x > 210)
		x = mx - 130;
	x = CLIP(x, 4, 316 - 106);
	const int y = CLIP(my - height / 2, 4, 148 - height);
	const Common::Rect box(x, y, x + 106, y + height);
	surface.fillRect(box, 0);
	surface.frameRect(box, green);
	panel.drawText(surface, kindText.c_str(), x + 10, y + 5, yellow, false);
	panel.drawText(surface, _world.locationName(index, *sentences).c_str(), x + 4, y + 16, white, false);
}

void MapScreen::drawOnmap(Graphics::ManagedSurface &surface, uint sprite, int x, int y) const {
	if (_icons)
		_icons->drawFrame((uint16)sprite, surface.surfacePtr(), x, y);
}

void MapScreen::applyPalette() const {
	if (_icons)
		_icons->setPalette();
}

uint MapScreen::onmapWidth(uint sprite) const {
	uint16 w = 0, h = 0;
	return _icons && _icons->frameSize((uint16)sprite, w, h) ? w : 0;
}

void MapScreen::drawVegetation(Graphics::ManagedSurface &surface, const Panel &panel) {
	// map_draw_vegetation_marks (seg000:633b): a tuft on every sprouting cell
	// ((cell & 0x30) == 0x10) in view, ONMAP 0x79 (the sheet open while the
	// map draws; the place markers follow at 0x7a) when the next cell east
	// sprouts too, else 0x78, jittered 0..3 pixels by the cell's offset
	// (seg000:63c7) so the tufts do not line up.
	if (!_renderer)
		return;
	const Common::Array<byte> &m = _world.map();
	for (int lat = _latitude - 2; lat <= _latitude + MapRenderer::kViewRows + 2; ++lat) {
		if (lat < -99 || lat > 99)
			continue;
		const int first = _world.mapCell(0, (int16)lat);
		if (first < 0)
			continue;
		const int cells = (int)_world.rowCells(lat);
		for (int c = 0; c < cells; ++c) {
			const uint o = (uint)(first + c);
			if (o >= m.size() || (m[o] & 0x30) != 0x10)
				continue;
			int x, y;
			const uint16 lng = (uint16)(((uint32)c << 16) / (uint32)cells);
			if (!_renderer->project(_latitude, _longitude, (int16)lat, lng, x, y))
				continue;
			const bool eastToo = o + 1 < m.size() && (m[o + 1] & 0x30) == 0x10;
			x += (int)(o & 3) - 2;
			y += (int)((o >> 2) & 3) - 2;
			if (x < 4 || x > 312 || y < 4 || y > 144)
				continue;
			(void)panel;
			drawOnmap(surface, eastToo ? 0x79 : 0x78, x - 4, y - 4);
		}
	}
}

void MapScreen::drawFlight(Graphics::ManagedSurface &surface, const Panel &panel) {
	if (!_flying || _mode != kFlat || !_renderer)
		return;
	int x, y;
	for (uint i = 0; i < _trail.size(); ++i)
		if (_renderer->project(_latitude, _longitude, (int16)(_trail[i] & 0xffff), (uint16)(_trail[i] >> 16), x, y))
			panel.drawIcon(surface, 0x2f, x - 1, y - 1);
	if (_flightDestination >= 0) {
		const Location l = _world.location((uint)_flightDestination);
		if (placeOnScreen(l, x, y))
			panel.drawIcon(surface, 0x2e, x - 1, y - 1);
	}
	if (_renderer->project(_latitude, _longitude, _flightLatitude, _flightLongitude, x, y))
		panel.drawIcon(surface, 0x30, x - 1, y - 1);
}

// The DUNE MAP title popup (map_show_rallied_troops_popup, seg000:5bb0): the
// panel record ds:194a, (10,10)-(190,64) filled with 0xfb inside a one-pixel
// 0xf5 frame (loc_07b1b, seg000:c551), then phrase 0xe2 in the small font
// with the pen at the record's corner + (10,8). The phrase centres itself
// with spaces. The live number of rallied troops (ds:28) is written over the
// phrase's first number as a three-character field ending where that number
// ends, blanks for leading zeros, capped at 999 (seg000:d03c, e2e3). The
// floppy numbers its phrases differently, so the phrase is found by its text.
void MapScreen::drawInfoBox(Graphics::ManagedSurface &surface, const Panel &panel, const SentenceBank *sentences,
		uint rallied) {
	const uint16 id = panel.findCommand("DUNE  MAP", true);
	if (_density) {
		// COMMAND "  SPICE DENSITY  " heads the box instead.
		const uint16 title = panel.findCommand("SPICE DENSITY", true);
		const Common::Rect box(6, 6, 216, 22);
		surface.fillRect(box, 250);
		surface.frameRect(box, 243);
		if (title != 0xffff)
			panel.drawText(surface, panel.commandString(title).c_str(), 12, 10, 243, false);
		return;
	}
	if (!sentences || id == 0xffff)
		return;
	// text() keeps the carriage returns that command() strips.
	Common::String text = sentences->text((uint16)(id + 1), false, _world.state());
	patchRalliedCount(text, rallied);
	const Common::Rect box(10, 10, 190, 64);
	surface.fillRect(box, 0xfb);
	surface.frameRect(box, 0xf5);
	Common::Array<Common::String> lines;
	Common::String line;
	for (uint i = 0; i < text.size(); ++i) {
		if (text[i] == '\r') {
			lines.push_back(line);
			line.clear();
		} else {
			line += text[i];
		}
	}
	lines.push_back(line);
	for (uint i = 0; i < lines.size() && i < 4; ++i)
		panel.drawText(surface, lines[i].c_str(), box.left + 10, box.top + 8 + (int)i * 10, 243, false); // the pen colour is inherited (not set by seg000:5bb0); the recording's text is this dark blue
}

void MapScreen::patchRalliedCount(Common::String &text, uint rallied) {
	uint i = 0;
	while (i < text.size() && (text[i] < '0' || text[i] > '9'))
		++i;
	while (i < text.size() && text[i] >= '0' && text[i] <= '9')
		++i;
	if (i < 3 || i > text.size())
		return;
	const uint n = MIN<uint>(rallied, 999);
	const char digits[3] = { (char)('0' + n / 100), (char)('0' + n / 10 % 10), (char)('0' + n % 10) };
	const bool hundreds = n >= 100, tens = n >= 10;
	text.setChar(hundreds ? digits[0] : ' ', i - 3);
	text.setChar(tens ? digits[1] : ' ', i - 2);
	text.setChar(digits[2], i - 1);
}

void MapScreen::draw(Graphics::ManagedSurface &surface, const Panel &panel, const SentenceBank *sentences, uint rallied) {
	Graphics::Surface *target = surface.surfacePtr();
	surface.fillRect(Common::Rect(0, 0, 320, 152), 0);
	if (_icons)
		_icons->setPalette(); // ONMAP carries the map screen's palette, the planet's colours 20-31 included
	if (_mode == kFlat) {
		if (_renderer) {
			Graphics::Surface view = target->getSubArea(Common::Rect(0, 0, 320, 152));
			_renderer->draw(view, _latitude, _longitude);
		}
		// The frame around the map (dune-rust draw_background): four
		// nested outlines in the panel's light colours.
		byte colour = 0xfc;
		for (int i = 0; i < 4; ++i, colour -= 2) {
			surface.hLine(i, i, 319 - i, colour);
			surface.hLine(i, 151 - i, 319 - i, colour);
			surface.vLine(i, i, 151 - i, colour);
			surface.vLine(319 - i, i, 151 - i, colour);
		}
	} else {
		drawDuneGlobe(_system, *target, _resources, _rotation, _tilt);
	}
	if (_mode == kFlat)
		drawVegetation(surface, panel);
	drawIcons(surface);
	drawFlight(surface, panel);
	if (_mode == kFlat && _destination == -2 && _renderer) {
		int x, y;
		if (_renderer->project(_latitude, _longitude, _pointLatitude, _pointLongitude, x, y))
			panel.drawIcon(surface, 0x2e, x - 1, y - 1); // the destination mark (seg000:49a0)
	}
	if (_mode == kFlat && _destination >= 0 && !_flying)
		drawLocationPopup(surface, panel, sentences, (uint)_destination);
	if (_mode == kFlat && (_caption || _density))
		drawInfoBox(surface, panel, sentences, rallied);
	else if (_results && _fresk) {
		// SEE RESULTS (seg000:b96b): the house panels slide open, uncovering
		// the stats overlay the scene draws on the black behind them.
		target->fillRect(Common::Rect(0, 0, 106, 152), 0);
		target->fillRect(Common::Rect(214, 0, 320, 152), 0);
		_fresk->drawFrame(0, target, -(int)_results, 0);
		_fresk->drawFrame(1, target, 214 + (int)_results, 0);
	}
}

void MapScreen::drawPanelExtras(Graphics::ManagedSurface &surface, const Panel &panel) const {
	if (_mode == kFlat) {
		// ui_set_and_draw_frieze_sides_map (seg000:d792) folds the map
		// variant ds:1c66 into the frieze records at ds:1ae6: the left one,
		// (22,161)-(68,196), gets ICONES 0x0d, the planet. Its flag 0x40
		// runs gfx vtable function 9 first; the recording shows the eye
		// frame intact around the planet, so that is not a plain fill.
		// ICONES 6 is the eye frame around a transparent oval; the two small
		// buttons the panel draws (ICONES 64, 20x15) fill the book block's
		// holes (ICONES 0) and do not belong here: their rectangles are
		// blacked out and the frame repainted over them.
		surface.fillRect(Common::Rect(35, 182, 55, 197), 0);
		surface.fillRect(Common::Rect(58, 182, 78, 197), 0);
		panel.drawIcon(surface, 6, 0, Panel::kTop);
		panel.drawIcon(surface, 0x0d, 22, 161);
		panel.drawIcon(surface, 41, 266, 171);
		panel.drawIcon(surface, 37, 267, 162);
		panel.drawIcon(surface, 38, 285, 171);
		panel.drawIcon(surface, 39, 267, 184);
		panel.drawIcon(surface, 40, 254, 171);
		panel.drawIcon(surface, 53, 266, 171);
	} else {
		panel.drawIcon(surface, 13, 22, 161);
		panel.drawIcon(surface, 49, 38, 159);
		panel.drawIcon(surface, 50, 54, 168);
		panel.drawIcon(surface, 51, 38, 183);
		panel.drawIcon(surface, 52, 20, 168);
		panel.drawIcon(surface, 53, 36, 172);
	}
}

} // namespace Dune

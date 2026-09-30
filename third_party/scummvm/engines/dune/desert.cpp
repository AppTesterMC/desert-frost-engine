/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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

// The floppy's desert landscape and the walk out of a place into the desert
// (notes/desert-walk-spec.md, from the floppy DUNEPRG.EXE and checked against
// a DOSBox-X recording of the original): rows of DUNES3 dunes and rocks
// placed by a seeded generator and drawn in perspective, the place's building
// from DUNES2 on the horizon, each arrow a small step, the sun's glare and
// the faint after too long a walk.

#include "dune/scene.h"

#include "common/config-manager.h"
#include "common/endian.h"
#include "common/system.h"
#include "graphics/paletteman.h"

#include "dune/amiga.h"
#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/hnm.h"
#include "dune/resource.h"
#include "dune/sky.h"
#include "dune/sprite.h"
#include "dune/world.h"

namespace Dune {

namespace {

enum {
	kHorizon = 77,        ///< ds:20E7, the landscape's baseline
	kDesertHeight = 20,   ///< ds:20ED in the desert
	kGroundColour = 0xbf, ///< the sand below the horizon (floppy 3AF8)
	kRows = 30
};

// ds:20FD: the eight sets of x positions a row's pieces take.
const int16 kXSets[8][4] = {
	{ -900, -200, 200, 900 }, { -1300, -400, 0, 1300 }, { -800, -100, 400, 800 }, { -600, -300, 100, 600 },
	{ -1200, -500, 300, 700 }, { -1000, -50, 500, 1000 }, { -700, -150, 50, 800 }, { -1100, -350, 350, 600 }
};

/** A frame of DUNES2/DUNES3 (6-byte header: width, height | palette base << 8, anchor). */
struct LandFrame {
	int w = 0, h = 0, anchor = 0;
	Common::Array<byte> pixels; ///< w x h, 0 transparent
};

bool decodeLandFrame(const Common::Array<byte> &data, uint frame, LandFrame &out) {
	if (data.size() < 4)
		return false;
	const uint table = READ_LE_UINT16(data.data());
	if (table + 2 > data.size())
		return false;
	const uint count = READ_LE_UINT16(data.data() + table) / 2;
	if (frame >= count)
		return false;
	uint p = table + READ_LE_UINT16(data.data() + table + 2 * frame);
	if (p + 6 > data.size())
		return false;
	const uint16 w0 = READ_LE_UINT16(data.data() + p);
	const bool packed = (w0 & 0x8000) != 0;
	out.w = w0 & 0x1ff;
	out.h = data[p + 2];
	const byte base = data[p + 3];
	out.anchor = READ_LE_UINT16(data.data() + p + 4);
	p += 6;
	if (!out.w || !out.h || out.w > 320 || out.h > 200)
		return false;
	out.pixels.resize(out.w * out.h);
	for (uint i = 0; i < out.pixels.size(); ++i)
		out.pixels[i] = 0;
	const int rowWidth = (out.w + 3) & ~3;
	for (int y = 0; y < out.h; ++y) {
		int x = 0;
		while (x < rowWidth) {
			if (p >= data.size())
				return true;
			int runs = 1;
			bool fill = false;
			byte value = 0;
			if (packed) {
				const int8 r = (int8)data[p++];
				fill = r < 0;
				runs = (fill ? -r : r) + 1;
				if (fill) {
					if (p >= data.size())
						return true;
					value = data[p++];
				}
			}
			for (int k = 0; k < runs; ++k) {
				if (!fill) {
					if (p >= data.size())
						return true;
					value = data[p++];
				}
				const byte nibbles[2] = { (byte)(value & 15), (byte)(value >> 4) };
				for (int n = 0; n < 2; ++n, ++x)
					if (x < out.w && nibbles[n])
						out.pixels[y * out.w + x] = (byte)(nibbles[n] + base);
			}
		}
	}
	return true;
}

/** T[z] (floppy 5A61): about 256 / z. */
uint depthScale(uint z) {
	const uint q = 75 / (75 * z), r = 75 % (75 * z);
	return (q << 8) | ((65536u * r / (75 * z)) >> 8);
}

} // namespace

uint16 GameScreen::landRandom() {
	_landSeed = (uint16)(_landSeed * 0xe56d + 1);
	return _landSeed;
}

uint GameScreen::landPick(byte terrain) {
	// The pickers (floppy 5C20): by the terrain's stage bits and height.
	const uint s = terrain & 0x30, t = terrain & 0x0f;
	uint h = landRandom() >> 8;
	if (s != 0x10) {
		if (t <= 8)
			return h & 7;
		if (t <= 10) {
			uint v = h & 15;
			while (v > 11) {
				h = landRandom() >> 8;
				v = h & 15;
			}
			return v;
		}
		return (h & 3) + 8;
	}
	if (t <= 8)
		return (h & 0x80) ? (h & 7) : (h & 3) + 12;
	if (t <= 10)
		return h & 15;
	return (h & 3) + 8 + ((_landSeed & 0x8000) ? 0 : 4);
}

void GameScreen::drawLandscape(Graphics::Surface &view, uint16 longitude, int16 latitude, byte fine, uint16 key,
		bool inPlace) {
	// build_landscape (floppy 57FA, desert branch 586B) then project_and_draw (5A8D).
	Common::Array<byte> dunes3, dunes2;
	if (!_resources.load("DUNES3.HSQ", dunes3) || !_resources.load("DUNES2.HSQ", dunes2))
		return;
	Sprite(_system, dunes3).setPalette(); // 80-94 and 209-223 (the sky's tail stays)
	struct Entry {
		uint z;
		int x;
		uint sprite;
	};
	Common::Array<Entry> entries;
	auto emit = [&](uint z, int x, uint sprite) { entries.push_back(Entry{ z, x, sprite }); };

	const int cell = _world.mapCell(longitude, latitude);
	const Common::Array<byte> &m = _world.map();
	const byte terrain = cell >= 0 && (uint)cell < m.size() ? m[cell] : 0;
	bool rock = false;
	for (int k = -1; k <= 4 && cell >= 0; ++k)
		if (cell + k >= 0 && (uint)(cell + k) < m.size() && (m[cell + k] & 0x30) == 0x10)
			rock = true;
	uint building = 0;
	int buildingX = 0, orniCount = 0;
	if (!inPlace && (terrain & 0x40)) {
		for (uint i = 0; i < _world.locationCount(); ++i) {
			const Location l = _world.location(i);
			if ((int)l.mapOffset != cell)
				continue;
			const int d = (int16)(l.longitude - longitude);
			if (d >= -4 && d <= 3) {
				buildingX = 512 * d;
				static const uint kBuildings[5] = { 17, 16, 18, 19, 19 };
				const uint kind = l.type < 0x20 ? 0 : l.type == 0x20 ? 1 : l.type < 0x28 ? 2 : l.type < 0x30 ? 3 : 4;
				building = kBuildings[kind];
				if (building >= 0x13)
					building = MIN<uint>(0x17, building + ((l.type - 0x28) & 0xfb));
				orniCount = l.ornithopters;
			}
			break;
		}
	}
	uint bl = (byte)latitude, bh = inPlace ? 0 : fine;
	uint pending = 0;
	for (uint z = 1; z < kRows;) {
		_landSeed = (uint16)((((bh << 8) | bl) ^ key) + 0x301);
		// emit_row (floppy 5982)
		landRandom();
		const int16 *xs = kXSets[((_landSeed >> 8) & 0x38) >> 3];
		if (pending) {
			emit(z, buildingX, pending);
			pending = 0;
		} else {
			for (uint k = 0; k < 4; ++k) {
				emit(z, xs[k], landPick(terrain));
				if (rock)
					emit(z, -xs[k], ((_landSeed >> 8) & 3) + 0x0c);
			}
		}
		++z;
		bh = (bh - 1) & 0xff;
		if (bh == 0 && building) {
			pending = building;
			if (building != 0x10 && building != 0x12)
				for (int k = 0; k < orniCount; ++k)
					emit(z, buildingX + 0x80 + 32 * (orniCount - 1 - k), 0x18);
			building = 0;
		}
	}
	// project_and_draw: the far rows first; sprites 0x10-0x17 from DUNES2.
	for (int i = (int)entries.size() - 1; i >= 0; --i) {
		const Entry &e = entries[i];
		const bool fromDunes2 = e.sprite >= 0x10 && e.sprite <= 0x17;
		drawLandObject(view, fromDunes2 ? dunes2 : dunes3, e.sprite, e.z, e.x, kDesertHeight);
	}
}

void GameScreen::drawLandObject(Graphics::Surface &view, const Common::Array<byte> &sheet, uint sprite, uint z, int x,
		uint height) {
	// project_and_draw (floppy 5A8D) and the scaled blit (5B00): the
	// baseline 77 + H T[z] / 256, the left edge 160 + x T[z] / 256, the
	// step s = 65536 / T[z] (one step less for buildings, ornis and
	// harvesters), the frame's anchor on the baseline; clipped to the view.
	const uint t = depthScale(z);
	if (!t)
		return;
	const int yb = kHorizon + (int)(256u * height * t / 65536);
	const int xl = 160 + (x * (int)t) / 256;
	uint s = 65536 / t;
	if (sprite >= 0x10 && s > 256)
		s -= 256;
	LandFrame f;
	if (!decodeLandFrame(sheet, sprite, f))
		return;
	const int width = 256 * ((f.w + 3) & 0x1fc) / (int)s, frameHeight = 256 * f.h / (int)s;
	const int top = MAX(0, yb - 256 * f.anchor / (int)s);
	for (int y = 0; y < frameHeight; ++y) {
		const int sy = top + y, srcY = y * (int)s / 256;
		if (sy < 0 || sy >= 152 || sy >= view.h || srcY >= f.h)
			continue;
		for (int dx = 0; dx < width; ++dx) {
			const int sx = xl + dx, srcX = dx * (int)s / 256;
			if (sx < 0 || sx >= 320 || srcX >= f.w)
				continue;
			const byte v = f.pixels[srcY * f.w + srcX];
			if (v)
				*(byte *)view.getBasePtr(sx, sy) = v;
		}
	}
}

// ---- The flight's landscape (floppy 57FA's flight branch, 54ED; notes/orni-flight-spec.md 6) ----

namespace {
enum {
	kFlightHeight = 0x48, ///< ds:20ED in an ornithopter
	kFlightFar = 40,      ///< new rows come in at z 40
	kFlightWrap = 1300    ///< x wraps at +-1300 when the view pans
};
} // namespace

void GameScreen::flightLandscapeReseed(uint16 longitude, int16 latitude) {
	// 5BF2 / 5963: the seed and the sprite picker from the map 5 steps ahead.
	_landSeed = (uint16)(longitude ^ (uint16)latitude);
	const int cell = _world.mapCell(longitude, latitude);
	const Common::Array<byte> &m = _world.map();
	_flightTerrain = cell >= 0 && (uint)cell < m.size() ? m[cell] : 0;
	if (dumpEveryMillis()) // test runs: the seeds, to compare with the original's ds:20E3
		_log.line(Common::String::format("Land: seed %04x from %04x/%d, map byte %02x", _landSeed, longitude, latitude,
				_flightTerrain));
}

void GameScreen::flightLandscapeRow(uint z) {
	// emit_row (floppy 5982) without the mirrored rocks: 4 objects.
	landRandom();
	const int16 *xs = kXSets[((_landSeed >> 8) & 0x38) >> 3];
	for (uint k = 0; k < 4; ++k) {
		LandObject o;
		o.z = (int16)z;
		o.x = xs[k];
		o.sprite = (uint16)landPick(_flightTerrain);
		_flightObjects.push_back(o);
	}
}

void GameScreen::flightLandscapeStart(const uint16 *longitudes, const int16 *latitudes) {
	// The initial fill (57FA, [20ED] != 0x14): five groups of 8 rows, z 1-40,
	// each seeded from the route one step further on.
	_flightObjects.clear();
	for (uint g = 0; g < 5; ++g) {
		flightLandscapeReseed(longitudes[g], latitudes[g]);
		for (uint z = 1 + 8 * g; z < 9 + 8 * g; ++z)
			flightLandscapeRow(z);
	}
	_flightFrameAt = _system->getMillis();
}

void GameScreen::flightLandscapeTick() {
	// One frame of 54ED: every object one step nearer (those at z 0 are
	// gone), and a new row at z 40.
	for (uint i = 0; i < _flightObjects.size();) {
		if (--_flightObjects[i].z <= 0)
			_flightObjects.remove_at(i);
		else
			++i;
	}
	flightLandscapeRow(kFlightFar);
}

void GameScreen::flightLandscapePan(int direction) {
	// 5A36 (left, -1) adds 4z to every object's x, 5A0B (right, +1) takes it off.
	for (uint i = 0; i < _flightObjects.size(); ++i) {
		LandObject &o = _flightObjects[i];
		int x = o.x - direction * 4 * o.z;
		if (direction < 0 && x > kFlightWrap)
			x = -kFlightWrap;
		else if (direction > 0 && x <= -kFlightWrap)
			x = kFlightWrap;
		o.x = (int16)x;
	}
}

void GameScreen::flightLandscapeDraw(Graphics::Surface &view, const Common::Array<byte> &dunes) {
	// The background (3AF8: the sand from y 77), then every object, the far ones first.
	view.fillRect(Common::Rect(0, kHorizon, 320, MIN<int>(152, view.h)), kGroundColour);
	for (int i = (int)_flightObjects.size() - 1; i >= 0; --i) {
		const LandObject &o = _flightObjects[i];
		drawLandObject(view, dunes, o.sprite, (uint)o.z, o.x, kFlightHeight);
	}
}

void GameScreen::drawWalkView() {
	// draw_room_scene's desert case (floppy 3AA7): sky, sand from y 77, the landscape.
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	_panel.applyPalette();
	Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
	const bool tall = _walkLng == 0x2001 || _walkLng == 0x3001; // the original's quirk (3.1)
	if (_world.amiga()) {
		amigaDesertView(_system, _resources, view, _state.w(GameState::kGameTime));
	} else {
		drawSky(_system, _resources, view, tall ? kSkyLarge : kSkyNarrow, 320, skyPalette(), true);
		setSkyPalette(false); // the hour's light, or the running blend (floppy 3b13)
		_surface.fillRect(Common::Rect(0, kHorizon, 320, 152), kGroundColour);
		drawLandscape(view, _walkLng, _walkLat, _walkFine, _walkLng, false);
	}
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);
	_viewOk = true;
}

void GameScreen::walkOut(byte exit) {
	// ui_click_move_room's walk out (floppy 422C): 0xFF up, 0xFE right, 0xFD
	// down, 0xFC left, 0xFB none; the place is left behind at fine 0.
	const uint from = _world.currentLocation();
	const Location l = _world.location(from);
	_walkFrom = (int)from;
	_walkLng = l.longitude;
	_walkLat = l.latitude;
	_walkFine = 0;
	_walkSteps = 0;
	_walking = true;
	_desert = true;
	_state.setB(0xe7, 0);
	_log.line(Common::String::format("Desert: Paul walks out of place %u", from));
	desertStep((byte)(-(int)exit) & 0xff, false);
}

void GameScreen::desertStep(uint direction, bool counted) {
	// desert_apply_step_delta (floppy B4CC) with desert_step_deltas
	// [_, 0xFF00, 0x0001, 0x0100, 0x00FF, 0]: a 256th of a row north or
	// south, one longitude unit east or west.
	if (counted) {
		_walkSteps = (byte)MIN<uint>(0x7f, _walkSteps + 1u);
		if (_state.b(0xf4) < 20)
			_state.setB(0xf4, (byte)(_state.b(0xf4) + 1));
	}
	switch (direction) {
	case 1: // north
		if (_walkFine == 0) {
			if (_walkLat - 1 > -0x62) {
				--_walkLat;
				_walkFine = 0xff;
			}
		} else {
			--_walkFine;
		}
		break;
	case 3: // south
		if (_walkFine == 0xff) {
			if (_walkLat + 1 < 0x62) {
				++_walkLat;
				_walkFine = 0;
			}
		} else {
			++_walkFine;
		}
		break;
	case 2:
		++_walkLng;
		break;
	case 4:
		--_walkLng;
		break;
	default:
		break;
	}
	if (_walkFine == 0) {
		// Back on a place's own cell and longitude: arrival, room 1 (floppy 4279).
		const int cell = _world.mapCell(_walkLng, _walkLat);
		const Common::Array<byte> &m = _world.map();
		if (cell >= 0 && (uint)cell < m.size() && (m[cell] & 0x40))
			for (uint i = 0; i < _world.locationCount(); ++i) {
				const Location l = _world.location(i);
				if ((int)l.mapOffset == cell && l.longitude == _walkLng) {
					_walking = false;
					_desert = false;
					_log.line(Common::String::format("Desert: Paul walks back into place %u", i));
					_world.setPosition(i, 1);
					showRoom(1);
					return;
				}
			}
	}
	_mode = kRoom;
	drawRoom();
	if (counted)
		thirstCheck();
}

void GameScreen::thirstCheck() {
	// The auto-action armed by the step counter (floppy 39C2): the sun's
	// glare at steps 20, 36, 52; at 68 Paul faints and is found five periods
	// later in his place (palace room 10, a sietch's room 2).
	const int k = (int)_walkSteps - 20;
	if (k < 0 || k % 16)
		return;
	Common::Array<byte> sun;
	if (_resources.load("SUN.HSQ", sun)) {
		Sprite glare(_system, sun);
		glare.setPalette();
		glare.drawFrame(0, _surface.surfacePtr(), 0, 0);
		_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 152);
		_system->updateScreen();
		dumpScreen(_system, "desert-glare");
		if (!isRecording() && !isDumpRun() && !isDuneFastHarness())
			_system->delayMillis(300u * (uint)MIN(10, k / 16 + 1));
	}
	_log.line(Common::String::format("Desert: the sun's glare (step %u)", _walkSteps));
	if (_walkSteps < 0x37) {
		drawRoom();
		return;
	}
	if (!_world.floppy() && !_world.amiga())
		desertCollapse(); // CD 3757 -> 0e77
	const uint home = _walkFrom >= 0 ? (uint)_walkFrom : _world.currentLocation();
	const byte type = _world.location(home).type;
	const uint room = type < 0x21 ? (type == Location::kPalace ? 10 : 2) : 1;
	_log.line(Common::String::format("Desert: Paul faints and is found in place %u, room %u", home, room));
	_walking = false;
	_desert = false;
	_world.setPosition(home, room);
	_room = room;
	refreshRooms();
	_mode = kTalk; // no redraw of the room while the five periods pass
	passTime(5);
	if (_mode != kTalk || _ending)
		return; // an ending came first
	_mode = kRoom;
	_state.setB(0xe7, (byte)(_state.b(0xe7) + 1));
	showRoom(room);
}

void GameScreen::desertCollapse() {
	// desert_collapse_cutscene (CD 0e77-0ea3; the floppy has none): the
	// WORMSUIT score (not built here), DEAD3.HNM's first frame into the game
	// area, then its five other frames, each revealed by transition 0x3c
	// (segvga 2a10: the slow dissolve, a 15-bit LFSR walking the game area,
	// 80 pixels a tick), the head redrawn over them; then Paul's head goes
	// down (181e).
	Common::Array<byte> video;
	if (!_resources.load("DEAD3.HNM", video))
		return;
	HnmPlayer player(_system);
	if (!player.begin(video) || !player.step())
		return;
	_log.line("Desert: Paul collapses (DEAD3.HNM)");
	// The video's palette chunks set only their own ranges (the panel keeps
	// its colours): the entries the video leaves black stay as they are.
	byte keep[256 * 3];
	_system->getPaletteManager()->grabPalette(keep, 0, 256);
	auto present = [&]() {
		byte pal[256 * 3];
		const byte *video = player.palette();
		for (uint i = 0; i < 256; ++i) {
			const bool set = video[3 * i] || video[3 * i + 1] || video[3 * i + 2];
			for (uint k = 0; k < 3; ++k)
				pal[3 * i + k] = set ? video[3 * i + k] : keep[3 * i + k];
		}
		_system->getPaletteManager()->setPalette(pal, 0, 256);
		_panel.applyPalette();
		const bool exits[4] = { false, false, false, false };
		_panel.draw(_surface, exits, -1, -1, day()); // the panel stays under the game area
		const byte black[3] = { 0, 0, 0 };
		_system->getPaletteManager()->setPalette(black, 0, 1);
		_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
		_system->updateScreen();
	};
	for (int y = 0; y < 152; ++y)
		memcpy(_surface.getBasePtr(0, y), player.screen() + 320 * y, 320);
	present();
	dumpScreen(_system, "desert-collapse-0");
	const bool fast = isDumpRun() || isDuneFastHarness() || isRecording() ||
			(ConfMan.hasKey("dune_speedrun") && !ConfMan.hasKey("dune_speedrun_watch"));
	for (uint frame = 1; frame <= 5 && !_quitRequested; ++frame) {
		if (!player.step())
			break;
		const byte *next = player.screen();
		if (fast) {
			for (int y = 0; y < 152; ++y)
				memcpy(_surface.getBasePtr(0, y), next + 320 * y, 320);
		} else {
			// The dissolve: the LFSR offset and that offset + 0x7fff each
			// step, 80 steps a tick (5 ms), offset 0 at the end.
			uint16 lfsr = 1;
			uint batch = 0;
			byte *dst = (byte *)_surface.getBasePtr(0, 0);
			do {
				dst[lfsr] = next[lfsr];
				if ((uint)lfsr + 0x7fff < 320 * 152)
					dst[lfsr + 0x7fff] = next[lfsr + 0x7fff];
				const bool carry = lfsr & 1;
				lfsr >>= 1;
				if (carry)
					lfsr ^= 0x4400;
				if (++batch == 0x50) {
					batch = 0;
					present();
					_system->delayMillis(5);
					Common::Event event;
					while (pollDuneEvent(_system, event))
						if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
							_quitRequested = true;
				}
			} while (lfsr != 1 && !_quitRequested);
			dst[0] = next[0];
		}
		present();
		dumpScreen(_system, Common::String::format("desert-collapse-%u", frame).c_str());
	}
	headDown("desert collapse"); // 0ea3 -> 181e
}

void GameScreen::dumpDesertWalk() {
	// Dump runs: out of the palace's front on foot, as in the DOSBox-X
	// recording (capture/duneprg_000.avi): six steps south, one east, back
	// north into the palace; then out again until the sun and the faint.
	travelTo(0);
	showRoom(1);
	const RoomRecord *record = currentRoom();
	int out = -1;
	for (uint d = 0; record && d < 4; ++d)
		if (record->exits[d] >= World::kExitWalkOut)
			out = (int)d;
	if (out < 0) {
		_log.line("Desert: no walk-out exit in the palace's first room");
		return;
	}
	walkOut(record->exits[out]);
	dumpScreen(_system, "walk-0");
	for (uint i = 1; i <= 6 && _walking; ++i) {
		desertStep(3, true);
		dumpScreen(_system, Common::String::format("walk-s%u", i).c_str());
	}
	desertStep(2, true);
	dumpScreen(_system, "walk-e1");
	desertStep(4, true);
	while (_walking && _walkFine)
		desertStep(1, true);
	_log.line(Common::String::format("Desert: back in place %u room %u", _world.currentLocation(), _room));
	dumpScreen(_system, "walk-back");
	walkOut(record->exits[out]);
	for (uint i = 0; i < 80 && _walking; ++i)
		desertStep((i & 1) ? 2 : 4, true);
	dumpScreen(_system, "walk-found");
}

} // namespace Dune

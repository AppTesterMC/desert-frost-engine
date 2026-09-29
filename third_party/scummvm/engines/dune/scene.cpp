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

#include "common/config-manager.h"
#include "common/file.h"
#include "common/events.h"
#include "common/random.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/paletteman.h"

#include "dune/amiga.h"
#include "dune/book.h"
#include "dune/debug.h"
#include "dune/dialogue.h"
#include "dune/harness.h"
#include "dune/hnm.h"
#include "dune/music.h"
#include "dune/palace.h"
#include "dune/resource.h"
#include "dune/room.h"
#include "dune/saves.h"
#include "dune/scene.h"
#include "dune/sky.h"
#include "dune/sprite.h"
#include "dune/text.h"
#include "dune/world.h"

namespace Dune {

namespace {

// Dump and harness runs capture pictures as fast as possible: no real-time
// clock, no flight or panel animations (as intro_scenes.cpp).
bool isFastCapture() {
	// The speedrun check runs as fast as a capture, unless it is watched
	// (dune_speedrun_watch: real-time flights and animations, pauses).
	// dune_real_time keeps a harness run (checkpoints, scripted input) at
	// the original's speed: the fidelity report's timed CD flight.
	return (isDumpRun() && !dumpEveryMillis()) || isDuneFastHarness() ||
		   (ConfMan.hasKey("dune_speedrun") && !ConfMan.hasKey("dune_speedrun_watch"));
}

// The capture runs also skip some rules (deaths, the ending, desert
// landings) so their pictures stay put; the speedrun check keeps them all.
bool g_rulesForced = false; ///< a story setup that checks a death or the ending (GameScreen::forceRules)

bool skipsRules() {
	return isFastCapture() && !ConfMan.hasKey("dune_speedrun") && !g_rulesForced;
}

} // namespace

void GameScreen::forceRules(bool on) {
	g_rulesForced = on;
}

namespace {

// GLOBDATA is the original globe outline stream plus two 64x100 lookup
// tables, not a flat bitmap. This is the recovered vga_globe_setup path from
// dune-re, kept here so SEE DUNE MAP renders the actual resource data.
enum GlobeSection { kFarNorth, kNearNorth, kNearSouth, kFarSouth };
struct GlobeSectionLatitude { GlobeSection section; byte latitude; };

} // namespace

byte globeCellColour(byte cell, bool results) {
	// The VGA driver's two pixel paths, chosen by the byte pair vga_globe_init
	// patches from the results flag (floppy DUNEVGA 1D24, CD DNVGA 1E4A).
	const byte terrain = cell & 0x0f, stage = cell & 0x30;
	if (results) {
		// SEE RESULTS (DUNEVGA 1DA3, DNVGA 1EC9): neutral 0x10 + t, Atreides
		// (sprouting 0x10 or land 0x20) 0x20 + t, Harkonnen 0x30 + t; FRESK's
		// 0x24-0x2f are the reds, 0x34-0x3f the blues.
		if (!stage)
			return 0x10 + terrain;
		return stage == 0x30 ? 0x30 + terrain : 0x20 + terrain;
	}
	// STANDARD VISION (DUNEVGA 1D26, DNVGA 1E4C): sprouting sand (stage 0x10,
	// terrain under 8) is drawn 12 higher, on FRESK's greens at 0x20-0x23.
	if (stage == 0x10 && terrain < 8)
		return 0x10 + terrain + 12;
	return 0x10 + terrain;
}

namespace {

bool drawRecoveredGlobe(Graphics::Surface &surface, const Common::Array<byte> &globdata,
		const Common::Array<byte> &map, const Common::Array<byte> &tablat, uint16 rotation, int tilt, bool results = false) {
	// A faithful transcription of madmoose's GlobeRenderer (dune-rust
	// globe_renderer.rs), itself from the original's globe routine.
	// GLOBDATA: an outline stream (per row: ~length, then one latitude byte
	// per column) followed at 3290 by two 64x100 tables, one row per column:
	// map row selector (x2) and a signed cell offset. TABLAT.BIN: 99 rows of
	// big-endian map row start and length. MAP.HSQ holds the pixel rows from
	// 0x62FC, wrapping around each row.
	static const uint kMapStart = 0x62fc;
	static const uint kTableStart = 3290;
	static const uint kRows = 99;
	static const uint kTiltEntries = 4 * kRows - 4;
	if (globdata.size() < kTableStart + 64 * 200 || map.size() <= kMapStart || tablat.size() < kRows * 8)
		return false;

	struct RotationEntry { int rowStart; int rowLength; uint32 fixed; };
	RotationEntry rotationTable[kRows];
	for (uint i = 0; i < kRows; ++i) {
		rotationTable[i].rowStart = (int16)((tablat[i * 8] << 8) | tablat[i * 8 + 1]);
		rotationTable[i].rowLength = (tablat[i * 8 + 2] << 8) | tablat[i * 8 + 3];
		rotationTable[i].fixed = 0;
	}
	uint32 dxax = 398u * rotation;
	dxax &= ~0xffffu;
	rotationTable[0].fixed = dxax;
	dxax += 0x8000;
	const uint32 bx0 = dxax / 398;
	for (uint i = 1; i < kRows; ++i)
		rotationTable[i].fixed = 2 * bx0 * (uint32)rotationTable[i].rowLength;

	GlobeSectionLatitude tiltTable[kTiltEntries];
	uint tiltCount = 0;
	for (int i = 1; i <= 98; ++i) tiltTable[tiltCount++] = { kFarSouth, (byte)i };
	for (int i = 98; i >= 0; --i) tiltTable[tiltCount++] = { kNearSouth, (byte)i };
	for (int i = 1; i <= 98; ++i) tiltTable[tiltCount++] = { kNearNorth, (byte)i };
	for (int i = 98; i >= 2; --i) tiltTable[tiltCount++] = { kFarNorth, (byte)i };
	if (tiltCount != kTiltEntries)
		return false;
	// globe_increment_tilt (CD seg000:ba15) keeps the tilt within +-98.
	tilt = CLIP(tilt, -98, 98);

	auto mapColour = [&](int offset) -> byte {
		const int address = (int)kMapStart + offset;
		if (address < 0 || address >= (int)map.size())
			return 0;
		const byte cell = map[address];
		if (amigaRelease())
			return terrainColour(cell & 0x0f); // the Amiga's ONMAP ramp (map.cpp); no ownership ramps
		return globeCellColour(cell, results);
	};

	const int centerX = 159, centerY = 79;
	for (uint half = 0; half < 2; ++half) {
		uint position = 0;
		int y = 0;
		while (position < globdata.size()) {
			const int8 marker = (int8)globdata[position++];
			if (marker >= 0)
				return false;
			const int lineLength = (byte)(~marker);
			if (!lineLength)
				break;
			for (int x = 0; x < lineLength; ++x) {
				if (position >= globdata.size())
					return false;
				const int n = half == 0 ? (int8)globdata[position++] : -(int)(int8)globdata[position++];
				const int lookup = n + 196 + tilt;
				if (lookup < 0 || lookup >= (int)kTiltEntries)
					continue;
				const GlobeSectionLatitude selected = tiltTable[lookup];
				const uint tableOffset = kTableStart + (uint)x * 200 + selected.latitude;
				const uint rowIndex = globdata[tableOffset] / 2;
				if (rowIndex >= kRows)
					continue;
				int ax = (int8)globdata[tableOffset + 100];
				int rowStart = rotationTable[rowIndex].rowStart;
				int cx = rotationTable[rowIndex].rowLength;
				int dx = (int)(int16)(rotationTable[rowIndex].fixed >> 16);
				switch (selected.section) {
				case kFarNorth: ax = cx - ax; rowStart = -rowStart; break;
				case kNearNorth: rowStart = -rowStart; break;
				case kNearSouth: break;
				case kFarSouth: ax = cx - ax; break;
				}
				cx *= 2;
				const int py = half == 0 ? centerY - y : centerY + y;
				int bp = dx - ax;
				if (bp < 0)
					bp += cx;
				bp += rowStart;
				dx += ax;
				if (centerX - x >= 0 && centerX - x < surface.w && py >= 0 && py < surface.h)
					*(byte *)surface.getBasePtr(centerX - x, py) = mapColour(bp);
				bp = dx - cx;
				if (bp < 0)
					bp += cx;
				bp += rowStart;
				if (centerX + x + 1 >= 0 && centerX + x + 1 < surface.w && py >= 0 && py < surface.h)
					*(byte *)surface.getBasePtr(centerX + x + 1, py) = mapColour(bp);
			}
			++y;
		}
	}
	return true;
}

// Floppy CS:B9F1-BB0A: invert the globe lookup for the single player marker.
bool projectGlobePlayer(const Common::Array<byte> &globdata,
		const Common::Array<byte> &tablat, uint16 rotationIndex, int tilt,
		uint16 longitude, int latitude, int &x, int &y) {
	const uint table = 3290;
	const uint latitudeRow = ABS(latitude);
	if (latitudeRow >= 99 || globdata.size() < table + 64 * 200 || tablat.size() < 99 * 8)
		return false;
	const uint half = (tablat[latitudeRow * 8 + 2] << 8) | tablat[latitudeRow * 8 + 3];
	const uint32 unit = (((uint32)rotationIndex << 16) + 0x8000) / 398;
	const uint rowRotation = latitudeRow == 0 ? rotationIndex : (2 * unit * half) >> 16;
	int delta = (int)(((uint32)(2 * half) * longitude + 0x8000) >> 16) - rowRotation;
	bool negative = delta < 0;
	delta = ABS(delta);
	if (delta >= (int)half) {
		delta = 2 * half - delta;
		negative = !negative;
	}
	const bool far = delta >= (int)(half / 2);
	if (far)
		delta = 2 * (half / 2) - delta;

	// BA55..BA7F: scan the same 64 column blocks, retaining the found
	// latitude position when moving to the next block.
	uint offset = table, remaining = 100, column = 0, previousRemaining = 100;
	bool selected = false;
	while (offset < table + 64 * 200) {
		bool found = false;
		while (remaining) {
			const byte v = globdata[offset++];
			--remaining;
			if (v == latitudeRow * 2) {
				found = true;
				break;
			}
		}
		if (!found) {
			remaining = previousRemaining;
			selected = true;
			break;
		}
		++remaining;
		if (delta <= globdata[offset + 99]) {
			selected = true;
			break;
		}
		previousRemaining = remaining;
		++column;
		offset += 199;
	}
	if (!selected)
		return false;
	uint16 lookup = (uint16)(100 - remaining);
	if (far)
		lookup = (byte)-lookup;
	if (latitude < 0)
		lookup |= 0xff00;

	// BA8F..BAAD: inverse search of DS:8702..8802, a128-word subset of
	// the196-word tilt table atDS:86BE. Formula matches B92C and all196
	// words of explore08/09 original DS snapshots.
	auto tiltEntry = [&](int index) -> uint16 {
		const int n = tilt + 98 - index;
		if (n > 98)
			return (byte)(n - 197);
		if (n >= 0)
			return (uint16)n;
		if (n >= -98)
			return 0xff00 | (uint16)-n;
		return 0xff00 | (byte)(-197 - n);
	};
	int tableIndex = -1;
	for (int i = 34; i < 162; ++i) {
		if (tiltEntry(i) == lookup) {
			tableIndex = i;
			break;
		}
	}
	if (tableIndex < 0)
		return false;
	const int vertical = tableIndex - 98;
	const bool south = vertical < 0;
	const uint target = ABS(vertical);

	// BAAF..BAE3: find the outline row whose column sample is closest,
	// accepting an unsigned error of at most2, as the original does.
	uint stream = 0, best = 255, bestRow = 0;
	for (uint row = 0; row < 54; ++row) {
		const uint width = (byte)~globdata[stream++];
		if (!width || width <= column)
			break;
		const uint difference = (byte)(globdata[stream + column] - target);
		stream += width;
		if (difference < best) {
			best = difference;
			bestRow = row;
			if (!difference)
				break;
		}
	}
	if (best > 2)
		return false;
	x = 160 + (negative ? -(int)column : (int)column);
	y = 79 + (south ? (int)bestRow : -(int)bestRow);
	return true;
}

} // namespace

namespace {

// dune_globe_dump=<dir> (scripts/check_globe_results.sh): every globe draw
// writes the sampler's palette indices before the panels and Paul's arrow
// (globe-NNN.idx, rows 0-151), the live map (globe-NNN.map) and the phase,
// the tilt and the mode (globe-NNN.txt), for scripts/globe_ref.py.
void dumpGlobeState(const Graphics::Surface &surface, const Common::Array<byte> &map, uint16 rotation, int tilt, bool results) {
	static uint counter = 0;
	const Common::Path dir(ConfMan.get("dune_globe_dump"));
	const Common::String name = Common::String::format("globe-%03u", counter++);
	Common::DumpFile idx;
	if (idx.open(dir.appendComponent(name + ".idx"), true)) {
		for (int y = 0; y < 152 && y < surface.h; ++y)
			idx.write(surface.getBasePtr(0, y), 320);
		idx.close();
	}
	Common::DumpFile mapFile;
	if (mapFile.open(dir.appendComponent(name + ".map"), true)) {
		mapFile.write(map.data(), map.size());
		mapFile.close();
	}
	Common::DumpFile info;
	if (info.open(dir.appendComponent(name + ".txt"), true)) {
		info.writeString(Common::String::format("phase %u tilt %d %s\n", (398u * rotation) >> 16, tilt,
				results ? "results" : "standard"));
		info.close();
	}
}

} // namespace

bool drawDuneGlobe(OSystem *system, Graphics::Surface &surface, Resource &resources,
		const Common::Array<byte> &mapData, uint16 rotation, int tilt, uint results, const Location &player,
		bool resultsColours) {
	Common::Array<byte> globeData, tablatData, freskData;
	if (!resources.load("GLOBDATA.HSQ", globeData)
			|| !resources.load("TABLAT.BIN", tablatData) || !resources.load("FRESK.HSQ", freskData))
		return false;
	Sprite fresk(system, freskData);
	byte planet[16 * 3];
	Common::Array<byte> onmapData;
	const bool amigaPlanet = amigaRelease() && resources.load("ONMAP.HSQ", onmapData);
	if (amigaPlanet) {
		// The Amiga's FRESK has its own colours at 16-31; the planet wears the
		// map's (ONMAP), as the recording's globe shows.
		Sprite(system, onmapData).setPalette();
		system->getPaletteManager()->grabPalette(planet, 16, 16);
	}
	if (!fresk.setPalette())
		return false;
	if (amigaPlanet) {
		system->getPaletteManager()->setPalette(planet, 16, 16);
		amigaMirrorUiColours(system);
	}
	// Floppy CS:B749 fills with F1, draws the ring and globe, then B77D
	// draws the sliding house panels. Never erase rectangles over the sphere.
	surface.fillRect(Common::Rect(0, 0, 320, 152), 0xf1);
	fresk.drawFrame(2, &surface, 91, 20);
	// The recovered sampler's tilt convention is opposite to DS:297C;
	// retain its existing convention for the prologue's separate caller.
	// The colours follow the results flag (floppy ds:FEF2), which flips after
	// the panels have slid open (CS:B86A) and before they slide shut (B860).
	const bool ok = drawRecoveredGlobe(surface, globeData, mapData, tablatData, rotation, -tilt, resultsColours);
	if (ok && ConfMan.hasKey("dune_globe_dump"))
		dumpGlobeState(surface, mapData, rotation, tilt, resultsColours);
	const int slide = (int)MIN<uint>(results, 100) * 112 / 100;
	fresk.drawFrame(0, &surface, -slide, 0);
	fresk.drawFrame(1, &surface, 214 + slide, 0);
	int playerX, playerY;
	Common::Array<byte> iconData;
	if (ok && projectGlobePlayer(globeData, tablatData, (uint16)((398u * rotation) >> 16), tilt,
			player.longitude, player.latitude, playerX, playerY) && resources.load("ICONES.HSQ", iconData)) {
		// Floppy CS:BB0B: the player's arrow, with its bottom at the projection.
		Sprite icons(system, iconData);
		icons.drawFrame(0x36, &surface, playerX, playerY - 16);
	}
	uint lit = 0;
	for (int y = 20; y < 140; ++y)
		for (int x = 100; x < 220; ++x)
			if (*(const byte *)surface.getBasePtr(x, y))
				++lit;
	debug(1, "Dune: globe %s, %u pixels in the ring", ok ? "drawn" : "FAILED", lit);
	return ok;
}

bool drawGlobeSphere(OSystem *system, Graphics::Surface &surface, Resource &resources, uint16 rotation, int tilt) {
	Common::Array<byte> mapData, globeData, tablatData;
	if (!resources.load("MAP.HSQ", mapData) || !resources.load("GLOBDATA.HSQ", globeData)
			|| !resources.load("TABLAT.BIN", tablatData))
		return false;
	(void)system;
	return drawRecoveredGlobe(surface, globeData, mapData, tablatData, rotation, tilt);
}

GameScreen::GameScreen(OSystem *system, Resource &resources, StartupLog &log) :
		_system(system), _resources(resources), _log(log), _panel(system, resources), _mode(kRoom),
		_room(kPalaceFirstRoom), _viewOk(false), _world(_state, resources, log), _sentences(nullptr),
		_dialogue(nullptr), _conditions(nullptr), _conversation(nullptr), _book(nullptr), _map(nullptr),
		_saves(nullptr), _menu(kMenuNone), _menuStatus(0xffff), _music(nullptr), _musicOn(false), _musicOrder(0),
		_quitRequested(false), _troopId(0), _troopFromMap(false), _troopChoosing(false), _recruiting(false), _troopFromRoom(false),
		_hireTroop(0),
		_clockStart(0), _talkWho(0), _talkIdle(0), _talkEnded(false), _talkRecruit(0), _talkRecruitOk(false), _talkSheet(nullptr), _talkLine(0), _talkPage(0), _talkAnimation(0), _talkFrame(0), _talkStart(0),
		_talkAnimating(false) {
	_surface.create(320, 200, Graphics::PixelFormat::createFormatCLUT8());
	memset(_backdropPalette, 0, sizeof(_backdropPalette));
	for (uint i = 0; i < Panel::kCommandRows; ++i) {
		_rowActions[i] = kRowNone;
		_rowArguments[i] = 0;
	}
}

GameScreen::~GameScreen() {
	delete _talkSheet;
	delete _saves;
	delete _map;
	delete _book;
	delete _conversation;
	delete _conditions;
	delete _dialogue;
	delete _sentences;
	_surface.free();
}

// ---- Text helpers -----------------------------------------------------------

void GameScreen::drawSceneText(const char *text, int y, uint32 colour) {
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (font)
		font->drawString(&_surface, text, 0, y, 320, colour, Graphics::kTextAlignCenter);
}

// ---- The world ----------------------------------------------------------------

void GameScreen::startNewGame() {
	_sceneActive = false;
	_pendingScene = 0;
	_cast.clear();
	_finalPicture = 0;
	_desert = false;
	_walking = false;
	_commList = -1;
	_troopEquipment = false;
	_state.newGame();
	_world.loadInitialData();
	_world.prepareNewGame();
	_world.applyCelimynTuekFix(); // dune_fix_celimyn_tuek (off = as the original)
	// The executable runs the phase triggers twice at startup (seg000:00b9, 00bc).
	runPhaseTriggers();
	runPhaseTriggers();
	// The always-true condition byte; the executable's data has it too.
	_state.setB(0xfc, 1);
	if (_dialogue)
		_dialogue->load(_resources, _log); // forget what was said
	_menu = kMenuNone;
	_clockStart = 0;
	_hireTroop = 0;
	_world.setPosition(_world.currentLocation(), kPalaceFirstRoom);
	showRoom(kPalaceFirstRoom);
}

uint GameScreen::day() const {
	// get_ingame_day_3_periods_later (seg000:1ad1) mod 365, plus one (seg000:1a6b).
	_panel.setTimeOfDay(_world.timeSlot());
	return (uint)((_state.w(GameState::kGameTime) + 3) >> 4) % 365 + 1;
}

uint GameScreen::skyPalette() const {
	// The sun of the panel (the table at 0x1E7E, one entry per sixteenth of
	// a day) is up from slot 0 to 12; the moon from 11 to 1. The sky follows:
	// sunrise, day, sunset, night (SKY.HSQ records, swift-dune's numbers).
	const uint slot = _world.timeSlot();
	if (slot == 0)
		return kSkySunrise;
	if (slot <= 10)
		return kSkyDay;
	if (slot <= 12)
		return kSkySunset;
	return kSkyNight;
}

void GameScreen::passTime(uint slots) {
	_world.advanceTime(slots);
	_clockStart = _system->getMillis();
	if (_world.takeEmperorEnding()) {
		_endingText = "As Paul Atreides failed";
		emperorEnding();
		return;
	}
	if (battleCheck())
		return;
	if (_mode == kRoom)
		drawRoom(); // the day counter, and the sky when the hour turned
}

bool GameScreen::battleCheck() {
	// night_attack_period_step (seg000:1bec): a death pending in the battle
	// (ds:46d9 = 6, COMMAND 0xc0) ends the game; a battle over clears ds:2b.
	if (_world.takePaulFate() == 6) {
		_battle = false;
		// COMMAND 0xc0 on the CD: two records after "You know what?..."
		// (0xbe); found by its neighbour, as the floppy's numbering differs.
		const uint16 shot = _panel.findCommand("You know what?", true);
		const Common::String text = shot != 0xffff ? _panel.commandString(shot + 2) : Common::String();
		_log.line(Common::String::format("Battle: Paul dies in the battle (\"%s\")", text.c_str()));
		_endingText = text.size() > 12 ? text.substr(0, 12) : text;
		emperorEnding();
		return true;
	}
	if (_battle && !_world.placeInBattle(_world.currentLocation())) {
		_battle = false;
		_state.setB(0x2b, 0);
		_log.line(Common::String::format("Battle: the battle at place %u is over", _world.currentLocation()));
	}
	return false;
}

void GameScreen::rideWormTo(int destination) {
	// map_confirm_travel_and_close (seg000:4703) in travel mode 2: calling a
	// worm ends a battle; the first ride is phase 0x50 (ds:0a bit 6,
	// charisma + 40); no ornithopter is taken and no Harkonnen zone is
	// checked on the way (seg000:4182 only runs for the ornithopter).
	_battle = false;
	_state.setB(0x2b, 0);
	_riding = true;
	setGamePhase(0x50);
	_log.line(Common::String::format("Travel: riding a worm to %d", destination));
	if (destination == -2)
		travelToward(_map->pointLongitude(), _map->pointLatitude());
	else
		travelTo((uint)destination);
	_riding = false;
}

void GameScreen::refreshRooms() {
	if (!_world.roomTable(_world.placeType(), _rooms)) {
		_rooms.clear();
		_log.line(Common::String::format("Rooms: no table for place type %#x", _world.placeType()));
	}
}

const RoomRecord *GameScreen::currentRoom() const {
	return _room >= 1 && _room <= _rooms.size() ? &_rooms[_room - 1] : nullptr;
}

void GameScreen::showRoom(uint number) {
	applyStory();
	_palacePlan = false;
	_mode = kRoom;
	_menu = kMenuNone;
	_desert = false;
	_walking = false;
	_commList = -1;
	_world.setPosition(_world.currentLocation(), number);
	_room = number;
	refreshRooms();
	updateRoomVars();
	if (_world.placeType() == Location::kHarkonnenPalace && number == 2 && !skipsRules() &&
			_state.b(GameState::kPhase) < 0xc8) {
		// sub_14057: location_and_room 0x3002, the Baron's hall: phase 0xc8
		// and the final scene (cs:128f, seg000:16fc).
		_log.line("Story: Paul enters the Baron's hall, the end");
		_state.setB(GameState::kPhase, 0xc8);
		_pendingScene = 0x128f;
	}
	if (_world.shipmentReady()) {
		// seg000:135ad: entering the COMM room with Duncan after agreeing,
		// the spice goes (sub_12566; its star-field animation is not shown).
		_world.shipSpice(_state.w(World::kAgreed));
	}
	debugSetRoom((int)number);
	const bool palace = _world.placeType() == Location::kPalace;
	if (palace && number >= 1 && number <= kPalaceRoomCount)
		debugSetScene(Common::String::format("palace/%s", palaceRoom(number).label));
	else
		debugSetScene(Common::String::format("place-%u/room-%u", _world.currentLocation(), number));
	drawRoom();
	// Dump names: the palace keeps "room-<n>" (the regression goldens), other places carry their index.
	dumpScreen(_system, (palace ? Common::String::format("room-%u", _room)
							   : Common::String::format("place-%u-room-%u", _world.currentLocation(), _room)).c_str());
	_idleStart = _system->getMillis();
	maybeStartScene();
}

void GameScreen::travelToward(uint16 longitude, int16 latitude) {
	const uint from = _world.currentLocation();
	_log.line(Common::String::format("Travel: toward the desert at %u/%d", longitude, latitude));
	const bool fromDesert = _desert;
	_desert = false;
	_walking = false;
	if (!fromDesert && !_riding) {
		if (_world.room() != 1 && parkedOrnis())
			_world.setPosition(from, 1);
		_room = _world.room();
		refreshRooms();
		animateOrni(+1);
		_world.setOrnithopters(from, -1);
	}
	const uint arrived = flyToward(longitude, latitude, -1);
	if (arrived == kDesertLanding) {
		_walkLng = longitude; // the landscape around the landing point
		_walkLat = latitude;
		_walkFine = 0;
		_landAtSet = true;
		landInDesert();
		return;
	}
	if (arrived == kShotDown) {
		_endingText = "Ah ah! One day";
		emperorEnding();
		return;
	}
	if (arrivalIsFatal(arrived))
		return;
	_world.markDiscovered(arrived);
	_world.setPosition(arrived, 1);
	_world.rollRoomRotation();
	_room = 1;
	refreshRooms();
	if (!_riding) {
		_world.setOrnithopters(arrived, +1);
		animateOrni(-1);
	}
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	showRoom(1);
	// A travel arrival reaches the room-entry scan too (seg000:35b4).
	if (_mode == kRoom)
		roomEntryScan();
}

void GameScreen::travelTo(uint locationIndex) {
	_log.line(Common::String::format("Travel: to place %u (type %#x)", locationIndex, _world.location(locationIndex).type));
	const uint from = _world.currentLocation();
	const bool fromDesert = _desert;
	_desert = false;
	_walking = false;
	if (from != locationIndex && !fromDesert && !_riding) {
		// play_travel_departure_transition: Paul's orni takes off from the pad
		// (seen from the place's first room), then leaves it (map_confirm_travel).
		if (_world.room() != 1 && parkedOrnis())
			_world.setPosition(from, 1);
		_room = _world.room();
		refreshRooms();
		animateOrni(+1);
		_world.setOrnithopters(from, -1);
	}
	const uint arrived = flyTo(locationIndex);
	if (arrived == kShotDown) {
		_endingText = "Ah ah! One day";
		emperorEnding();
		return;
	}
	if (arrivalIsFatal(arrived))
		return;
	_world.markDiscovered(arrived);
	_world.setPosition(arrived, 1);
	if (from != arrived || fromDesert) {
		// travel_finish_at_destination parks it on the destination's pad, and
		// the arrival lands it (travel_arrival_landing_sequence): on the CD
		// the place's approach clip (SIET, PALACE, FORT) plays to its end.
		_world.rollRoomRotation();
		if (!_riding)
			_world.setOrnithopters(arrived, +1);
		_room = 1;
		refreshRooms();
		if (_riding)
			_log.line("Travel: the worm stops at the place");
		else if (!_world.floppy() && !isFastCapture())
			playArrivalVideo(_world.location(arrived).type);
		else
			animateOrni(-1);
	}
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	showRoom(1);
	// A travel arrival reaches the room-entry scan too (seg000:35b4).
	if (_mode == kRoom)
		roomEntryScan();
}

void GameScreen::setRows(const RowAction *actions, const int *arguments, const uint16 *commands, uint count) {
	uint16 rows[Panel::kCommandRows];
	for (uint i = 0; i < Panel::kCommandRows; ++i) {
		_rowActions[i] = i < count ? actions[i] : kRowNone;
		_rowArguments[i] = i < count ? arguments[i] : 0;
		rows[i] = i < count ? commands[i] : 0xffff;
	}
	_panel.setCommandRows(rows, Panel::kCommandRows);
}

void GameScreen::composeView() {
	for (uint p = 0; p < ARRAYSIZE(_personPos); ++p)
		_personPos[p] = Common::Point(-1, -1); // only a drawn room places people
	if (_finalPicture) {
		showFinal(_finalPicture);
		return;
	}
	if (_desert) {
		if (_walking)
			drawWalkView();
		else
			drawDesert();
		return;
	}
	const RoomRecord *record = currentRoom();
	const byte placeType = _world.placeType();
	const bool palace = placeType == Location::kPalace;
	const bool floppy = _world.floppy();

	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);

	// Palette order matters: interface ranges first, then the room sheet,
	// which also owns 240-255 and so tints the panel.
	_panel.applyPalette();
	bool ok = record != nullptr;
	Common::Array<byte> roomData, sheetData;
	Common::String sheetName;
	uint salRoom = 0;
	if (ok) {
		salRoom = record->salRoom();
		sheetName = _world.sheetFor(*record);
		ok = _resources.load(World::salFile(placeType), roomData) && _resources.load(sheetName, sheetData);
		if (!ok)
			_log.line(Common::String::format("Room: %s or %s missing", World::salFile(placeType), sheetName.c_str()));
	} else {
		_log.line(Common::String::format("Room: no record %u in the table of place type %#x", _room, placeType));
	}
	if (ok) {
		// Rooms that look outside: the palace balcony and stairs (SAL rooms
		// 10 and 11) and every other place's entrance (SAL room 0). The
		// floppy draws them from their sheets under SKY.HSQ (the intro's
		// palace and sietch scenes); the CD's records hold only the
		// character markers and the last picture of the arrival video stays
		// behind them (SIET, PALACE, FORT). It is always midday until the
		// game clock exists.
		// An exterior is the palace balcony or front, or a room drawn from
		// an exterior sheet: the floppy's SIET0, VILG and FORT (slots 6, 8,
		// 9; the Harkonnen palace is FORT's room 4), the CD's GENERIC (0).
		const uint slot = record->sheetSlot();
		// The Amiga's are its slots 6 (SIET0), 9 (FORT), 15 (VILG1-6) and the
		// Harkonnen palace's front, HARKO room 6 (code 0xb7).
		const bool outdoors = palace ? (salRoom == 10 || salRoom == 11)
									 : _world.amiga() ? (slot == 6 || slot == 9 || slot == 15 || record->code == 0xb7)
									 : (floppy ? (slot == 6 || slot == 8 || slot == 9) : slot == 0);
		const bool video = !floppy && outdoors && !(palace && salRoom == 10);
		if (video) {
			if (!drawVideoBackdrop(placeType))
				_log.line(Common::String::format("Room: %s backdrop missing", World::arrivalVideo(placeType)));
			_panel.applyPalette(); // the interface colours over the video's
		} else if (outdoors && _world.amiga()) {
			// The Amiga pictures hold the whole view; the sky is their colour
			// 1, repainted below. A sietch or fortress entrance stands in
			// the open desert (code 0x534a): sky over sand (colour 2).
			if (!palace && placeType != Location::kHarkonnenPalace && !(placeType >= Location::kVillageMin && placeType <= Location::kVillageMax)) {
				_surface.fillRect(Common::Rect(0, 0, 320, 78), 1);
				_surface.fillRect(Common::Rect(0, 78, 320, 152), 2);
			}
		} else if (outdoors) {
			if (palace && salRoom == 11)
				drawSky(_system, _resources, *_surface.surfacePtr(), kSkyLarge, 200, skyPalette(), true);
			else
				drawSky(_system, _resources, *_surface.surfacePtr(), kSkyNarrow, 320, skyPalette(), true);
			if (!palace) {
				_surface.fillRect(Common::Rect(0, 77, 320, 152), 0xbf); // floppy 3AF8
				const bool landscape = floppy && _room == 1 &&
						(placeType <= Location::kSietchMax ||
						 (placeType >= Location::kFortressMin && placeType <= Location::kFortressMax));
				if (landscape) {
					// floppy 3C5C: a sietch's or fortress's entrance stands in the
					// desert landscape at its own position (desert.cpp), keyed by
					// location_and_room.
					const Location here = _world.location(_world.currentLocation());
					Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
					drawLandscape(view, here.longitude, here.latitude, 0, (uint16)((placeType << 8) | _room), true);
				}
			}
		}

		Sprite sheet(_system, sheetData);
		Sprite characters(_system, _panel.characterSheet());
		// The characters stand at the room's markers as the executable
		// places them (sub_13D83 / sub_13DF4 / sub_13D2F): each person, in
		// ascending group order, takes slot (group + ds:0xC5) mod markers or
		// else the first free slot; the markers then read the slots from the
		// last one down. PERS frame 2 x group; groups from 15 on (the troop
		// chiefs) are all drawn as 15. ds:0xC5 is 0 at the start and a new
		// random byte after each landing (World::rollRoomRotation).
		Common::Array<uint16> markerSprites;
		{
			Common::Array<byte> people;
			_world.peopleInRoom(people);
			const uint markers = MIN<uint>(Room(roomData).markerCount(salRoom), 23);
			byte slots[23];
			memset(slots, 0xff, sizeof(slots));
			const uint rotation = _state.b(0xc5) & 0x0f;
			if (_sceneActive && !_cast.empty()) {
				// A scripted scene places its cast itself (scene action 00:
				// sal_read_position_markers copies the list verbatim).
				for (uint i = 0; i < markers && i < _cast.size(); ++i)
					slots[i] = _cast[i];
				people.clear();
			}
			for (uint i = 0; i < people.size() && markers; ++i) {
				// Floppy CS:4014-401C removes travelling persons from the
				// room's standing sprites; they occupy the companion slots.
				if (people[i] < 16 && (_state.w(GameState::kPersonsWith) & (1 << people[i])))
					continue;
				uint slot = (people[i] + rotation) % markers;
				if (slots[slot] != 0xff) {
					for (slot = 0; slot < markers && slots[slot] != 0xff; ++slot) {
					}
					if (slot >= markers)
						continue;
				}
				slots[slot] = people[i];
			}
			markerSprites.resize(markers, 0xffff);
			Common::Array<Common::Point> positions;
			Room(roomData).markerPositions(salRoom, positions);
			for (uint p = 0; p < ARRAYSIZE(_personPos); ++p)
				_personPos[p] = Common::Point(-1, -1);
			Common::String standing;
			for (uint j = 0; j < markers; ++j) {
				const byte who = slots[markers - 1 - j];
				if (who != 0xff) {
					standing += Common::String::format(" %u@%u", who, j);
					// A Fremen stands as PERS 14-16 by its troop (seg000:913b).
					const uint troop = _world.troopForPerson(who);
					const uint figure = troop ? World::kFremen + World::fremenHead(troop) : MIN<uint>(who, World::kFremenChief);
					markerSprites[j] = (uint16)(2 * figure);
					if (who < ARRAYSIZE(_personPos) && j < positions.size()) {
						_personPos[who] = positions[j];
						_personFrame[who] = markerSprites[j];
					}
				}
			}
			// person@marker, logged when it changes (check_room_rotation.sh).
			const bool cast = _sceneActive && !_cast.empty();
			standing = Common::String::format("Room people: place %u room %u (%u markers, rotation %u%s):",
					_world.currentLocation(), salRoom, markers, rotation, cast ? ", scene cast" : "") + standing;
			if (standing != _lastStanding) {
				_lastStanding = standing;
				_log.line(standing);
			}
		}

		// The exterior sheets (SIET0, VILG, FORT, BALCON) carry no palette of
		// their own and take the sky's colours; PERS's went in first through
		// the panel, so nothing here disturbs the sky.
		if (!sheet.setPalette())
			_log.line(Common::String::format("Room: %s has a broken palette chunk", sheetName.c_str()));
		if (outdoors && _world.amiga())
			amigaSkyPalette(_system, _resources, _state.w(GameState::kGameTime),
					!palace && placeType != Location::kHarkonnenPalace && !(placeType >= Location::kVillageMin && placeType <= Location::kVillageMax));
		if (floppy && palace && outdoors && salRoom == 10 && !_world.amiga())
			// The intro's palace scenes, matched against the recording, put
			// BALCON frame 2 under the pieces the SAL record places.
			sheet.drawFrame(2, _surface.surfacePtr(), 0, 0);
		ok = Room(roomData).draw(salRoom, sheet, *_surface.surfacePtr(),
				_panel.characterSheet().empty() ? nullptr : &characters, &markerSprites);
		// draw_ornis_loop (seg000:3a32): the ornithopters parked on the pad
		// of the place's first room, in the room's own colours.
		if (ok && _room == 1 && _orniFrame != 0xff)
			drawParkedOrnis(*_surface.surfacePtr(), 0);
		if (ok && _sceneKiss)
			drawKiss();
		if (ok && outdoors && _world.amiga())
			amigaSkyGradient(*_surface.surfacePtr());
		if (!ok)
			_log.line(Common::String::format("Room: %s #%u did not parse", World::salFile(placeType), salRoom));
	}

	// Colour 0 is the black behind the view and the command box; several
	// room sheets set it to something else.
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);
	if (_world.amiga())
		amigaMirrorUiColours(_system);

	_viewOk = ok;
}

bool GameScreen::drawVideoBackdrop(byte placeType) {
	const char *name = World::arrivalVideo(placeType);
	if (_backdropName != name) {
		Common::Array<byte> video;
		_backdrop.clear();
		_backdropName = name;
		if (_resources.load(name, video)) {
			HnmPlayer player(_system);
			_backdrop.resize(320 * 200);
			if (!player.lastFrame(video, _backdrop.data(), _backdropPalette))
				_backdrop.clear();
			else
				_log.line(Common::String::format("Room: %s decoded for the backdrop", name));
		}
	}
	if (_backdrop.empty())
		return false;
	// The arrival videos carry no palette of their own (their first chunk is
	// a frame index): they play under the sky palette of the CD's SKYDN.HSQ,
	// whose records cover 73-239, the range the frames use, at the hour of the clock.
	Common::Array<byte> skyData;
	if (_resources.load("SKYDN.HSQ", skyData)) {
		Sprite sky(_system, skyData);
		if (!sky.setPaletteRecord(8 + skyPalette()))
			_log.line("Room: SKYDN.HSQ has no day palette record");
	} else {
		_log.line("Room: SKYDN.HSQ missing");
	}
	for (int y = 0; y < 152; ++y)
		memcpy(_surface.getBasePtr(0, y), _backdrop.data() + y * 320, 320);
	return true;
}

// COMMANDx names of the characters, in DIALOGUE.HSQ order.
static const char *const kCharacterCommands[World::kCharacters] = {
	"DUKE LETO ATREIDES", "JESSICA", "Thufir HAWAT", "Duncan IDAHO", "Gurney HALLECK", "STILGAR, Fremen leader",
	"KYNES, planetary ecologist", "CHANI", "HARAH", "BARON VLADIMIR HARKONNEN", "FEYD-RAUTHA HARKONNEN",
	"EMPEROR SHADDAM IV", "Harkonnen Captain", "Smuggler", "Fremen", "Fremen Chief"
};

const char *GameScreen::characterName(uint who) {
	static const char *const kChiefs[] = { "Fremen Chief", "2nd Fremen Chief", "3rd Fremen Chief", "4th Fremen Chief" };
	if (who < World::kFremenChief)
		return kCharacterCommands[who];
	if (who - World::kFremenChief < ARRAYSIZE(kChiefs))
		return kChiefs[who - World::kFremenChief];
	return nullptr;
}

void GameScreen::addRoomRows(RowAction *actions, int *arguments, uint16 *commands, bool *greyed, uint &count,
		bool canLeave) {
	auto add = [&](RowAction action, int argument, const char *text, bool grey = false, bool prefix = false) {
		const uint16 id = text ? _panel.findCommand(text, prefix) : 0xffff;
		if (count < Panel::kCommandRows && id != 0xffff) {
			actions[count] = action;
			arguments[count] = argument;
			commands[count] = id;
			greyed[count] = grey;
			++count;
		}
	};
	if (_sceneActive) {
		// A scripted scene between its lines: " Continue..." (menu ds:1fba).
		add(kRowContinue, 0, " Continue...");
		return;
	}
	if (_commList >= 0) {
		// The COMM message list (seg000:2864): the senders, newest first,
		// of the new or the seen messages, then "  Cancel" (seg000:29d4).
		for (int i = (int)_world.sightingCount() - 1; i >= 0 && count < Panel::kCommandRows - 1; --i) {
			const uint16 message = _world.sighting((uint)i);
			if (((message & 0x80) != 0) == (_commList == 1))
				add(kRowCommPick, i, characterName(message & 0x3f));
		}
		add(kRowCommCancel, 0, "Cancel", true);
		return;
	}
	const byte phase = _state.b(GameState::kPhase);
	add(kRowMap, 0, "SEE DUNE MAP");
	if (_desert) {
		// Outside a place (seg000:2faa): CALL A WORM (greyed before phase
		// 0x4f), WAIT FOR EVENING before period 11, else
		// WAIT FOR MORNING. The way back is the ornithopter parked beside
		// Paul (its hotspot, person 0x2f, gives TAKE AN ORNITHOPTER).
		add(kRowWorm, 0, "CALL A WORM", false, phase < 0x4f);
		if (_world.timeSlot() < 11)
			add(kRowWait, 0, "WAIT FOR EVENING");
		else
			add(kRowWait, 1, "WAIT FOR MORNING");
		// On foot (walked out of a place) there is no ornithopter to take.
		if (!_walking)
			add(kRowOrnithopter, 0, "TAKE AN ORNITHOPTER");
		return;
	}
	const bool palace = _world.placeType() == Location::kPalace;
	(void)canLeave;
	if (_battle && _room == 1) {
		// build_room_command_records (seg000:2efb) in a battle (ds:2b): no
		// ornithopter, the two ways to fight and the worm.
		add(kRowMassiveAttack, 0, "MASSIVE ATTACK");
		add(kRowFightDay, 0, "FIGHT FOR A WHOLE DAY");
		add(kRowWorm, 0, "CALL A WORM", false, phase < 0x4f);
		return;
	}
	if (_room == 1) {
		// The first room: TAKE AN ORNITHOPTER, greyed without one here.
		add(kRowOrnithopter, 0, "TAKE AN ORNITHOPTER", parkedOrnis() == 0);
	} else if (palace && _room == 8 && _world.sightingCount()) {
		// The COMM room: VIEW NEW MESSAGES (greyed with none unread) and
		// "Messages already seen" (greyed while none was read).
		const uint unread = _state.b(World::kUnread);
		add(kRowCommNew, 0, "VIEW NEW MESSAGES", unread == 0);
		add(kRowCommSeen, 0, "Messages already seen", unread >= _world.sightingCount());
	} else if (palace && _room == 9) {
		add(kRowMirror, 0, "LOOK AT MIRROR");
	}
	// The CD's room verbs end with Mixer Panel (CD sub_4DCB, loc_4E73: after
	// the ornithopter, messages or mirror, before the people; not in the
	// desert or a battle). The floppy's table has no such row.
	add(kRowMixer, 0, "Mixer Panel");
	(void)phase;
	// build_persons_in_room_records: the people present.
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	// Floppy CS:334D/3353 scans residents first, then companions. Their
	// travelling bits are the engine's source for the original flag 0x40.
	const uint16 with = _state.w(GameState::kPersonsWith);
	for (uint pass = 0; pass < 2; ++pass)
		for (uint i = 0; i < people.size(); ++i) {
			const bool companion = people[i] < 16 && (with & (1 << people[i]));
			if (companion == (pass != 0))
				add(kRowTalk, people[i], characterName(people[i]));
		}
}

void GameScreen::roomNav(bool exits[4], bool &canLeave) {
	// rebuild_and_draw_room_nav_panel (floppy seg000:329F): inside a place
	// (not a village) the room's exits light the arrows (0x01-0x7F and
	// 0xFB-0xFF; 0x80-0xFA are doors still shut), room 1 shows box 34 and
	// only the Atreides palace shows the red dot; the desert and villages
	// show box 35 with all four arrows.
	canLeave = false;
	for (uint d = 0; d < 4; ++d)
		exits[d] = false;
	const RoomRecord *record = currentRoom();
	if (_desert || _world.placeType() == Location::kVillageMin) {
		for (uint d = 0; d < 4; ++d)
			exits[d] = true;
		canLeave = true;
		_panel.setNavMode(Panel::kNavDesert);
		return;
	}
	if (record)
		for (uint d = 0; d < 4; ++d) {
			const byte e = record->exits[d];
			exits[d] = e != 0 && (e < 0x80 || e >= 0xfb);
			if (e >= 0xfb)
				canLeave = true;
		}
	if (_room == 1)
		_panel.setNavMode(Panel::kNavFront);
	else
		_panel.setNavMode(Panel::kNavRoom, _world.currentLocation() == 0);
}

void GameScreen::drawPalacePlan() {
	// ui_draw_palace_plan (CD seg000:18ee): the window ds:143c (160,0 -
	// 320,116) filled with colour 0xf1 and framed by four rings (loc_15b6e:
	// ds:1444 grown one pixel per ring, colours 0xf7, 0xf5, 0xf3, 0xf1),
	// then PALPLAN.HSQ's frames from the list at ds:120b (frame, x, y words
	// up to 0xffff: the plan at 182,12 and its labels), then the marks
	// (sub_11948).
	_surface.fillRect(Common::Rect(160, 0, 320, 116), 0xf1);
	for (int k = 0; k < 4; ++k)
		_surface.frameRect(Common::Rect(163 - k, 3 - k, 317 + k, 113 + k), (uint32)(0xf7 - 2 * k));
	Common::Array<byte> data;
	if (!_resources.load("PALPLAN.HSQ", data)) {
		_log.line("Palace plan: PALPLAN.HSQ missing");
		return;
	}
	Sprite plan(_system, data);
	const byte *v = _state.vars;
	uint list = _world.ds(0x120b);
	for (uint guard = 0; guard < 16 && list + 6 <= GameState::kSize; ++guard, list += 6) {
		const uint16 frame = READ_LE_UINT16(v + list);
		if (frame == 0xffff)
			break;
		plan.drawFrame(frame, _surface.surfacePtr(), (int16)READ_LE_UINT16(v + list + 2), (int16)READ_LE_UINT16(v + list + 4));
	}
	// sub_11948: per room, the characters of this place standing there
	// (records at 0xfd8, byte 3 = location + 1 against ds:7), in two rows by
	// the record's flag 0x40 (byte 15); sub_1127C leaves Gurney (id 4, byte
	// 14) out while the phase is 0x15-0x1f. Paul's room (ds:4) gets the red
	// mark.
	byte counts[36];
	memset(counts, 0, sizeof(counts));
	for (uint i = 0; i < 16; ++i) {
		const byte *c = v + World::kCharacterTable + i * World::kCharacterSize;
		if (c[3] != v[7])
			continue;
		const byte phase = _state.b(GameState::kPhase);
		if (c[14] == 4 && phase >= 0x15 && phase < 0x20)
			continue;
		const byte room = (byte)(c[0] - 1);
		const uint index = room + ((c[15] & 0x40) ? 12u : 0u);
		if (index < 24)
			counts[index]++;
	}
	if (v[4] <= 12)
		counts[0x17 + v[4]] = 1;
	// Rooms 2-12 (the first room, the front, is not on the plan): their
	// offsets from the plan's origin (ds:120d/120f) at ds:1426, x and y bytes.
	const int originX = (int16)READ_LE_UINT16(v + _world.ds(0x120b) + 2);
	const int originY = (int16)READ_LE_UINT16(v + _world.ds(0x120b) + 4);
	const uint offsets = _world.ds(0x1426);
	for (uint r = 1; r < 12; ++r) {
		const int x = originX + v[offsets + 2 * (r - 1)] + 3;
		const int y = originY + v[offsets + 2 * (r - 1) + 1] + 2;
		// sub_119df: up to five marks (frame 2), four pixels apart.
		for (uint k = 0; k < MIN<uint>(counts[r], 5); ++k)
			plan.drawFrame(2, _surface.surfacePtr(), x + 4 * (int)k, y);
		for (uint k = 0; k < MIN<uint>(counts[r + 12], 5); ++k)
			plan.drawFrame(2, _surface.surfacePtr(), x + 4 * (int)k, y + 7);
		if (counts[r + 24])
			plan.drawFrame(1, _surface.surfacePtr(), x + 9, y + 3);
	}
}

void GameScreen::drawRoom(int pressedRow, int pressedArrow) {
	composeView();
	const RoomRecord *record = currentRoom();

	bool exits[4] = { false, false, false, false };
	bool canLeave = false;
	roomNav(exits, canLeave); // bit 7 marks a door still hidden: the story opens it (seg000:1027)

	if (pressedRow < 0 && pressedArrow < 0)
		_log.line(Common::String::format("Room %u of place %u (type %#x, %s #%u): %s", _room,
				_world.currentLocation(), _world.placeType(), World::salFile(_world.placeType()),
				record ? record->salRoom() : 0, _viewOk ? "drawn" : "FAILED"));

	// The command box (build_room_command_records, seg000:2efb, then the
	// people present). Commands are resolved by text because CD and floppy
	// number them differently.
	RowAction actions[Panel::kCommandRows];
	int arguments[Panel::kCommandRows];
	uint16 commands[Panel::kCommandRows];
	bool greyed[Panel::kCommandRows];
	uint count = 0;
	if (_palacePlan) {
		// The plan's menu (ds:2012): "Done" alone.
		actions[0] = kRowPlanDone;
		arguments[0] = 0;
		commands[0] = _panel.findCommand("Done", true);
		greyed[0] = false;
		count = commands[0] != 0xffff ? 1 : 0;
	} else {
		addRoomRows(actions, arguments, commands, greyed, count, canLeave);
	}
	setRows(actions, arguments, commands, count);
	for (uint i = 0; i < count; ++i)
		if (greyed[i])
			_panel.setRowDisabled(i, true);
	if (_sceneActive || (_desert && !_walking))
		for (uint d = 0; d < 4; ++d)
			exits[d] = false;
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	if (_sceneActive)
		_panel.setNavMode(Panel::kNavBlank);
	_panel.draw(_surface, exits, pressedRow, pressedArrow, day());
	if (_palacePlan)
		drawPalacePlan();

	debugSetRoom((int)_room);
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

void GameScreen::panelAction(Panel::Action action, int row, int arrow) {
	if (action == Panel::kActionNone)
		return;
	if (action == Panel::kActionHead) {
		openMap(MapScreen::kGlobe, false);
		return;
	}
	if (action == Panel::kActionPlan) {
		// seg000:18ee: a second click closes it (the handler checks for its
		// own menu, bp = 2012h); only in the palace, not its first room.
		if (_world.currentLocation() != 0 || _room == 1 || _desert)
			return;
		_palacePlan = !_palacePlan;
		_log.line(_palacePlan ? "Palace plan: open" : "Palace plan: closed");
		drawRoom();
		return;
	}
	if (action == Panel::kActionCommand) {
		if (row < 0 || row >= (int)Panel::kCommandRows)
			return;
		const char *text = _panel.commandText(row);
		_log.line(Common::String::format("Command selected: %s", text ? text : "(none)"));
		switch (_rowActions[row]) {
		case kRowMap:
			openMap(MapScreen::kFlat, false);
			return;
		case kRowTalk:
			startConversation((uint)_rowArguments[row]);
			return;
		case kRowMirror:
			openMirror();
			return;
		case kRowPlanDone:
			_palacePlan = false;
			_log.line("Palace plan: closed");
			drawRoom();
			return;
		case kRowMixer:
			// menu_callback_choice_mixer_panel (CD seg000:a3f0): the volume
			// sliders and subtitle buttons; not built yet.
			_log.line("Mixer Panel: not built");
			showStatus("Dune: the Mixer Panel is not built yet");
			return;
		case kRowOrnithopter:
			openCockpit(false);
			return;
		case kRowWorm:
			// seg000:42d1: the map, choosing where the worm goes.
			_riding = true;
			openMap(MapScreen::kFlat, true);
			return;
		case kRowMassiveAttack:
			_world.massiveAttack(_world.currentLocation());
			dumpScreen(_system, "battle-massive");
			if (!battleCheck())
				drawRoom();
			return;
		case kRowFightDay:
			// seg000:0fc5: up to 16 periods, until the battle is over.
			for (uint n = 0; n < 16 && _battle && _mode == kRoom; ++n)
				passTime(1);
			return;
		case kRowContinue:
			sceneStep();
			return;
		case kRowCommNew:
		case kRowCommSeen:
			openComm(_rowActions[row] == kRowCommSeen);
			return;
		case kRowCommPick: {
			// seg000:290b: the sender's face and message (their topic-4 line,
			// its variant in ds:24).
			byte variant = 0;
			const byte person = _world.viewSighting((uint)_rowArguments[row], variant);
			_state.setB(0x24, variant);
			_state.setB(0xe9, person);
			_commList = -1;
			_log.line(Common::String::format("COMM: message from person %u, variant %u", person, variant));
			presentLine(person, person, 4, 0, kTalkComm);
			return;
		}
		case kRowCommCancel:
			_commList = -1;
			drawRoom();
			return;
		case kRowWait: {
			// WAIT FOR EVENING (seg000:0f48): to period 12 of today; WAIT FOR
			// MORNING (0f67): to period 0 of the next day. At phase 0x14 the
			// wait also counts as 1000 idle ticks (seg000:0f8e): the first
			// vision comes at once in the desert.
			const uint slot = _world.timeSlot();
			const uint slots = _rowArguments[row] ? World::kSlotsPerDay - slot : (slot < 12 ? 12 - slot : 0);
			passTime(slots);
			if (_state.b(GameState::kPhase) == 0x14)
				_idleStart = _system->getMillis() - 5000;
			drawRoom();
			return;
		}
		default:
			if (text)
				showStatus(Common::String::format("Dune: %s", text).c_str());
			return;
		}
	}
	if (action == Panel::kActionBook) {
		openBook();
		return;
	}

	if (_desert) {
		// ui_click_move_room in the desert (floppy 418B): each arrow is a step.
		if (_walking && action >= Panel::kActionUp && action <= Panel::kActionLeft)
			desertStep((uint)(action - Panel::kActionUp) + 1, true);
		return;
	}
	const RoomRecord *record = currentRoom();
	if (!record)
		return;
	const byte exit = record->exits[action - Panel::kActionUp];
	if (!exit || (exit < World::kExitWalkOut && (exit & 0x80)))
		return;
	if (exit >= World::kExitWalkOut) {
		// 0xFB-0xFF: Paul walks out into the desert (floppy 422C).
		walkOut(exit);
		return;
	}
	if (row >= 0 || arrow >= 0) {
		// Show the pressed control for a moment, as the original does.
		drawRoom(row, arrow);
		_system->delayMillis(120);
	}
	enterRoom(exit & 0x7f);
}

void GameScreen::enterRoom(uint room) {
	// ui_click_move_room (seg000:3f27): ds:26 cleared; the first move inside
	// a place marks it visited (status bit 4), counts a sietch (ds:25) and
	// flags the first entry (ds:26 = 0xff); ds:0c the new room; then
	// pending_room_action 5 for the entry lines (seg000:3fca).
	_state.setB(0x26, 0);
	byte &status = _state.vars[Location::kTableOffset + _world.currentLocation() * Location::kRecordSize + 10];
	if (!(status & 0x10)) {
		status |= 0x10;
		if (_world.placeType() < 0x20)
			_state.setB(0x25, (byte)(_state.b(0x25) + 1));
		_state.setB(0x26, 0xff);
	}
	_state.setB(0x0c, (byte)room);
	_state.setB(0x23, 5);
	showRoom(room);
	if (_mode == kRoom)
		roomEntryScan();
}

// ---- The map and the globe --------------------------------------------------

void GameScreen::openMap(MapScreen::Mode mode, bool selectDestination, bool fromFlatView) {
	_cockpit = false;
	loadDialogue(); // the sentences: place names and the box text
	if (!_map)
		_map = new MapScreen(_system, _resources, _log, _world);
	if (!_map->open(mode, selectDestination, fromFlatView)) {
		showStatus("Dune: map data missing");
		return;
	}
	_mode = kMap;
	debugSetScene(mode == MapScreen::kFlat ? "map/flat" : "map/globe");
	drawMapScreen();
	dumpScreen(_system, mode == MapScreen::kFlat ? (selectDestination ? "map-select" : "map-flat") : "map-globe");
}

void GameScreen::drawMapScreen() {
	if (_cockpit) {
		drawCockpit();
		return;
	}
	_map->setDensityForMap();
	_map->draw(_surface, _panel, _sentences, _state.b(GameState::kFremenTroops));
	if (_map->mode() == MapScreen::kGlobe && _map->results() >= 100)
		drawResults();

	RowAction actions[Panel::kCommandRows];
	int arguments[Panel::kCommandRows];
	uint16 commands[Panel::kCommandRows];
	bool greyed[Panel::kCommandRows];
	uint count = 0;
	auto add = [&](RowAction action, int argument, const char *text, bool prefix = false, bool grey = false) {
		const uint16 id = _panel.findCommand(text, prefix);
		if (count < Panel::kCommandRows && id != 0xffff) {
			actions[count] = action;
			arguments[count] = argument;
			commands[count] = id;
			greyed[count] = grey;
			++count;
		}
	};
	if (_map->mode() == MapScreen::kFlat) {
		// map_setup_main_menu (seg000:878c), menu_map_main (ds:20f2): EXIT
		// MAPS, the contact slot, SEE SPICE DENSITY, TAKE AN ORNITHOPTER and
		// FIND PROSPECTORS. Within a contact range under 2 cells (ds:1176) the
		// slot is GIVE ORDERS TO TROOP, greyed unless a troop is where Paul
		// stands; beyond it CONTACT FREMEN TROOPS, greyed without rallied
		// troops. The density is greyed and the prospectors hidden before
		// phase 5; the ornithopter is greyed without one parked here.
		const byte phase = _state.b(GameState::kPhase);
		add(kRowExitMap, 0, "EXIT MAPS");
		if (_map->selecting() && _movingTroop) {
			if (_map->destination() >= 0)
				add(kRowMoveDone, _map->destination(), "Done", true); // "  Done" on the CD, "Done" on the floppy
		} else if (_map->selecting() && (_map->destination() >= 0 || _map->destination() == -2)) {
			if (_riding)
				add(kRowWormTravel, _map->destination(), "GO THERE RIDING A WORM");
			else
				add(kRowFly, _map->destination(), "GO THERE FLYING AN ORNI");
		} else if (!_map->selecting() && _map->destination() >= 0 && (_state.b(World::kPaulEvents) & 0x40)) {
			// seg000:5ff9: once a worm was ridden the place's popup offers it.
			add(kRowWormTravel, _map->destination(), "GO THERE RIDING A WORM");
		}
		if (_world.contactRange() < 2)
			add(kRowOrders, 0, "GIVE ORDERS TO TROOP", false, !hiredTroopAt(_world.currentLocation()));
		else
			add(kRowContact, 0, "CONTACT FREMEN TROOPS", false, _state.b(GameState::kFremenTroops) == 0);
		// The row keeps its name while the overlay is up (it toggles it, as
		// the panel's close box does).
		add(kRowDensity, 0, "SEE SPICE DENSITY", false, phase < 5);
		add(kRowOrnithopter, 0, "TAKE AN ORNITHOPTER", false, parkedOrnis() == 0);
		if (phase >= 5)
			add(kRowProspectors, 0, "FIND PROSPECTORS"); // seg000:5b1e
	} else if (_menu == kMenuNone) {
		// Floppy DUNEPRG: DS:269c; CS:b840 patches the results row.
		// EXIT GLOBE's CS:bb80 handler returns to the flat map.
		add(kRowFlatMap, 0, "EXIT GLOBE");
		add(kRowResults, 0, _map->results() ? "STANDARD VISION" : "SEE RESULTS");
		add(kRowSaveMenu, 0, "SAVE GAME");
		add(kRowLoadMenu, 0, "LOAD GAME");
		// The CD's COMMAND1 has no "OPTIONS & QUIT GAME" (entry 181 is a
		// placeholder): its row reads EXIT GAME and leads to the same list.
		if (_panel.findCommand("OPTIONS & QUIT GAME") != 0xffff)
			add(kRowOptionsMenu, 0, "OPTIONS & QUIT GAME");
		else
			add(kRowOptionsMenu, 0, "EXIT GAME");
	} else if (_menu == kMenuSave || _menu == kMenuLoad) {
		// Shared with the mirror's save/load menus below.
	} else if (_menu == kMenuOptions) {
		// Floppy DS:26bc. CD-style opens DS:26d4 (CS:ac52).
		add(kRowMusic, 0, "MUSIC OFF");
		add(kRowMusic, 1, "MUSIC ON (GAME RELATIVE)");
		add(kRowMusicOrderMenu, 0, "MUSIC ON (CD-STYLE)");
		add(kRowExitGame, 0, "EXIT GAME");
		add(kRowMenuBack, 0, "Cancel", true);
	} else if (_menu == kMenuMusicOrder) {
		add(kRowMusicOrder, 1, "STANDARD ORDER");
		add(kRowMusicOrder, 3, "SHUFFLE");
		add(kRowMenuBack, kMenuOptions, "Cancel", true);
	} else {
		add(kRowConfirmExit, 0, "YES I WANT TO EXIT GAME");
		add(kRowCancelExit, 0, _panel.findCommand("NO I WISH TO CONTINUE") != 0xffff ? "NO I WISH TO CONTINUE"
																				  : "NO I DON'T WANT TO FINISH");
	}
	setRows(actions, arguments, commands, count);
	for (uint i = 0; i < count; ++i)
		if (greyed[i])
			_panel.setRowDisabled(i, true);
	if (_map->mode() == MapScreen::kGlobe && (_menu == kMenuSave || _menu == kMenuLoad))
		setSaveMenuRows();
	_panel.setLeftPanel(Panel::kLeftGlobe);
	const bool exits[4] = { false, false, false, false };
	const int selectedRow = _menu == kMenuOptions ? (!_musicOn ? 0 : (_musicOrder & 1) ? 2 : 1)
			: _menu == kMenuMusicOrder ? ((_musicOrder & 2) ? 1 : 0) : -1;
	_panel.draw(_surface, exits, selectedRow, -1, day());
	_map->drawPanelExtras(_surface, _panel);
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

void GameScreen::mapTap(int x, int y) {
	if (_mode != kMap || !_map)
		return;
	if (_cockpit) {
		cockpitTap(x, y);
		return;
	}
	const int hit = _map->hitLocation(x, y);
	if (hit < 0) {
		// A desert point: the flight goes that way, and whatever the
		// companions spot on the way can be visited (seg000:4944).
		if (_map->selecting() && _map->selectPoint(x, y)) {
			_log.line(Common::String::format("Map: desert point %u/%d selected", _map->pointLongitude(), _map->pointLatitude()));
			drawMapScreen();
		} else if (!_map->selecting() && _map->destination() >= 0) {
			// A tap on empty map space closes the place's popup (seg000:5c76).
			_map->select(-1);
			drawMapScreen();
		}
		return;
	}
	_map->select(hit);
	_log.line(Common::String::format("Map: place %u (%s) selected", hit,
			_sentences ? _world.locationName((uint)hit, *_sentences).c_str() : "?"));
	drawMapScreen();
	dumpScreen(_system, "map-selected");
}

void GameScreen::leaveMap() {
	_cockpit = false;
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	_menu = kMenuNone;
	_menuStatus = 0xffff;
	showRoom(_world.room());
}

// ---- Flight, results, troops ------------------------------------------------------

bool GameScreen::hiredTroopAt(uint location) const {
	Common::Array<uint> ids;
	_world.troopsAt(location, ids);
	for (uint i = 0; i < ids.size(); ++i)
		if (!_world.troop(ids[i]).harkonnen() && _world.troop(ids[i]).hired())
			return true;
	return false;
}

uint GameScreen::nextRalliedTroop(uint after) const {
	for (uint step = 1; step <= World::kTroops; ++step) {
		const uint id = (after + step - 1) % World::kTroops + 1;
		const Troop t = _world.troop(id);
		if (!t.harkonnen() && t.hired())
			return id;
	}
	return 0;
}

uint GameScreen::parkedOrnis() const {
	const Location l = _world.location(_world.currentLocation());
	const uint count = l.ornithopters + (l.type == Location::kPalace && !l.ornithopters ? 1 : 0);
	return MIN<uint>(count, 3);
}

void GameScreen::drawOrni(Graphics::Surface &target, int x, int y, uint frame) {
	// sub_13aa9: the body (ORNYTK 0), the hub (1) at +(6,30), the legs
	// (2 + clamp(frame - 15, 0, 5)) at +(4,50), the wings (8 + min(frame, 14))
	// at +(-81,-3). The sheet's palette chunk sets 80-96, its brown body and
	// green canopy (without it the outlines take the room's yellows).
	Common::Array<byte> data;
	if (!_resources.load("ORNYTK.HSQ", data))
		return;
	Sprite orni(_system, data);
	orni.setPalette();
	orni.drawFrame(0, &target, x, y);
	orni.drawFrame(1, &target, x + 6, y + 30);
	orni.drawFrame((uint16)(2 + CLIP<int>((int)frame - 15, 0, 5)), &target, x + 4, y + 50);
	// The Amiga's wing frames are cut to their size; code 0x547a adds this
	// table's shift to the DOS position.
	static const byte kAmigaWingShift[15] = { 0x8e, 0x8e, 0x8e, 0x89, 0x87, 0x81, 0x79, 0x6e, 0x5f, 0x50, 0x3f, 0x2b, 0x1d, 0x0d, 0x00 };
	const int wingX = x - 81 + (_world.amiga() ? kAmigaWingShift[MIN<uint>(frame, 14)] : 0);
	orni.drawFrame((uint16)(8 + MIN<uint>(frame, 14)), &target, wingX, y - 3);
}

int GameScreen::orniPadX() const {
	return _world.floppy() ? 0xb5 : 0xca; // floppy sub_5BD4 / CD seg000:3a95
}

void GameScreen::drawParkedOrnis(Graphics::Surface &target, uint skip) {
	// get_orni_position (CD seg000:3a95, floppy sub_5BD4): the pad at
	// (149, 57) for a sietch; elsewhere (202, 73) on the CD but (181, 73) on
	// the floppy (0xB5; the palace front's orni sat 21 px too far right).
	// Each further orni 70 to the right, 10 lower. The Amiga (code 0x5406,
	// 0x53c6) parks at (0xb5, 0x49) too and spaces the ornis 50 apart.
	const bool sietch = _world.placeType() < Location::kPalace;
	int x = sietch ? 0x95 : orniPadX(), y = sietch ? 0x39 : 0x49;
	const int step = _world.amiga() ? 0x32 : 0x46;
	const uint count = parkedOrnis();
	for (uint i = 0; i < count; ++i, x += step, y += 0x0a)
		if (i >= skip)
			drawOrni(target, x, y, 0);
}

void GameScreen::animateOrni(int step) {
	// orni_anim_loop / orni_anim_draw_frame (seg000:47fb, 4821): a frame per
	// 0x14 ticks (100 ms); past frame 14 the craft climbs away, 5 pixels a
	// frame sideways and (frame - 14)^2 / 2 up. Landing runs it backwards.
	if (_world.room() != 1 || !parkedOrnis())
		return;
	_orniFrame = 0xff;
	composeView();
	_orniFrame = 0;
	Graphics::ManagedSurface clean;
	clean.create(320, 152, Graphics::PixelFormat::createFormatCLUT8());
	clean.blitFrom(*_surface.surfacePtr(), Common::Rect(0, 0, 320, 152), Common::Point(0, 0));
	const bool sietch = _world.placeType() < Location::kPalace;
	const int padX = sietch ? 0x95 : orniPadX(), padY = sietch ? 0x39 : 0x49;
	const bool fast = isFastCapture();
	for (int frame = step > 0 ? 1 : 0x1f; frame >= 0 && frame <= 0x21; frame += step) {
		_surface.blitFrom(clean, Common::Rect(0, 0, 320, 152), Common::Point(0, 0));
		int x = padX, y = padY;
		if (frame > 14) {
			x -= 5 * (frame - 14) * (step > 0 ? 1 : 1);
			y -= (frame - 14) * (frame - 14) / 2;
		}
		drawOrni(*_surface.surfacePtr(), x, y, (uint)frame);
		// The other ornis stay parked (the step-first draw_ornis entry).
		drawParkedOrnis(*_surface.surfacePtr(), 1);
		if (fast) {
			if (frame == 20) {
				_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 152);
				dumpScreen(_system, step > 0 ? "orni-takeoff" : "orni-landing");
			}
			continue;
		}
		_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 152);
		_system->updateScreen();
		if (isRecording())
			recordFrame(_system, 100); // a recorded run keeps the frame's time
		else
			_system->delayMillis(100);
	}
	clean.free();
}

void GameScreen::openMirror() {
	// menu_callback_choice_palace_look_at_mirror (seg000:0ea6): the game
	// clock stops and the mirror still comes up.
	_mode = kMirror;
	_menu = kMenuNone;
	_menuStatus = 0xffff;
	_log.line("Mirror: Paul looks at himself");
	drawMirror();
	dumpScreen(_system, "mirror");
}

void GameScreen::drawMirror() {
	// callback_transition_look_at_mirror (seg000:0ed0): MIRROR.HSQ's
	// reflected bedroom (frames 0 and 1), Paul's face, then the gilt frame (2);
	// the menu is RESTART / LOAD / SAVE / EXIT GAME and Look away (ds:1d1e).
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	Common::Array<byte> data, paul;
	if (_world.amiga()) {
		// The Amiga's mirror picture has no palette: it is the bedroom's (POR).
		Common::Array<byte> bedroom;
		if (_resources.load("POR.HSQ", bedroom))
			Sprite(_system, bedroom).setPalette();
	}
	if (_resources.load("MIRROR.HSQ", data)) {
		Sprite mirror(_system, data);
		mirror.setPalette();
		// The reflected bedroom is two frames: 0 (colours 129-141, the walls,
		// door and shelves) and 1 (144-158); the recording shows both. The
		// Amiga's picture is frame 1 alone.
		if (!_world.amiga())
			mirror.drawFrame(0, _surface.surfacePtr(), 0, 0);
		mirror.drawFrame(1, _surface.surfacePtr(), 0, 0);
		if (_resources.load("PAUL.HSQ", paul)) {
			Sprite face(_system, paul);
			face.setPalette();
			// Centred in the frame; Paul ages with the clock (seg000:917a).
			const uint expression = MIN<uint>((uint)(_state.w(GameState::kGameTime) >> 6), 8u) * 2;
			if (_world.amiga()) {
				// The Amiga's picture includes the gilt frame: Paul, where he
				// talks from (measured on the recording), shows inside it.
				const Common::Rect glass(12, 12, 306, 138);
				Graphics::Surface inside = _surface.surfacePtr()->getSubArea(glass);
				if (!face.drawAnimationFrame(expression, 0, &inside, -glass.left, -glass.top))
					face.drawAnimationFrame(0, 0, &inside, -glass.left, -glass.top);
			} else if (!face.drawAnimationFrame(expression, 0, _surface.surfacePtr(), 86, 0)) {
				face.drawAnimationFrame(0, 0, _surface.surfacePtr(), 86, 0);
			}
		}
		mirror.setPalette();
		mirror.drawFrame(2, _surface.surfacePtr(), 0, 0);
	}
	_panel.applyPalette();
	RowAction actions[Panel::kCommandRows];
	int arguments[Panel::kCommandRows] = { 0, 0, 0, 0, 0 };
	uint16 commands[Panel::kCommandRows];
	uint count = 0;
	auto add = [&](RowAction action, const char *text) {
		const uint16 id = _panel.findCommand(text);
		if (count < Panel::kCommandRows && id != 0xffff) {
			actions[count] = action;
			commands[count++] = id;
		}
	};
	if (_menu == kMenuNone) {
		add(kRowRestart, "RESTART GAME");
		add(kRowLoadMenu, "LOAD GAME");
		add(kRowSaveMenu, "SAVE GAME");
		add(kRowExitGame, "EXIT GAME");
		add(kRowMirrorAway, "Look away from the mirror");
	} else if (_menu == kMenuQuit) {
		add(kRowConfirmExit, "YES I WANT TO EXIT GAME");
		add(kRowCancelExit, _panel.findCommand("NO I WISH TO CONTINUE") != 0xffff ? "NO I WISH TO CONTINUE"
																				  : "NO I DON'T WANT TO FINISH");
	}
	setRows(actions, arguments, commands, count);
	if (_menu == kMenuSave || _menu == kMenuLoad)
		setSaveMenuRows();
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	const bool exits[4] = { false, false, false, false };
	_panel.setCompassBlank(true); // the mirror's compass screen is dark
	_panel.draw(_surface, exits, -1, -1, day());
	_panel.setCompassBlank(false);
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

uint GameScreen::flyTo(uint locationIndex) {
	const Location l = _world.location(locationIndex);
	return flyToward(l.longitude, l.latitude, (int)locationIndex);
}

uint GameScreen::flyToward(uint16 targetLongitude, int16 targetLatitude, int target) {
	// The ornithopter flight as the executable runs it (travel_pump,
	// seg000:4f0c): a step of one map cell every 0x300 PIT ticks (3.83 s),
	// one time period every 16 steps (seg000:4b3b), the view following Paul's
	// position with its trail. A tap skips to the destination (SKIP TO
	// DESTINATION, seg000:4ffb). A companion may sight a place on the way
	// (seg000:40f9, showSighting).
	const uint from = _world.currentLocation();
	int destination = target;
	if ((int)from == destination)
		return from;
	Location a = _world.location(from), b = a;
	b.longitude = targetLongitude;
	b.latitude = targetLatitude;
	uint16 lng = a.longitude;
	int16 lat = a.latitude;
	const int startCell = _world.mapCell(a.longitude, a.latitude);
	uint total = _world.cellDistance(a.longitude, a.latitude, b.longitude, b.latitude);
	uint step = 0;
	const uint32 kTurnRepeatMillis = 250; // 0x32 ticks between repeats of a held turn
	bool skipping = isFastCapture();
	if (!skipping) {
		if (!_map)
			_map = new MapScreen(_system, _resources, _log, _world);
		if (!_map->open(MapScreen::kFlat, false))
			skipping = true;
		loadDialogue();
	}
	_mode = kMap;
	// The floppy draws the flight as the desert view; the CD plays its MNT
	// videos (not yet), so it keeps the flat map with the trail.
	Common::Array<byte> dunesData;
	Sprite *dunes = nullptr; // set when the floppy's landscape is drawn (DUNES.HSQ)
	Common::Array<byte> ornypanData;
	Sprite *ornypan = nullptr; // its palette colours the minimap (checked against the original's flight)
	if (_resources.load("ORNYPAN.HSQ", ornypanData))
		ornypan = new Sprite(_system, ornypanData);
	if (!skipping && _resources.load("DUNES.HSQ", dunesData))
		dunes = new Sprite(_system, dunesData);
	// The route (travel_step_position, floppy 7E87 in IDA numbering; see
	// World::travelStep): a heading of 256 units a turn, re-aimed from the
	// current position at the destination before every step while homing
	// (7E4C). A free flight (a desert point picked in the cockpit) keeps its
	// heading, can be steered, and never lands (seg000:4944): it flies on
	// until BACK TO STARTING POINT, TOWARDS NEAREST PLACE, CHANGE DESTINATION
	// or a sighting. The worm, the dump and the speedrun runs keep the old
	// landing. Checked step by step against the original's memory in a
	// palace -> Carthag-Tuek flight (Spice86 memory dumps).
	uint16 destLng = targetLongitude;
	int16 destLat = targetLatitude;
	byte fraction = 0x80; // travel_step_accum at take-off
	bool freeFlight = target < 0 && !_riding && !isFastCapture();
	bool fixedHeading = false; // ds:11D5: set by steering
	byte heading = 0;
	World::compassAngle(lng, lat, destLng, destLat, heading);
	auto routeStep = [&](uint16 &stepLng, int16 &stepLat, byte &stepFraction, uint16 fromLng, int16 fromLat) {
		// 7E4C: homing re-aims from where the ornithopter is (ds:4/ds:6, even
		// during the look-ahead); a fixed heading only gets the polar guard.
		if (freeFlight || fixedHeading) {
			if (stepLat < -0x4d || stepLat > 0x4d) {
				const byte ah = (byte)((byte)(heading - 0x40) ^ (byte)((uint16)stepLat >> 8));
				if (!(ah & 0x80))
					heading = (byte)((heading & 0x80) | 0x40);
			}
		} else {
			byte aim;
			if (World::compassAngle(fromLng, fromLat, destLng, destLat, aim))
				heading = aim;
		}
		_world.travelStep(stepLng, stepLat, stepFraction, heading);
	};
	uint hostileSteps = 0;
	// The flight's verbs (build_room_command_records, floppy 327B): homing
	// SKIP TO DESTINATION (greyed while the hostile-zone count runs) and
	// CHANGE DESTINATION; free BACK TO STARTING POINT, TOWARDS NEAREST PLACE
	// from phase 0x32, CHANGE DESTINATION.
	enum FlightVerb { kVerbSkip, kVerbChange, kVerbBack, kVerbNearest };
	FlightVerb verbs[Panel::kCommandRows];
	uint verbCount = 0;
	auto setFlightRows = [&]() {
		static const char *const kVerbText[4] = { "SKIP TO DESTINATION", "CHANGE DESTINATION", "BACK TO STARTING POINT",
												  "TOWARDS NEAREST PLACE" };
		verbCount = 0;
		if (freeFlight) {
			verbs[verbCount++] = kVerbBack;
			if (_state.b(GameState::kPhase) >= 0x32)
				verbs[verbCount++] = kVerbNearest;
		} else {
			verbs[verbCount++] = kVerbSkip;
		}
		verbs[verbCount++] = kVerbChange;
		RowAction actions[Panel::kCommandRows];
		int arguments[Panel::kCommandRows];
		uint16 commands[Panel::kCommandRows];
		for (uint i = 0; i < verbCount; ++i) {
			actions[i] = kRowNone;
			arguments[i] = 0;
			commands[i] = _panel.findCommand(kVerbText[verbs[i]]);
		}
		setRows(actions, arguments, commands, verbCount);
		if (!freeFlight && hostileSteps)
			_panel.setRowDisabled(0, true);
		// rebuild_and_draw_room_nav_panel in travel (floppy 516F): the steering
		// arrows in a free flight, the dark screen while homing.
		_panel.setNavMode(freeFlight ? Panel::kNavFlight : Panel::kNavBlank);
	};
	// The landscape (floppy 4F63): its rows follow the route five steps ahead.
	const uint32 kLandFrameMillis = 80; // a frame per 16 ticks (the frame task at 546D)
	uint landFrames = 0; // landscape frames since the rows were laid out
	auto startLandscape = [&]() {
		// 76CA: five groups of rows, each seeded one register step further on
		// (the fraction kept).
		uint16 longitudes[5];
		int16 latitudes[5];
		uint16 aheadLng = lng;
		int16 aheadLat = lat;
		byte aheadFraction = fraction;
		for (uint k = 0; k < 5; ++k) {
			longitudes[k] = aheadLng;
			latitudes[k] = aheadLat;
			routeStep(aheadLng, aheadLat, aheadFraction, lng, lat);
		}
		flightLandscapeStart(longitudes, latitudes);
		// The step counter (ds:4286) is 0 after the take-off: the first frame
		// already brings a step, then one every 8 frames (reloaded with 7).
		_flightTicks = 7;
		landFrames = 0;
	};
	auto reseedLandscape = [&]() {
		// 7AC2: five register steps ahead (the fraction kept, the heading
		// re-aimed from here), seed = longitude ^ latitude there.
		uint16 aheadLng = lng;
		int16 aheadLat = lat;
		byte aheadFraction = fraction;
		for (uint k = 0; k < 5; ++k)
			routeStep(aheadLng, aheadLat, aheadFraction, lng, lat);
		flightLandscapeReseed(aheadLng, aheadLat);
	};
	const uint32 flightStart = _system->getMillis();
	uint32 lastTimedDump = 0;
	const bool cdView = !skipping && !dunes && !_world.floppy() && startCdFlightView();
	auto present = [&]() {
		if (cdView) {
			// The terrain six cells ahead on the route (travel_probe_terrain_ahead, seg000:4e8e).
			uint16 aLng = lng;
			int16 aLat = lat;
			byte aFraction = fraction;
			const byte keepHeading = heading;
			for (uint k = 0; k < 6; ++k)
				routeStep(aLng, aLat, aFraction, lng, lat);
			heading = keepHeading;
			const int c0 = _world.mapCell(lng, lat), c1 = _world.mapCell(aLng, aLat);
			const Common::Array<byte> &m = _world.map();
			const byte t0 = c0 >= 0 ? (m[c0] & 0x0f) : 0, t1 = c1 >= 0 ? (m[c1] & 0x0f) : 0;
			drawCdFlightView((byte)((t0 + t1) / 2));
			_map->drawMinimap(_surface, Common::Rect(202, 3, 318, 61), _panel);
			setFlightRows();
			_panel.setLeftPanel(Panel::kLeftBook);
			_panel.setCompanions(_world.companion(0), _world.companion(1));
			const bool exits[4] = { false, false, false, false };
			_panel.draw(_surface, exits, -1, -1, day());
			_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
			_system->updateScreen();
			if (!_pendingFlightDump.empty()) {
				dumpScreen(_system, _pendingFlightDump.c_str());
				_pendingFlightDump.clear();
			}
			return;
		}
		if (!dunes) {
			drawMapScreen();
			return;
		}
		Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
		_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
		_panel.applyPalette();
		// ONMAP's palette, then ORNYPAN's (the cockpit's sheet, still
		// installed from the take-off: the minimap's terrain 0x14-0x1D in its
		// browns, and the trail's colours), then the sky's.
		_map->applyPalette();
		if (ornypan)
			ornypan->setPalette();
		if (_world.amiga()) {
			// DUNES3 is not ported: the Amiga's sky over its sand colour.
			view.fillRect(Common::Rect(0, 0, 320, 78), 1);
			amigaSkyPalette(_system, _resources, _state.w(GameState::kGameTime), true);
		} else {
			drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette(), true);
		}
		const uint32 nowMillis = _system->getMillis();
		// A frame every 16 ticks (546D); the eighth frame since the last step
		// makes the next one due (the counter at ds:4286), so rows and steps
		// keep the original's order: the step's row is drawn with the old seed.
		bool ticked = false;
		while (nowMillis - _flightFrameAt >= kLandFrameMillis && _flightTicks < 8) {
			flightLandscapeTick();
			_flightFrameAt += kLandFrameMillis;
			++_flightTicks;
			++landFrames;
			ticked = true;
		}
		if (nowMillis - _flightFrameAt >= 8 * kLandFrameMillis)
			_flightFrameAt = nowMillis - kLandFrameMillis; // after a pause, no rush of frames
		if (_world.amiga()) {
			view.fillRect(Common::Rect(0, 78, 320, 152), 2);
			amigaSkyGradient(view);
			amigaMirrorUiColours(_system);
		} else {
			flightLandscapeDraw(view, dunesData);
		}
		_map->drawMinimap(_surface, Common::Rect(202, 3, 318, 61), _panel);
		setFlightRows();
		_panel.setLeftPanel(Panel::kLeftBook);
		_panel.setCompanions(_world.companion(0), _world.companion(1));
		const bool exits[4] = { false, false, false, false };
		_panel.draw(_surface, exits, -1, -1, day());
		_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
		_system->updateScreen();
		// dune_dump_every samples the real-time flight too (flight-rt-<ms>),
		// and names each landscape frame by its number since the rows were laid
		// out (flight-frame-<n>), to set beside the original's.
		if (dumpEveryMillis() && ticked)
			dumpScreen(_system, Common::String::format("flight-frame-%03u", landFrames).c_str());
		if (dumpEveryMillis() && _system->getMillis() - lastTimedDump >= dumpEveryMillis()) {
			lastTimedDump = _system->getMillis();
			dumpScreen(_system, Common::String::format("flight-rt-%06u", lastTimedDump - flightStart).c_str());
		}
	};
	uint32 next = _system->getMillis();
	uint periods = 0;
	if (skipping && isDumpRun() && !isDuneHarnessRun()) {
		// Dump runs fly instantly; picture the view once ("flight").
		if (!_map)
			_map = new MapScreen(_system, _resources, _log, _world);
		if (_map->open(MapScreen::kFlat, false) && _resources.load("DUNES.HSQ", dunesData)) {
			loadDialogue();
			dunes = new Sprite(_system, dunesData);
			_map->addFlightTrail(a.longitude, a.latitude);
			_map->setFlight(true, (uint16)(a.longitude + (int16)(b.longitude - a.longitude) / 3),
					(int16)(a.latitude + (b.latitude - a.latitude) / 3), destination);
			startLandscape();
			for (uint n = 0; n < 20; ++n)
				flightLandscapeTick(); // the frame below shows the rows in flight
			_flightTicks = 0;
			Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
			_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
			_panel.applyPalette();
			_map->applyPalette();
			if (_world.amiga()) {
				view.fillRect(Common::Rect(0, 0, 320, 78), 1);
				amigaSkyPalette(_system, _resources, _state.w(GameState::kGameTime), true);
				view.fillRect(Common::Rect(0, 78, 320, 152), 2);
				amigaSkyGradient(view);
				amigaMirrorUiColours(_system);
			} else {
				drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette(), true);
				flightLandscapeDraw(view, dunesData);
			}
			_map->drawMinimap(_surface, Common::Rect(202, 3, 318, 61), _panel);
			uint16 commands[Panel::kCommandRows] = { _panel.findCommand("SKIP TO DESTINATION"), _panel.findCommand("CHANGE DESTINATION") };
			RowAction actions[Panel::kCommandRows] = { kRowNone, kRowNone };
			int arguments[Panel::kCommandRows] = { 0, 0 };
			setRows(actions, arguments, commands, 2);
			_panel.setRowDisabled(1, true);
			const bool exits[4] = { false, false, false, false };
			_panel.draw(_surface, exits, -1, -1, day());
			_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
			_system->updateScreen();
			dumpScreen(_system, "flight");
			_map->setFlight(false, 0, 0, -1);
			delete dunes;
			dunes = nullptr;
		}
	}
	// CHANGE DESTINATION (floppy 51FD): the cockpit again over the flight; a
	// pick re-aims the flight under way, Cancel carries on.
	auto changeDestination = [&]() {
		_flightLng = (uint16)lng;
		_flightLat = (int16)lat;
		openCockpit(true);
		while (_cockpit && !_quitRequested) {
			Common::Event event;
			while (_cockpit && pollDuneEvent(_system, event)) {
				if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
					_quitRequested = true;
				} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
					cockpitCancel();
				} else if (event.type == Common::EVENT_LBUTTONDOWN) {
					const int arrow = _map->hitArrow(event.mouse.x, event.mouse.y);
					int row, arrowHit;
					if (arrow >= 0) {
						static const int dx[5] = { 0, 1, 0, -1, 0 }, dy[5] = { -1, 0, 1, 0, 0 };
						if (arrow == 4)
							_map->centreOnPosition(_flightLng, _flightLat);
						else
							_map->scroll(dx[arrow], dy[arrow]);
						drawMapScreen();
					} else if (_panel.hitTest(event.mouse.x, event.mouse.y, row, arrowHit) == Panel::kActionCommand && row == 0) {
						cockpitCancel();
					} else if (event.mouse.y < 152) {
						cockpitTap(event.mouse.x, event.mouse.y);
					}
				}
			}
			updateCockpit(_system->getMillis());
			_system->delayMillis(10);
		}
		_cockpit = false;
		_mode = kMap;
		if (_changeTarget >= 0) {
			destination = _changeTarget;
			const Location d = _world.location((uint)destination);
			destLng = d.longitude;
			destLat = d.latitude;
			freeFlight = false;
		} else if (_changeTarget == -2) {
			destination = -1;
			destLng = _changeLongitude;
			destLat = _changeLatitude;
			freeFlight = !_riding;
			World::compassAngle(lng, lat, destLng, destLat, heading);
			fraction = 0x80;
		}
		_changeTarget = -1;
		next = _system->getMillis() + World::kFlightStepMillis;
		_flightFrameAt = _system->getMillis();
	};
	auto aimAt = [&](uint place) {
		destination = (int)place;
		const Location d = _world.location(place);
		destLng = d.longitude;
		destLat = d.latitude;
		freeFlight = false;
		fixedHeading = false;
	};
	// The pump's arrival test (floppy 5D1A): the cell under the ornithopter
	// is the destination's.
	auto arrived = [&]() { return _world.mapCell(lng, lat) == _world.mapCell(destLng, destLat); };
	// The steering arrows (ui_nav_panel_flight, floppy ds:2404): press and
	// hold turns 4 units every 50 ticks; the middle one resumes the flight.
	const Common::Rect kTurnLeft(258, 172, 267, 183), kResume(270, 170, 280, 183), kTurnRight(283, 172, 292, 183);
	int turning = 0;
	uint32 nextTurn = 0;
	const uint cap = 4 * total + 64;
	auto stepDue = [&]() { return dunes ? _flightTicks >= 8 : _system->getMillis() >= next; };
	for (;;) {
		if (!freeFlight && arrived())
			break;
		if (!freeFlight && step >= cap) {
			lng = destLng; // a route that cannot close in: the destination
			lat = destLat;
			break;
		}
		if (!skipping && step > 0) {
			// The take-off already took the first step (floppy 4F63).
			while (!stepDue() && !skipping && !_quitRequested && !isRecording()) {
				Common::Event event;
				while (pollDuneEvent(_system, event)) {
					if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
						_quitRequested = true;
					} else if (event.type == Common::EVENT_LBUTTONUP) {
						turning = 0;
					} else if (event.type == Common::EVENT_LBUTTONDOWN) {
						int row, arrow;
						const Common::Point at(event.mouse.x, event.mouse.y);
						if (freeFlight && (kTurnLeft.contains(at) || kTurnRight.contains(at))) {
							turning = kTurnLeft.contains(at) ? -4 : 4;
							nextTurn = _system->getMillis();
						} else if (freeFlight && kResume.contains(at)) {
							// RESUME FLIGHT (floppy 5CE5): nothing is paused here.
						} else if (_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) == Panel::kActionCommand &&
								   row >= 0 && row < (int)verbCount) {
							switch (verbs[row]) {
							case kVerbSkip:
								if (!hostileSteps)
									skipping = true;
								break;
							case kVerbChange:
								changeDestination();
								break;
							case kVerbBack:
								_log.line("Flight: BACK TO STARTING POINT");
								aimAt(from);
								break;
							case kVerbNearest: {
								uint best = from, bestDistance = 0xffff;
								for (uint i = 0; i < _world.locationCount(); ++i) {
									const Location c = _world.location(i);
									const uint d = _world.cellDistance((uint16)lng, (int16)lat, c.longitude, c.latitude);
									if (!c.hidden() && d < bestDistance) {
										bestDistance = d;
										best = i;
									}
								}
								_log.line(Common::String::format("Flight: TOWARDS NEAREST PLACE, place %u", best));
								aimAt(best);
								break;
							}
							}
						} else if (!freeFlight && event.mouse.y < 152) {
							skipping = true; // a tap on the view skips, as SKIP TO DESTINATION
						}
					}
				}
				if (turning && freeFlight && _system->getMillis() >= nextTurn) {
					// adjust_travel_heading (floppy 5ECA): +-4 of 256, fixed-heading
					// mode, the fraction back to 0x80.
					heading = (byte)(heading + turning);
					fixedHeading = true;
					fraction = 0x80;
					flightLandscapePan(turning < 0 ? -1 : 1); // the floppy pans the view 4 px a turn
					nextTurn = _system->getMillis() + kTurnRepeatMillis;
				}
				if (dunes || cdView)
					present();
				_system->delayMillis(dunes || cdView ? 40 : 10);
			}
			next += World::kFlightStepMillis;
		}
		if (_quitRequested)
			break;
		++step;
		if (step > 1)
			_flightTicks = 0;
		{
			const uint16 fromLng = lng;
			const int16 fromLat = lat;
			routeStep(lng, lat, fraction, fromLng, fromLat);
		}
		if (dunes) {
			if (step == 1)
				startLandscape(); // 4F63: the rows are laid out after the first step
			else
				reseedLandscape();
		}
		if (step % 16 == 0) {
			_world.advanceTime(1);
			++periods;
		}
		// travel_route_hostile_zone_check (seg000:4182): flying to a place
		// that is not the Atreides', every step over Harkonnen land (cell
		// stage 0x30) takes 0x20 from the accumulator; the first warns
		// (ENTERING HARKONNEN ZONE, and SKIP TO DESTINATION stops), the
		// eighth in a row brings the ornithopter down (room screen 2).
		if (!isFastCapture() && !_riding) {
			const bool safe = destination >= 0 && _world.friendlyPlace((uint)destination);
			if (!safe && _world.cellStage((uint16)lng, (int16)lat) == 0x30) {
				if (!hostileSteps) {
					skipping = false;
					if (!dunes)
						present();
					if (!askHostileZone())
						aimAt(from); // BACK TO STARTING POINT
					next = _system->getMillis() + World::kFlightStepMillis;
					_flightFrameAt = _system->getMillis();
				}
				if (++hostileSteps >= 8) {
					_log.line("Flight: shot down over Harkonnen land");
					if (_map)
						_map->setFlight(false, 0, 0, -1);
					delete dunes;
					delete ornypan;
					return kShotDown;
				}
			} else {
				hostileSteps = 0;
			}
		}
		if (!skipping) {
			_map->addFlightTrail((uint16)lng, (int16)lat);
			_map->setFlight(true, (uint16)lng, (int16)lat, freeFlight ? (int)from : destination);
			present();
			recordFrame(_system, World::kFlightStepMillis); // a recorded run: one frame a cell
		}
		if (freeFlight && step > 1 && _world.mapCell(lng, lat) == startCell) {
			// The route crossed the starting cell, the free flight's destination.
			destination = (int)from;
			break;
		}
		// travel_scan_nearby_location (floppy 4353): with someone travelling
		// with Paul (ds:10), a findable place in the 9x9 block round him and
		// within 135 degrees of the heading; the last one found. It is marked
		// discovered, the flight homes on it at once, and the companion says
		// so in the cabin (3924). The speedrun bot finds places itself.
		if (!skipping && _state.w(GameState::kPersonsWith) && !ConfMan.hasKey("dune_speedrun")) {
			int sighted = -1;
			byte sightedBearing = 0;
			for (uint i = 0; i < _world.locationCount(); ++i) {
				if ((int)i == destination || !_world.discoverable(i))
					continue;
				const Location sl = _world.location(i);
				if (_world.cellDistance((uint16)lng, (int16)lat, sl.longitude, sl.latitude) > 4)
					continue;
				byte angle = heading;
				World::compassAngle(lng, lat, sl.longitude, sl.latitude, angle);
				const byte bearing = (byte)(angle - heading);
				if ((byte)(bearing + 0x60) >= 0xc0)
					continue;
				sighted = (int)i;
				sightedBearing = bearing;
			}
			if (sighted >= 0) {
				_world.markDiscovered((uint)sighted);
				aimAt((uint)sighted);
				showSighting((uint)sighted, sightedBearing);
				next = _system->getMillis() + World::kFlightStepMillis;
				_flightFrameAt = _system->getMillis();
			}
		}
	}
	if (_map)
		_map->setFlight(false, 0, 0, -1);
	delete dunes;
	delete ornypan;
	stopCdFlightView();
	_clockStart = _system->getMillis();
	if (destination < 0) {
		// A flight to a point of the desert lands there (current_scene
		// 0xff); the dump and harness runs keep the old nearest-place end.
		if (!skipsRules()) {
			_log.line(Common::String::format("Flight: place %u -> the desert, %u cells", from, total));
			return kDesertLanding;
		}
		uint best = from, bestDistance = 0xffff;
		for (uint i = 0; i < _world.locationCount(); ++i) {
			const Location c = _world.location(i);
			if (c.hidden())
				continue;
			const uint d = _world.cellDistance((uint16)lng, (int16)lat, c.longitude, c.latitude);
			if (d < bestDistance) {
				bestDistance = d;
				best = i;
			}
		}
		destination = (int)best;
	}
	_log.line(Common::String::format("Flight: place %u -> %d, %u cells, %u steps, %u period(s)", from, destination, total, step, periods));
	return (uint)destination;
}

bool GameScreen::askHostileZone() {
	// pending_room_action 4 (seg000:41ae): "  ****  WARNING  ****
	// ENTERING HARKONNEN ZONE" over the view; the flight goes on or turns
	// back (the original also offers CHANGE DESTINATION, not built here).
	// Returns true to resume.
	if (ConfMan.hasKey("dune_speedrun")) {
		_log.line("Speedrun: turns back from the Harkonnen zone");
		return false; // the bot never flies on into Harkonnen land
	}
	const uint16 resume = _panel.findCommand("RESUME FLIGHT"), back = _panel.findCommand("BACK TO STARTING POINT");
	const uint16 warning = _panel.findCommand("  ****  WARNING", true);
	if (resume == 0xffff)
		return true;
	RowAction actions[Panel::kCommandRows] = { kRowNone, kRowNone };
	int arguments[Panel::kCommandRows] = { 0, 0 };
	uint16 commands[Panel::kCommandRows] = { resume, back };
	setRows(actions, arguments, commands, back != 0xffff ? 2 : 1);
	if (warning != 0xffff) {
		Common::Array<Common::String> lines;
		Common::String text = _panel.commandString(warning), line;
		for (uint i = 0; i <= text.size(); ++i) {
			if (i == text.size() || text[i] == '\r') {
				lines.push_back(line);
				line.clear();
			} else {
				line += text[i];
			}
		}
		drawInfoBox(lines);
	}
	const bool exits[4] = { false, false, false, false };
	_panel.draw(_surface, exits, -1, -1, day());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
	_log.line("Flight: entering the Harkonnen zone");
	dumpScreen(_system, "flight-hostile");
	for (;;) {
		Common::Event event;
		while (pollDuneEvent(_system, event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				_quitRequested = true;
				return true;
			}
			if (event.type != Common::EVENT_LBUTTONDOWN)
				continue;
			int row, arrow;
			if (_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) == Panel::kActionCommand && row >= 0 &&
					row < (back != 0xffff ? 2 : 1))
				return row == 0;
		}
		_system->delayMillis(10);
	}
}

bool GameScreen::arrivalIsFatal(uint place) {
	// location_related_to_dying_if_arriving_at_fortress (seg000:503c): at a
	// place in battle, or not the Atreides', Fremen attacking it start the
	// night battle (not built: Paul arrives); otherwise any Harkonnen troop
	// there means room screen 4, "he was immediately shot".
	if (place >= _world.locationCount() || skipsRules())
		return false;
	const Location l = _world.location(place);
	if (!(l.status & 2) && _world.friendlyPlace(place))
		return false;
	uint harkonnen = 0, attacking = 0;
	_world.countHostiles(place, harkonnen, attacking);
	if (attacking || (l.status & 2)) {
		_battle = true;
		_state.setB(0x2b, 1); // 5058
		_log.line(Common::String::format("Arrival: place %u is in battle, Paul joins it", place));
		return false;
	}
	if (!harkonnen)
		return false;
	_log.line(Common::String::format("Arrival: %u Harkonnen troop(s) at place %u, Paul is shot", harkonnen, place));
	_endingText = "You know what?";
	emperorEnding();
	return true;
}

void GameScreen::drawInfoBox(const Common::Array<Common::String> &lines) {
	// The map's box (COMMAND "DUNE  MAP"): light fill, dark frame and text.
	const Common::Rect box(6, 6, 216, 12 + 11 * (int)MIN<uint>(lines.size(), 5));
	_surface.fillRect(box, Panel::kLightColour);
	_surface.frameRect(box, Panel::kDarkColour);
	for (uint i = 0; i < lines.size() && i < 5; ++i)
		_panel.drawText(_surface, lines[i].c_str(), 12, 9 + 11 * (int)i, Panel::kDarkColour, false);
}

void GameScreen::drawResults() {
	// The SEE RESULTS overlay as the executable lays it out (UI records at
	// ds:2482, gauges at ds:24ee, seg000:bed7): the day and charisma on top,
	// then for each pair Harkonnen (colour 0x3f) and Atreides (0x25) - the
	// controlled areas, the spice production (x10 kg) and the number of men -
	// as figures and ICONES gauge bars (0x37 / 0x38, cap 0x39, height <= 30).
	uint harkonnenArea = 0, atreidesArea = 0, cells = 0;
	if (ensureSaves()) {
		const Common::Array<byte> &map = _saves->map();
		// seg000:bfe3: (cell & 0x30) == 0x30 Harkonnen, any other stage Atreides.
		for (uint i = 0; i + 0x187 < map.size(); ++i, ++cells) {
			const byte stage = map[i] & 0x30;
			if (stage == 0x30)
				++harkonnenArea;
			else if (stage)
				++atreidesArea;
		}
	}
	const uint areaH = cells ? (harkonnenArea * 100 + cells / 2) / cells : 0;
	const uint areaA = cells ? (atreidesArea * 100 + cells / 2) / cells + 1 : 0;
	// Men: ds:ac the Harkonnen troops, ds:aa the rallied ones (bytes = men / 10).
	uint menH = 0, menA = 0;
	for (uint id = 1; id <= World::kTroops; ++id) {
		const Troop t = _world.troop(id);
		if (!t.id)
			continue;
		if (t.harkonnen())
			menH += t.population / 10;
		else if (!(t.occupation & 0xa0))
			menA += t.population / 10;
	}
	const uint spiceH = _state.w(0xa8), spiceA = _state.w(0xa6);

	const uint dayNumber = _world.day();
	const char *suffix = (dayNumber % 10 == 1 && dayNumber % 100 != 11) ? "st" :
						 (dayNumber % 10 == 2 && dayNumber % 100 != 12) ? "nd" :
						 (dayNumber % 10 == 3 && dayNumber % 100 != 13) ? "rd" : "th";
	struct Text {
		int x, y;
		byte colour;
		Common::String text;
	};
	const byte kHarkonnen = 0x3f, kAtreides = 0x25, kTitle = 0xfd, kLabel = 0xfb;
	const Text texts[] = {
		{ 16, 6, kTitle, Common::String::format("%u%s day on DUNE", dayNumber, suffix) },
		// Half the charisma byte (floppy CS:BD06-BD0B: mov al,[29h]; shr ax,1;
		// the originals show 42 for ds:29 = 84, captures/globe-results).
		{ 216, 6, kTitle, Common::String::format("CHARISMA = %u", _state.b(World::kCharisma) / 2u) },
		{ 20, 69, kHarkonnen, Common::String::format("%3u%%", areaH) },
		{ 48, 69, kAtreides, Common::String::format("%3u%%", areaA) },
		{ 8, 80, kLabel, _panel.commandString(_panel.findCommand("CONTROLLED AREAS")) },
		{ 240, 60, kHarkonnen, Common::String::format("%5u0", spiceH) },
		{ 272, 60, kAtreides, Common::String::format("%5u0", spiceA) },
		{ 236, 71, kLabel, _panel.commandString(_panel.findCommand("SPICE PRODUCTION")) },
		{ 240, 131, kHarkonnen, Common::String::format("%5u0", menH) },
		{ 272, 131, kAtreides, Common::String::format("%5u0", menA) },
		{ 236, 142, kLabel, _panel.commandString(_panel.findCommand("NUMBER OF MEN", true)) },
		{ 35, 125, kAtreides, _panel.commandString(_panel.findCommand("ATREIDES")) },
		{ 35, 139, kHarkonnen, _panel.findCommand("HARKONNENS") != 0xffff ? _panel.commandString(_panel.findCommand("HARKONNENS"))
																	  : Common::String("HARKONNENS") }
	};
	for (uint i = 0; i < ARRAYSIZE(texts); ++i)
		if (!texts[i].text.empty())
			_panel.drawText(_surface, texts[i].text.c_str(), texts[i].x, texts[i].y, texts[i].colour, true);
	const int anchors[6][2] = { { 26, 62 }, { 54, 62 }, { 252, 54 }, { 280, 54 }, { 252, 125 }, { 280, 125 } };
	const uint targets[6] = { areaH / 2 + 1, areaA / 2 + 1, (spiceH >> 4) + 1, (spiceA >> 4) + 1, (menH >> 8) + 1,
							  (menA >> 8) + 1 };
	for (uint g = 0; g < 6; ++g) {
		const int height = (int)MIN<uint>(targets[g], 30);
		for (int v = 1; v <= height; ++v)
			_panel.drawIcon(_surface, (uint16)((g & 1) ? 0x38 : 0x37), anchors[g][0], anchors[g][1] - v);
		_panel.drawIcon(_surface, 0x39, anchors[g][0], anchors[g][1] - height - 10);
	}
}

void GameScreen::openTroop(uint troopId, bool fromMap) {
	_troopEquipment = false;
	_troopId = troopId;
	_troopFromMap = fromMap;
	_troopChoosing = false;
	_mode = kTroop;
	_log.line(Common::String::format("Troops: orders for troop %u", troopId));
	if (fromMap) {
		// map_setup_troop_contact_popup: the chief's contact lines (DIALOGUE
		// character 15, list 2) over the troop's staged figures.
		loadDialogue();
		stageTroopForConditions(troopId);
		_conversation->start(World::kFremenChief, 2, 0x80, true);
		nextTroopLine();
	}
	drawTroop();
	dumpScreen(_system, "troop");
}

void GameScreen::stageTroopForConditions(uint troopId) {
	// troop_prepare_troop_data_for_condit, seg000:31f6: the troop's figures
	// at ds:2c-4b and the name-table words the lines' placeholders read
	// (0x81/0x82 the place, 0x84 the job, 0x85 how long, 0x86-0x88 the skills).
	const Troop t = _world.troop(troopId);
	const byte *r = _state.vars + World::kTroopTable + (troopId - 1) * World::kTroopSize;
	_state.setW(0x2c, t.location);
	_state.setB(0x2e, (byte)t.id);
	_state.setB(0x30, t.occupation);
	_state.setB(0x2f, t.occupation & 0x0f);
	_state.setW(0x32, READ_LE_UINT16(r + 0x10));
	_state.setW(0x34, READ_LE_UINT16(r + 0x12));
	_state.setB(0x31, r[0x12] & 0x0f);
	_state.setB(0x36, (byte)_world.motivationModifier(troopId));
	_state.setB(0x37, r[0x16 + MIN<uint>(2, (t.occupation & 0x0c) >> 2)]);
	_state.setB(0x38, t.spiceSkill);
	_state.setB(0x39, t.armySkill);
	_state.setB(0x3a, t.ecologySkill);
	_state.setB(0x3b, t.equipment);
	_state.setB(0x3c, (byte)(t.population / 10));
	_state.setB(0x40, (byte)((_state.w(GameState::kGameTime) >> 4) - r[0x14]));
	_state.setW(0x44, READ_LE_UINT16(r + 0x0c));
	_state.setW(0x46, READ_LE_UINT16(r + 0x0e));
	_state.setW(0x48, (uint16)_world.harvestRate(troopId));
	const uint16 names = _state.nameTable;
	const int index = (int)(t.location - Location::kTableOffset) / Location::kRecordSize;
	if (index >= 0 && (uint)index < _world.locationCount())
		_world.stageLocationForConditions((uint)index); // 31f6 ends with the troop's place
	if (index >= 0 && (uint)index < _world.locationCount()) {
		const Location l = _world.location((uint)index);
		_state.setW(names + 2, l.firstName);
		_state.setW(names + 4, (uint16)(12 + l.lastName));
	}
	// The CD's ids are 0x18 + job, 112-116 and 0xd1 + rank; the floppy
	// numbers its COMMAND records differently, so the bases are found by text
	// (1-based ids, as the name table holds them).
	const uint16 jobs = (uint16)(_panel.findCommand("Spice Mining") + 1);
	const uint16 durations = (uint16)(_panel.findCommand("for a very short time") + 1);
	const uint16 ranks = (uint16)(_panel.findCommand("On trial") + 1);
	_state.setW(names + 8, (uint16)(jobs + (t.occupation & 0x0f)));
	// 0x83 (subst_id_03, 326e): the item a searching troop wants, COMMAND
	// 0xe8 + troop +0e's low byte on the CD ("a spice-harvester" .. "some
	// bulbs"; 0xdc on the floppy).
	const uint16 items = (uint16)(_panel.findCommand("a spice-harvester") + 1);
	_state.setW(names + 6, (uint16)(items + MIN<uint>(6, r[0x0e])));
	// The duration phrase (sub_132c7; its thresholds are not transcribed):
	// "for a very short time" ... "for 12 days", or "but our job is finished".
	const uint periods = (uint16)(_state.w(GameState::kGameTime) - READ_LE_UINT16(r + 0x0a));
	const uint step = (t.occupation & Troop::kStopped) ? 4 : periods < 4 ? 0 : periods < 16 ? 1 : periods < 96 ? 2 : 3;
	_state.setW(names + 10, (uint16)(durations + step));
	_state.setW(names + 12, (uint16)(ranks + MIN<uint>(5, t.spiceSkill >> 4)));
	_state.setW(names + 14, (uint16)(ranks + MIN<uint>(5, t.armySkill >> 4)));
	_state.setW(names + 16, (uint16)(ranks + MIN<uint>(5, t.ecologySkill >> 4)));
}

void GameScreen::troopSceneStep() {
	// The scripted scene over the troop popup (CD 12f8 on the prospectors'
	// SPECIALIZE IN SPICE): action 07 raises the spice-density overlay and
	// speaks the next line of character 15's list 7 ("Here, take this map of
	// the planet."), action 08 drops it and speaks the next ("You can update
	// this map..."); the end gives the order menu back.
	Common::Array<byte> bytes;
	if (!_troopScene || !_world.sceneScript(_troopScene, bytes) || _troopSceneCursor >= bytes.size() ||
			bytes[_troopSceneCursor] == 0xff) {
		_troopScene = 0;
		if (_map)
			_map->setDensity(false);
		drawTroop();
		return;
	}
	const byte op = bytes[_troopSceneCursor++];
	if (op == 0x0e || op == 0x10) {
		if (_map)
			_map->setDensity(op == 0x0e);
		stageTroopForConditions(_troopId);
		_conversation->start(World::kFremenChief, 7, 0x80, true, true);
		Common::String page;
		bool newSentence;
		if (_conversation->next(page, newSentence))
			_troopLine = page;
		_log.line(Common::String::format("Scene: troop lesson action %#x", op));
	} else {
		_log.line(Common::String::format("Scene: troop lesson action %#x not built", op));
	}
	drawTroop();
	dumpScreen(_system, Common::String::format("troop-lesson-%u", _troopSceneCursor).c_str());
}

void GameScreen::startTroopPick() {
	// seg000:8064 (floppy 8d7a): the caption (COMMAND 0x4a, "Show me where
	// you want me to go...", or 0x4b for the prospectors, who copy their
	// queue to the working one), the density popup, and the pick menu.
	_troopChoosing = false;
	_troopPicking = true;
	_pickLineBefore = _troopLine;
	_pickCount = 0;
	_pickQueue[0] = _pickQueue[1] = _pickQueue[2] = 0;
	const uint16 show = _panel.findCommand("Show me where", true);
	uint16 caption = show;
	if (_troopId == World::kProspectorTroop) {
		// The count is the first empty slot of the four words (repne scasw), at most 3.
		for (uint k = 0; k < 3 && _world.prospectorDestination(k); ++k)
			_pickQueue[_pickCount++] = _world.prospectorDestination(k);
		if (show != 0xffff)
			caption = (uint16)(show + 1);
	}
	_troopLine = caption != 0xffff ? _panel.commandString(caption) : Common::String();
	_log.line(Common::String::format("Troops: troop %u picks a destination (command %#x \"%s\", %u queued)", _troopId,
			caption, _troopLine.c_str(), _pickCount));
	if (_map)
		_map->setDensity(true);
	drawTroop();
	dumpScreen(_system, "troop-pick");
}

void GameScreen::troopPickTap(int x, int y) {
	// mouse_handler_move_troop_pick (seg000:81ec): the nearest marker within
	// 9 pixels on the popup's window; move_troop_validate_pick (8256).
	if (!_map)
		return;
	const int hit = _map->densityHit(x, y);
	if (hit < 0)
		return;
	if (_troopId != World::kProspectorTroop) {
		endTroopPick(hit);
		return;
	}
	// The prospectors take a sietch or an Atreides-held place (status bit 3).
	const Location l = _world.location((uint)hit);
	if (!l.isSietch() && !(l.status & 0x08)) {
		_log.line(Common::String::format("Troops: the prospectors cannot prospect place %d", hit));
		return;
	}
	if (_pickCount >= 3) {
		_pickCount = 0;
		_pickQueue[0] = _pickQueue[1] = _pickQueue[2] = 0;
	}
	_pickQueue[_pickCount++] = World::placeOffset((uint)hit);
	_log.line(Common::String::format("Troops: prospector destination %u = place %d (status %#x)", _pickCount, hit, l.status));
	drawTroop();
	dumpScreen(_system, Common::String::format("troop-pick-%u", _pickCount).c_str());
	if (_pickCount < 3)
		return;
	_system->delayMillis(isFastCapture() ? 0 : 250); // sub_FD63(0x32): a beat before the order
	endTroopPick(-3);
}

void GameScreen::endTroopPick(int dest) {
	// The teardown (move_troop_teardown, 82b7), then for a pick (-3 the
	// prospectors' queue) the done path (seg000:8214): the acknowledgement
	// through list 4 with ds:23 = 0x0b (0x10 when the troop is already
	// there), then troop_issue_move_order and the map's main menu.
	_troopPicking = false;
	if (_map) {
		_map->setDensity(false);
		_map->setRoute(Common::Array<Common::Point>());
	}
	if (dest == -3) {
		for (uint k = 0; k < 3; ++k)
			_world.setProspectorDestination(k, _pickQueue[k]);
		dest = _world.placeIndex(_pickQueue[0]);
		if (dest < 0)
			dest = -1; // an empty head cancels
	}
	if (dest < 0) {
		_troopLine = _pickLineBefore;
		drawTroop();
		return;
	}
	byte *r = _state.vars + World::kTroopTable + (_troopId - 1) * World::kTroopSize;
	const uint16 before = READ_LE_UINT16(r + 4);
	const bool same = _world.troopPlace(_troopId) == dest;
	if (!same)
		WRITE_LE_UINT16(r + 4, World::placeOffset((uint)dest));
	stageTroopForConditions(_troopId);
	WRITE_LE_UINT16(r + 4, before);
	_state.setB(0x23, same ? 0x10 : 0x0b);
	_conversation->start(World::kFremenChief, 4, 0x20, true, true); // 7bbe -> 96f1 -> 9f8b: sentence mask 0x20
	_conversation->armGate();
	Common::String page;
	bool newSentence;
	if (_conversation->next(page, newSentence))
		_troopLine = page;
	if (!_conversation->gateHeld()) {
		// Event 2 drops the gate: no march now. The prospectors keep their
		// queue (already stored) and leave once this place is prospected.
		_log.line(Common::String::format("Troops: troop %u stays for now (\"%s\")", _troopId, _troopLine.c_str()));
		drawTroop();
		return;
	}
	if (_world.issueMoveOrder(_troopId, (uint)dest))
		_log.line(Common::String::format("Troops: troop %u answers \"%s\"", _troopId, _troopLine.c_str()));
	drawTroop();
	dumpScreen(_system, "troop-moving");
	_mode = kMap;
	drawMapScreen();
}

bool GameScreen::troopReaction(byte action) {
	// troop_present_reaction_line (CD 7bb9, floppy 88d4): the troop's
	// figures staged (31f6), ds:23 = the reaction, one line of the chief's
	// list 4 into the popup. The gate (a1c4/a1e2) tells whether the line's
	// event refused the order.
	if (!loadDialogue())
		return true;
	stageTroopForConditions(_troopId);
	_state.setB(0x23, action);
	_conversation->start(World::kFremenChief, 4, 0x20, true, true); // 7bbe -> 96f1 -> 9f8b: sentence mask 0x20
	_conversation->armGate();
	Common::String page;
	bool newSentence;
	if (_conversation->next(page, newSentence))
		_troopLine = page;
	const bool held = _conversation->gateHeld();
	_log.line(Common::String::format("Troops: troop %u answers (ds:23 = %#x) \"%s\"%s", _troopId, action, _troopLine.c_str(),
			held ? "" : " (refuses)"));
	return held;
}

void GameScreen::searchForEquipment() {
	// GO & SEARCH FOR EQUIPMENT (CD 7734 army, 775c ecology, 776d spice;
	// floppy 8498, 84c0, 84d1): the item the class lacks; all of them, the
	// answer 0x0f "I have all the equipment I need!". The item's name goes
	// to subst_id_0c (ds:1203, floppy 1210: placeholder 0x8c).
	_troopChoosing = false;
	const int item = _world.searchedEquipment(_troopId);
	_log.line(Common::String::format("Troops: troop %u searches for equipment: item %d", _troopId, item));
	if (item < 0) {
		troopReaction(0x0f);
		drawTroop();
		dumpScreen(_system, "troop-search");
		return;
	}
	const uint16 items = (uint16)(_panel.findCommand("a spice-harvester") + 1);
	_state.setW(_state.nameTable + 24, (uint16)(items + (uint)item));
	// CD 7789 -> 77d7 -> 7d81: the item free at the troop's own place is
	// taken there, with MODIFY EQUIPMENT's answer (ds:23 = 0x0c). The floppy
	// (84ed) goes straight to the search.
	if (!_world.floppy() && _world.searchEquipmentHere(_troopId, (uint)item)) {
		troopReaction(0x0c);
		drawTroop();
		dumpScreen(_system, "troop-search");
		return;
	}
	// 7f90 (floppy 8ca1): the nearest known place with one free; none, the
	// answer 0x0e "I don't think I can find ... available in all of the
	// places around here."
	uint distance = 0;
	const int target = _world.equipmentSearchTarget(_troopId, (uint)item, &distance);
	if (target < 0) {
		_log.line(Common::String::format("Troops: troop %u finds no place with item %d within reach", _troopId, item));
		troopReaction(0x0e);
		drawTroop();
		dumpScreen(_system, "troop-search");
		return;
	}
	_log.line(Common::String::format("Troops: troop %u picks place %d for item %d (distance %u)", _troopId, target, item, distance));
	// 6a33 -> troop_apply_occupation_choice (6a89): occupation (class) | 3
	// and the answer 0x0a; an event there refuses and the job is taken back.
	const byte before = _world.troop(_troopId).occupation;
	byte record[World::kTroopSize];
	_world.saveTroopRecord(_troopId, record);
	const byte job = (byte)((before & 0x0c) | 3);
	if ((before & 0x0f) != job) {
		_world.setTroopOccupation(_troopId, job);
		if (!troopReaction(0x0a)) {
			_world.restoreTroopRecord(_troopId, record);
			_log.line(Common::String::format("Troops: troop %u refuses the search", _troopId));
			drawTroop();
			dumpScreen(_system, "troop-search");
			return;
		}
		// 6ab8-6abf: a troop leaving the spice class leaves its harvester.
		if (job & 0x0c)
			_state.vars[World::kTroopTable + (_troopId - 1) * World::kTroopSize + 0x19] &= 0x7f;
	}
	// 77b4-77c5: the march (84a6), then NO MORE ORDERS (8770): the map.
	const int from = _world.troopPlace(_troopId);
	_world.startEquipmentSearch(_troopId, (uint)item, (uint)target);
	if (from >= 0) {
		Common::Array<uint> left;
		_world.troopsAt((uint)from, left);
		_log.line(Common::String::format("Troops: %u troop(s) left at place %d, hired troop where Paul is: %s", left.size(), from,
				hiredTroopAt(_world.currentLocation()) ? "yes" : "no"));
	}
	drawTroop();
	dumpScreen(_system, "troop-search");
	if (_troopFromMap) {
		_mode = kMap;
		drawMapScreen();
	}
}

bool GameScreen::nextTroopLine() {
	Common::String page;
	bool newSentence;
	if (!_conversation->next(page, newSentence)) {
		// ASK FOR MORE INFORMATION starts the list over.
		stageTroopForConditions(_troopId);
		_conversation->start(World::kFremenChief, 2, 0x80, true);
		if (!_conversation->next(page, newSentence))
			return false;
	}
	_troopLine = page;
	return true;
}
void GameScreen::drawTroop() {
	const Troop t = _world.troop(_troopId);
	RowAction actions[Panel::kCommandRows];
	int arguments[Panel::kCommandRows];
	uint16 commands[Panel::kCommandRows];
	bool disabled[Panel::kCommandRows] = { false, false, false, false, false };
	uint count = 0;
	auto add = [&](RowAction action, int argument, const char *text, bool greyed = false, bool prefix = false) {
		const uint16 id = _panel.findCommand(text, prefix);
		if (count < Panel::kCommandRows && id != 0xffff) {
			actions[count] = action;
			arguments[count] = argument;
			commands[count] = id;
			disabled[count] = greyed;
			++count;
		}
	};
	const uint job = t.occupation & 0x0f;
	(void)job;
	if (_troopFromMap && _map) {
		// sub_A5E5: over a troop's popup the density popup is at (0x5c, 0x1e)
		// with the troop's marker (sub_8F62: its place, or its position on
		// the march) and its route (sub_AD0A: from its position through its
		// destinations).
		Common::Array<Common::Point> route;
		const byte *r = _state.vars + World::kTroopTable + (_troopId - 1) * World::kTroopSize;
		const Common::Point position((int16)READ_LE_UINT16(r + 6), (int16)READ_LE_UINT16(r + 8));
		Common::Point marker = position;
		if (!(r[3] & 0x40) && _world.troopPlace(_troopId) >= 0) {
			const Location l = _world.location((uint)_world.troopPlace(_troopId));
			marker = Common::Point((int16)l.longitude, l.latitude);
		}
		_map->setDensityForTroop(0x5c, 0x1e, marker);
		{
			route.push_back(position);
			if (_troopId == World::kProspectorTroop) {
				// The prospectors' line runs through the working queue (ds:4274),
				// which outside a pick holds the stored one.
				uint16 queue[3];
				uint count = 0;
				for (uint k = 0; k < 3; ++k) {
					queue[k] = _troopPicking ? _pickQueue[k] : _world.prospectorDestination(k);
					if (queue[k] && count == k)
						++count;
				}
				for (uint k = 0; k < count; ++k) {
					const int p = _world.placeIndex(queue[k]);
					if (p >= 0)
						route.push_back(Common::Point((int16)_world.location((uint)p).longitude, _world.location((uint)p).latitude));
				}
			} else if (_world.troopPlace(_troopId) >= 0) {
				const Location l = _world.location((uint)_world.troopPlace(_troopId));
				route.push_back(Common::Point((int16)l.longitude, l.latitude));
			}
		}
		_map->setRoute(route);
		// The density popup goes over the troop panel (sub_ACC0 draws the
		// panel, then sub_80B0 the popup), so the map is drawn without it.
		const bool overlay = _map->density();
		_map->setDensity(false);
		_map->draw(_surface, _panel, _sentences, _state.b(GameState::kFremenTroops));
		_map->setDensity(overlay);
		// map_draw_troop_contact_popup (seg000:79ee): the panel at the top
		// (the troop's icon is below it), a 61x61 head box (fill 0xe4, frame
		// 0xf5) at +(4,3) and the line at +(0x49,3) in a 153x63 area, all on
		// the popup fill 0xfb.
		const Common::Rect panel(6, 5, 6 + 0x49 + 153 + 4, 5 + 0x43);
		_surface.fillRect(panel, 0xfb);
		const Common::Rect head(panel.left + 4, panel.top + 3, panel.left + 4 + 0x3d, panel.top + 3 + 0x3d);
		_surface.fillRect(head, 0xe4);
		_surface.frameRect(head, 0xf5);
		Common::Array<byte> sheet;
		static const char *const kHeads[3] = { "FRM1.HSQ", "FRM2.HSQ", "FRM3.HSQ" };
		// talking_head_popup_anchor_table (ds:22b9): the head's point that lands on the box origin.
		static const int16 kAnchors[3][2] = { { 0x48, 0x3d }, { 0x54, 0x1d }, { 0x38, 0x1a } };
		const uint headIndex = World::fremenHead(_troopId);
		if (_resources.load(kHeads[headIndex], sheet)) {
			// The head: the portrait drawn offscreen, the 59x59 square from its
			// anchor (talking_head_popup_anchor_table, ds:22b9).
			Sprite portrait(_system, sheet);
			portrait.setPalette();
			Graphics::ManagedSurface off;
			off.create(320, 152, Graphics::PixelFormat::createFormatCLUT8());
			off.fillRect(Common::Rect(0, 0, 320, 152), 0xe4);
			if (!portrait.drawAnimationFrame(World::fremenExpression(_troopId), 0, off.surfacePtr(), 0, 0) &&
					!portrait.drawAnimationFrame(0, 0, off.surfacePtr(), 0, 0))
				portrait.drawFrame(0, off.surfacePtr(), 0, 0);
			// loc_09d94 subtracts the anchor: the portrait from the anchor point on shows in the box.
			const int ax = CLIP<int>(kAnchors[headIndex][0], 0, 320 - 59), ay = CLIP<int>(kAnchors[headIndex][1], 0, 152 - 59);
			_surface.blitFrom(off, Common::Rect(ax, ay, ax + 59, ay + 59), Common::Point(head.left + 1, head.top + 1));
			off.free();
			const byte black[3] = { 0, 0, 0 };
			_system->getPaletteManager()->setPalette(black, 0, 1);
		}
		if (_troopEquipment) {
			drawEquipmentPanel(Common::Rect(panel.left + 0x49, panel.top + 3, panel.left + 0x49 + 153, panel.top + 3 + 63));
			RowAction a[1] = { kRowEquipDone };
			int g[1] = { 0 };
			uint16 c[1] = { _panel.findCommand("Done", true) }; // "  Done" on the CD, "Done" on the floppy
			setRows(a, g, c, c[0] != 0xffff ? 1 : 0);
			_panel.setLeftPanel(Panel::kLeftGlobe);
			const bool noExits[4] = { false, false, false, false };
			_panel.draw(_surface, noExits, -1, -1, day());
			if (_map)
				_map->drawPanelExtras(_surface, _panel);
			_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
			_system->updateScreen();
			return;
		}
		Common::Array<Common::String> lines;
		_panel.wrapText(_troopLine, 153 - 8, false, lines);
		const int lineHeight = 10;
		// sub_ACC0: while a move is picked the text area is 0x19 high (two lines).
		const uint maxLines = overlay ? 2 : 6;
		const int area = overlay ? 0x19 : 63;
		int y = panel.top + 3 + (area - (int)MIN<uint>(lines.size(), maxLines) * lineHeight) / 2;
		for (uint i = 0; i < lines.size() && i < maxLines; ++i, y += lineHeight)
			_panel.drawText(_surface, lines[i].c_str(), panel.left + 0x49 + 4, y, Panel::kDarkColour, false);
		if (overlay)
			_map->drawDensityOverlay(_surface, _panel);

		// menu_map_troop_dialog: the contact verbs (greyed when they cannot apply).
		if (_troopPicking) {
			if (_troopId == World::kProspectorTroop) {
				// menu_map_move_prospectors (sub_AC8D): ADD A DESTINATION is
				// greyed unless the working queue holds one or two places.
				add(kRowPickAdd, 0, "ADD A DESTINATION", _pickCount < 1 || _pickCount > 2);
				add(kRowPickNew, 0, "GIVE NEW DESTINATIONS");
				add(kRowPickDone, 0, "Done", false, true);
				add(kRowPickCancel, 0, "Cancel", false, true);
			} else {
				add(kRowPickCancel, 0, "Cancel", false, true); // menu_multiple_cancel
			}
		} else if (_troopScene) {
			// The prospector's lesson (menu_prospector_troop_after_specializing_in_spice): " Continue...".
			add(kRowContinue, 0, " Continue...");
		} else if (!_troopChoosing) {
			add(kRowAskMore, 0, "ASK FOR MORE INFORMATION");
			add(kRowTroopOccupation, 0, job == Troop::kWaitingForOrders ? "SELECT TROOP OCCUPATION" : "CHANGE TROOP OCCUPATION");
			add(kRowEquipment, 0, "MODIFY EQUIPMENT");
			add(kRowMoveTroop, 0, "MOVE TROOP");
			add(kRowTroopDone, 0, "NO MORE ORDERS");
		}
	} else {
		composeView();
	}
	if (_troopChoosing) {
		if (job == Troop::kWaitingForOrders) {
			// seg000:6a71-6a87: a waiting troop first takes a speciality;
			// ecology is greyed until Kynes has been met.
			add(kRowSetOccupation, Troop::kSpiceMining, "SPECIALIZE IN SPICE");
			add(kRowSetOccupation, Troop::kMilitaryTraining, "SPECIALIZE IN ARMY");
			// ECOLOGY needs bitfield_Paul_events bit 5, Kynes met (seg000:69b3).
			add(kRowSetOccupation, Troop::kIrrigation, "SPECIALIZE IN ECOLOGY", !(_state.b(World::kPaulEvents) & 0x20));
		} else {
			// The class menus (seg000:69b3, floppy 774d): GO & SEARCH FOR
			// EQUIPMENT heads all three and is greyed below phase 0x10
			// (69f6-6a02); SPECIALIZE IN ECOLOGY (entry 0x77) is greyed until
			// Kynes has been met, ds:0a bit 5 (6a07-6a23).
			const bool noSearch = _state.b(GameState::kPhase) < 0x10;
			const bool noEcology = !(_state.b(World::kPaulEvents) & 0x20);
			switch (job & 0x0c) {
			case 0:
				// menu_map_troop_change_troop_occupation_for_spice_troop
				// (ds:216e, floppy 27d4): the search (776d), army, ecology.
				add(kRowSearchEquipment, 0, "GO & SEARCH FOR EQUIPMENT", noSearch);
				add(kRowSetOccupation, Troop::kMilitaryTraining, "SPECIALIZE IN ARMY");
				add(kRowSetOccupation, Troop::kIrrigation, "SPECIALIZE IN ECOLOGY", noEcology);
				break;
			case 4: {
				// menu ds:2182 (floppy 27e8) for an army troop; on espionage
				// the troop can attack instead (ds:219a).
				if (job == Troop::kEspionage) {
					add(kRowAttack, 0, "ATTACK");
					break;
				}
				const int here = _world.troopPlace(_troopId);
				uint dist = 0xffff;
				if (here >= 0)
					_world.nearestHiddenHarkonnen((uint)here, dist);
				add(kRowSearchEquipment, 0, "GO & SEARCH FOR EQUIPMENT", noSearch); // 7734
				add(kRowEspionage, 0, "ESPIONAGE", dist >= 0x1e); // ds:e2 < 0x1e (69db)
				add(kRowSetOccupation, Troop::kSpiceMining, "SPECIALIZE IN SPICE");
				add(kRowSetOccupation, Troop::kIrrigation, "SPECIALIZE IN ECOLOGY", noEcology);
				break;
			}
			default:
				// menu_map_troop_change_troop_occupation_for_ecology_troop
				// (ds:21a6, floppy 280c): the search (775c), ASSEMBLY
				// WIND-TRAP (job | 1, seg000:6a2b), or another speciality.
				add(kRowSearchEquipment, 0, "GO & SEARCH FOR EQUIPMENT", noSearch);
				add(kRowSetOccupation, (job & 0x0c) | 1, "ASSEMBLY WIND-TRAP");
				add(kRowSetOccupation, Troop::kSpiceMining, "SPECIALIZE IN SPICE");
				add(kRowSetOccupation, Troop::kMilitaryTraining, "SPECIALIZE IN ARMY");
				break;
			}
		}
		add(kRowTroopDone, 0, "Cancel", false, true);
	} else if (!_troopFromMap) {
		add(kRowTroopOccupation, 0, "CHANGE TROOP OCCUPATION");
		add(kRowTroopDone, 0, "NO MORE ORDERS");
	}
	setRows(actions, arguments, commands, count);
	for (uint i = 0; i < count; ++i)
		_panel.setRowDisabled(i, disabled[i]);
	_panel.setLeftPanel(_troopFromMap ? Panel::kLeftGlobe : Panel::kLeftBook);
	const bool exits[4] = { false, false, false, false };
	_panel.draw(_surface, exits, -1, -1, day());
	if (_troopFromMap && _map)
		_map->drawPanelExtras(_surface, _panel);
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}
void GameScreen::talkThrough(uint character) {
	startConversation(character);
	uint guard = 0;
	while (talking() && guard++ < 40)
		advanceConversation();
	if (inConversation())
		endConversation();
}

void GameScreen::dumpGameplay() {
	// The walkthrough's first steps: the Duke, then Jessica (who names
	// Carthag-Tuek), then Gurney there. The tour arrives from a sietch, so it
	// flies home first: the palace room numbers mean other rooms elsewhere.
	travelTo(0);
	showRoom(kPalaceFirstRoom);
	talkThrough(0);
	showRoom(4);
	talkThrough(1);
	_log.line(Common::String::format("Dump: phase %#x after Leto and Jessica", _state.b(GameState::kPhase)));
	travelTo(12);
	showRoom(2);
	talkThrough(4);
	_log.line(Common::String::format("Dump: phase %#x after Gurney", _state.b(GameState::kPhase)));
	travelTo(12);
	showRoom(2); // the Fremen of troop 1 stand in the first cave
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	_log.line(Common::String::format("Dump: %u person(s) in the cave", people.size()));
	startConversation(World::kFremen);
	while (inConversation() && !_talkEnded)
		advanceConversation();
	// WORK FOR ME: the check, the Fremen's answer, the troop follows.
	workForMe();
	dumpScreen(_system, "troop-recruit");
	while (talking() || _talkRecruit)
		advanceConversation();
	if (inConversation())
		endConversation();
	showRoom(2); // now the chief
	dumpScreen(_system, "sietch-chief");
	const uint id = _world.localTroop(true);
	if (id) {
		startConversation(World::kFremenChief);
		dumpScreen(_system, "chief-talk");
		endConversation();
		openMap(MapScreen::kFlat, false);
		openTroop(id, true); // "troop": the contact popup
		_troopChoosing = true;
		drawTroop();
		dumpScreen(_system, "troop-occupations"); // SPECIALIZE IN ...
		_world.setTroopOccupation(id, Troop::kSpiceMining);
		_troopChoosing = false;
		stageTroopForConditions(id);
		_conversation->start(World::kFremenChief, 2, 0x80, true);
		nextTroopLine();
		drawTroop();
		dumpScreen(_system, "troop-ordered");
		leaveMap();
	}
	passTime(World::kSlotsPerDay); // a day of mining
	if (id) {
		openMap(MapScreen::kFlat, false);
		openTroop(id, true);
		dumpScreen(_system, "troop-after-a-day");
		leaveMap();
	}
	openMap(MapScreen::kFlat, false);
	_map->setDensity(true);
	drawMapScreen();
	dumpScreen(_system, "map-density");
	openMap(MapScreen::kGlobe, false);
	_map->setResults(100);
	drawMapScreen();
	dumpScreen(_system, "results");
	_map->setResults(0);
	leaveMap();
	// Evening at the palace balcony: the sky follows the clock.
	travelTo(0);
	passTime((World::kSlotsPerDay + 11 - _world.timeSlot()) % World::kSlotsPerDay);
	showRoom(5);
	dumpScreen(_system, "evening-balcony");
}
// ---- The game menu: saving, loading, options -----------------------------------

void GameScreen::openMenu(Menu menu) {
	// Floppy CS:b171/b183 push DS:26e4/26f4 over the current screen.
	// Mirror DS:2728 uses those same handlers; CS:cd2b pops back to it.
	if (_mode != kMirror && (_mode != kMap || !_map || _map->mode() != MapScreen::kGlobe)) {
		openMap(MapScreen::kGlobe, false);
		if (_mode != kMap)
			return;
	}
	_menu = menu;
	_menuStatus = 0xffff;
	if (menu == kMenuSave || menu == kMenuLoad)
		ensureSaves();
	static const char *const kNames[] = { "menu-globe", "menu-save", "menu-load", "menu-options", "menu-quit", "menu-music-order" };
	const char *name = _mode == kMirror && menu == kMenuNone ? "menu-mirror" : kNames[menu];
	_log.line(Common::String::format("Menu: %s", name));
	if (_mode == kMirror)
		drawMirror();
	else
		drawMapScreen();
	dumpScreen(_system, name);
}

void GameScreen::setSaveMenuRows() {
	// Floppy DS:26e4/26f4: two manual logs for Save, four entries for Load.
	static const char *const kSlots[SaveGame::kSlots] = {
		"Log 1:", "Log 2:", "LAST ENTERING INTO A PLACE", "LAST ENTERING NEW SIETCH"
	};
	RowAction actions[Panel::kCommandRows];
	int arguments[Panel::kCommandRows];
	uint16 commands[Panel::kCommandRows];
	const uint slotCount = _menu == kMenuSave ? 2 : SaveGame::kSlots;
	for (uint slot = 0; slot < slotCount; ++slot) {
		actions[slot] = _menu == kMenuSave ? kRowSaveSlot : kRowLoadSlot;
		arguments[slot] = slot;
		commands[slot] = _panel.findCommand(kSlots[slot], true);
	}
	actions[slotCount] = kRowMenuBack;
	arguments[slotCount] = 0;
	commands[slotCount] = _panel.findCommand("Cancel", true);
	setRows(actions, arguments, commands, slotCount + 1);
	for (uint slot = 0; slot < 2; ++slot)
		_panel.setRowText(slot, slotLabel(slot));
	if (_menu == kMenuLoad)
		for (uint slot = 0; slot < slotCount; ++slot)
			_panel.setRowDisabled(slot, !_saves || _saves->slotTime(slot) < 0); // CS:b1f4: missing logs have flag 0x4000.
	if (_menu == kMenuSave && _menuStatus != 0xffff)
		// CS:b251-b259 draws the status in row CX=4; Cancel stays in row 2.
		_panel.setRowText(4, _panel.commandString(_menuStatus));
}

bool GameScreen::ensureSaves() {
	if (_saves)
		return true;
	if (!loadDialogue())
		return false;
	_saves = new SaveGame(_world, *_dialogue, _resources, _log);
	return true;
}

Common::String GameScreen::slotLabel(uint slot) const {
	// "Log 1: DAY  0 / 12.00 a.m.": the clock counts 24 units a day, the
	// first day starting at midnight (the original's own reading of the
	// time word is not decoded; see FINDINGS.md).
	const int time = _saves ? _saves->slotTime(slot) : -1;
	if (time < 0)
		return Common::String::format("Log %u: (empty)", slot + 1);
	const uint hour = (uint)time % 24;
	return Common::String::format("Log %u: DAY %2u / %2u.00 %s", slot + 1, (uint)time / 24,
			hour % 12 ? hour % 12 : 12, hour < 12 ? "a.m." : "p.m.");
}

bool GameScreen::saveSlot(uint slot) {
	if (!ensureSaves() || slot >= SaveGame::kSlots)
		return false;
	const bool ok = _saves->save(slot);
	_menu = kMenuSave;
	_menuStatus = _panel.findCommand(ok ? "SAVE SUCCESSFUL" : "*** SAVE ERROR", true);
	if (_mode == kMap)
		drawMapScreen();
	else if (_mode == kMirror)
		drawMirror();
	dumpScreen(_system, ok ? "menu-saved" : "menu-save-error");
	if (ok && (_mode == kMap || _mode == kMirror)) {
		// CS:b25c-b268 waits 0x12c timer ticks, then pops only on success.
		if (!isFastCapture())
			_system->delayMillis(1500);
		openMenu(kMenuNone);
	}
	return ok;
}

bool GameScreen::loadSlot(uint slot) {
	if (!ensureSaves() || slot >= SaveGame::kSlots)
		return false;
	// CS:b2aa/b2da preserves DS:00fb: a globe load stays on the globe;
	// the room/mirror branch (CS:b2f5) rebuilds the room.
	const bool fromGlobe = _menu == kMenuLoad && _mode == kMap && _map && _map->mode() == MapScreen::kGlobe;
	if (!_saves->load(slot)) {
		_menuStatus = _panel.findCommand("*** SAVE ERROR", true);
		if (_mode == kMap)
			drawMapScreen();
		else if (_mode == kMirror)
			drawMirror();
		return false;
	}
	// The book's journal is not in the file: it lists the lines said so
	// far that carry a topic (by character here, not in the order heard).
	_state.notebook.clear();
	for (uint c = 0; c < Dialogue::kCharacters; ++c)
		for (uint list = 0; list < Dialogue::kListsPerCharacter; ++list) {
			uint offset = _dialogue->listOffset(c, list);
			Dialogue::Entry entry;
			while (_dialogue->entryAt(offset, entry)) {
				if (entry.said() && ((entry.flags2 >> 2) & 0x0f))
					_state.notebook.push_back((uint16)((c << 11) | (entry.offset / 4)));
				offset += 4;
			}
		}
	if (_conversation)
		_conversation->stop();
	_battle = _state.b(0x2b) != 0; // Paul in a battle (ds:2b) is saved with the data segment
	_mode = kRoom;
	leaveMap();
	if (fromGlobe) {
		// CD menu_callback_choice_globe_load_game (seg000:b3b0, loc_1B412;
		// floppy CS:B2E2-B2EC): SEE RESULTS opens by
		// itself, then the globe centres on Paul (ba9e), tilt within +-98.
		openMenu(kMenuNone);
		if (_mode == kMap && _map) {
			_map->setResults(100);
			_map->centreOnPlayer();
			drawMapScreen();
		}
	}
	return true;
}

void GameScreen::toggleMusic() {
	_musicOn = !_musicOn;
	if (_music) {
		if (_musicOn) {
			Common::Array<byte> song;
			if (_resources.load("ARRAKIS.HSQ", song))
				_music->play(song);
		} else {
			_music->stop();
		}
	}
	_log.line(Common::String::format("Options: music %s", _musicOn ? "on" : "off"));
}

// ---- Conversations ----------------------------------------------------------

// Sprite sheets by dialogue character number: the DIALOGUE.HSQ group order,
// identified from the lines themselves (0 Leto, 1 Jessica, 2 Thufir,
// 3 Duncan, 4 Gurney, 5 Stilgar, 6 Liet, 7 Chani, 8 Harah, 9 the Baron,
// 10 Feyd, 11 the Emperor, 12 a Harkonnen prisoner, 13 the smuggler,
// 14-15 Fremen, 16 messages). Which Fremen face goes with which sietch is
// not known yet.
static const char *const kCharacterSheets[Dialogue::kCharacters] = {
	"LETO.HSQ", "JESS.HSQ", "HAWA.HSQ", "IDAH.HSQ", "GURN.HSQ", "STIL.HSQ", "KYNE.HSQ", "CHAN.HSQ",
	"HARA.HSQ", "BARO.HSQ", "FEYD.HSQ", "EMPR.HSQ", "HARK.HSQ", "SMUG.HSQ", "FRM1.HSQ", "FRM2.HSQ", nullptr
};

bool GameScreen::loadDialogue() {
	if (_conversation)
		return true;
	_sentences = new SentenceBank(_resources, _log);
	_dialogue = new Dialogue();
	_conditions = new Conditions();
	const bool ok = _sentences->load() && _dialogue->load(_resources, _log) && _conditions->load(_resources, _log);
	if (!ok) {
		delete _sentences;
		delete _dialogue;
		delete _conditions;
		_sentences = nullptr;
		_dialogue = nullptr;
		_conditions = nullptr;
		return false;
	}
	_conversation = new Conversation(*_sentences, *_dialogue, *_conditions, _state, _log);
	_conversation->setEventHandler(&GameScreen::storyEvent, this);
	return true;
}

void GameScreen::workForMe() {
	// seg000:95c1: the charisma check decides; the Fremen answer with their
	// topic-5 line, and a pass rallies the troop.
	const uint troop = _world.localTroop(false);
	if (!troop)
		return;
	_talkRecruitOk = _world.troopAgreesToFollow(troop);
	_state.setB(0x23, _talkRecruitOk ? 0 : 2); // pending_room_action: the check's outcome for the conditions
	_talkRecruit = troop;
	_log.line(Common::String::format("Troops: WORK FOR ME to troop %u: %s", troop, _talkRecruitOk ? "yes" : "no"));
	presentVerb(5);
	// Floppy CS:A0A8 presents the answer, A0B4 checks its gate, and A0DD
	// rallies the troop before returning to input. A10C installs the chief's
	// verbs while the same speaker and acceptance balloon remain visible.
	_talkRecruit = 0;
	if (!_conversation->gateHeld()) {
		_log.line(Common::String::format("Troops: troop %u refused", troop));
		return;
	}
	_world.rallyTroop(troop);
	for (uint who = World::kFremenChief; who < World::kFremenChief + 8; ++who) {
		if (_world.troopForPerson(who) != troop)
			continue;
		_talkWho = who; // menu identity; keep the current portrait and answer
		updateRoomVars();
		drawTalk();
		break;
	}
}

void GameScreen::companionVerb() {
	// COME WITH ME / STAY HERE (seg000:95e2, 9533): the speaker's topic
	// 5 or 6 line, then the travelling bit (ds:10).
	const uint16 bit = (uint16)(1 << _talkWho);
	const bool with = (_state.w(GameState::kPersonsWith) & bit) != 0;
	presentVerb(with ? 6 : 5);
	// The move happens unless the answer carried the refusal (event 2).
	if (_conversation->gateHeld()) {
		if (with) {
			_state.setW(GameState::kPersonsWith, _state.w(GameState::kPersonsWith) & ~bit);
			_world.settleCharacter(_talkWho);
			_world.removeCompanion(_talkWho);
		} else {
			_state.setW(GameState::kPersonsWith, _state.w(GameState::kPersonsWith) | bit);
			// seg000:9673: two at most; a third sends the first home.
			const int home = _world.addCompanion(_talkWho);
			if (home >= 0 && (uint)home < 16) {
				_state.setW(GameState::kPersonsWith, _state.w(GameState::kPersonsWith) & ~(1 << home));
				_world.settleCharacter((uint)home);
				_log.line(Common::String::format("Talk: character %d goes home", home));
			}
		}
	} else {
		_log.line(Common::String::format("Talk: character %u refuses", _talkWho));
	}
}

void GameScreen::answerQuestion(byte choice) {
	// seg000:241a/2432/2453: the choice (ds:9f), then the dialogue goes on
	// (loc_19472). Action 4 is the ACCEPT / REFUSE question of any speaker
	// (Stilgar's final attack, the Water of Life); only Duncan's is the
	// spice bargaining with its offer ladder.
	if (_conversation->bargainParty() == 0 && _talkWho == 3) {
		_world.bargainChoice(choice);
	} else if (_talkWho == World::kSmuggler) {
		_world.smugglerChoice(choice); // 241a / 2432 / 2453 with speaker 13
	} else {
		_state.setB(World::kChoice, choice);
		_state.setB(World::kArguing, (byte)(_state.b(World::kArguing) + 1));
	}
	_log.line(Common::String::format("Talk: choice %d", choice));
	_talkBargain = false;
	_talkEnded = false;
	_conversation->resume();
	advanceConversation();
}

void GameScreen::startConversation(uint character) {
	if (!loadDialogue()) {
		showStatus("Dune: dialogue data missing");
		return;
	}
	if (character == 2 && _state.b(0xc2) == 4 && _world.finalAttackReady()) {
		// seg000:9f40 -> 1243: any line of Thufir at stage 4 with 10 000 men
		// and atomics round the palace moves the final attack to stage 5.
		_state.setB(0xc2, 5);
		_log.line("Story: enough men round the palace, final attack stage 5");
	}
	if (character == World::kCaptain)
		_world.prepareCaptain();
	openTalk(character);
	_conversation->start(MIN<uint>(character, World::kFremenChief));
	advanceConversation();
}

void GameScreen::openTalk(uint character) {
	delete _talkSheet;
	_talkSheet = nullptr;
	// Troop chiefs past the first (groups 16, 17, ...) speak as group 15; a
	// Fremen's head is FRM1-3 by its troop (seg000:913b).
	const uint group = MIN<uint>(character, World::kFremenChief);
	const uint troop = _world.troopForPerson(character);
	static const char *const kFremenHeads[3] = { "FRM1.HSQ", "FRM2.HSQ", "FRM3.HSQ" };
	const char *sheetName = troop ? kFremenHeads[World::fremenHead(troop)] :
							group < Dialogue::kCharacters ? kCharacterSheets[group] : nullptr;
	_talkIdle = troop ? World::fremenExpression(troop) : 0;
	if (troop)
		stageTroopForConditions(troop); // the Fremen's lines read their troop's figures
	Common::Array<byte> sheet;
	if (sheetName && _resources.load(sheetName, sheet))
		_talkSheet = new Sprite(_system, sheet);
	else
		_log.line(Common::String::format("Conversation: no portrait sheet for character %u", character));
	_mode = kTalk;
	_talkWho = character;
	_talkEnded = false;
	_talkRecruit = 0;
	_talkLines.clear();
	_talkLine = 0;
	_talkBargain = false;
	_talkKind = kTalkNormal;
	debugSetScene(Common::String::format("talk/%u", character));
}
void GameScreen::applyStory() {
	if (_pendingPhase) {
		const byte phase = _pendingPhase;
		_pendingPhase = 0;
		setGamePhase(phase);
	}
	if (_pendingTriggers) {
		_pendingTriggers = false;
		runPhaseTriggers();
	}
	if (const byte phase = _world.takeRequestedPhase())
		setGamePhase(phase);
}

void GameScreen::advanceConversation() {
	if (_mode != kTalk || !_conversation || _talkBargain)
		return; // with the bargaining menu up only its answer goes on (answerQuestion)
	if (_pendingWakeUp) {
		waterOfLifeWakeUp();
		return;
	}
	applyStory();
	if (!_talkEnded && _talkLine + bubbleLines() < _talkLines.size()) {
		// The rest of a page that did not fit the balloon.
		_talkLine += bubbleLines();
	} else {
		Common::String page;
		bool newSentence = false;
		bool more = !_talkEnded && _conversation->next(page, newSentence);
		if (!more && !_talkEnded && _wakeResume.valid && !_conversation->paused()) {
			// The wake-up line said, the talk goes on where Stilgar's
			// answer left it (the original's next line: "I don't know what
			// the Water of Life has done to you...").
			_conversation->resumeAt(_wakeResume);
			_wakeResume.valid = false;
			more = _conversation->next(page, newSentence);
		}
		if (!more) {
			if (!_talkEnded && _conversation->paused()) {
				// Events 4 / 5: the bargaining menu (ds:1ffe) under the room.
				_talkBargain = true;
				_talkEnded = true;
				_talkLines.clear();
				drawTalk();
				dumpScreen(_system, "bargain");
				return;
			}
			if (_talkKind == kTalkScene && !_talkEnded) {
				sceneStep();
				return;
			}
			if (_talkKind == kTalkVision) {
				_talkKind = kTalkNormal;
				_visionDream = false;
				endConversation();
				return;
			}
			if (_pendingScene && _talkKind == kTalkNormal) {
				// Event 3 ended the talk: the scripted scene takes over.
				endConversation();
				return;
			}
			// Out of lines: the verbs stay, over the room (the video's 5:00).
			_talkEnded = true;
			_talkLines.clear();
			drawTalk();
			dumpScreen(_system, Common::String::format("talk-%u", ++_talkPage).c_str());
			return;
		}
		_talkLines.clear();
		_talkLine = 0;
		_talkLastPage = page;
		// The balloon's text width: the widest rect minus its paddings.
		_panel.wrapText(page, 208 - 2 * 16, false, _talkLines);
	}
	// Actions 4 / 5 (seg000:a244 / a248) run when the line's last segment
	// is drawn (sub_1A03F after sub_188D2) and open the bargaining menu
	// (ds:1ffe, sub_1D323) at once: ARGUE / ACCEPT / REFUSE come up with
	// the question, the line stays (Spice86: the smuggler's offer, Duncan's
	// "the totality of our stocks", checked 2026-09-29).
	if (_conversation->paused() && _talkLine + bubbleLines() >= _talkLines.size()) {
		_talkBargain = true;
		dumpScreen(_system, "bargain");
	}
	startTalkAnimation();
	drawTalk();
	dumpScreen(_system, Common::String::format("talk-%u", ++_talkPage).c_str());
}

void GameScreen::waterOfLifeWakeUp() {
	// callback_event_dialogue_line_08_Stilgar_drink_Water_of_Life (CD
	// seg000:2ccf, floppy 2f96), after Stilgar's "Drink it if that's your
	// will." and its voice (1abcc/1abd5, else 600 ticks): transitions 0x38
	// and 0x36 round a 1000-tick wait (Paul passes out; the engine draws no
	// transitions, the room simply comes back), three periods pass (0fd9,
	// done in World::stilgarWaterOfLife), then ds:23 = 0x11 and the room
	// scan (jmp 35ad): Stilgar's topic-4 line of condition 344,
	// "Ah, he is coming to. ... unconscious for three hours."
	_pendingWakeUp = false;
	if (_conversation) {
		_wakeResume = _conversation->position();
		_conversation->stop();
	}
	_talkLines.clear();
	_talkEnded = false;
	_talkBargain = false;
	_talkKind = kTalkNormal;
	_mode = kRoom;
	_state.setW(GameState::kPersonsTalkingTo, 0);
	// Transition 0x38, 1000 ticks (5 s), transition 0x36: the screen goes
	// black while Paul is out (seen on Spice86, captures/endgame/water-of-life).
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
	dumpScreen(_system, "wol-unconscious");
	if (!isFastCapture())
		_system->delayMillis(5000);
	_log.line(Common::String::format("Story: Paul comes to after the Water of Life (day %u, period %u)", _world.day(), _world.timeSlot()));
	showRoom(_world.room());
	_state.setB(0x23, 0x11);
	if (_mode == kRoom && !roomEntryScan(true)) {
		_log.line("Story: no one speaks as Paul comes to");
		_wakeResume.valid = false;
	}
}

uint GameScreen::bubbleLines() const {
	// The tallest of the three balloons (97 rows) holds eight 10-row lines.
	return 8;
}

void GameScreen::storyEvent(void *context, byte event, bool wasSaid, uint speaker) {
	GameScreen *screen = (GameScreen *)context;
	GameState &state = screen->_state;
	// Events 3, 8, 9 and 15 run every time their line is spoken (sub_1A03F
	// calls the handler before it looks at the said bit); 11 and 12 once.
	if (wasSaid && event != 3 && event != 8 && event != 9 && event != 15)
		return;
	switch (event) {
	case 8:
		// 0xA125, by speaker (ds:47c4). Jessica (1) trains Paul's mind: the
		// contact range grows (seg000:a186).
		if (speaker == 1) {
			const uint range = screen->_world.raiseContactRange();
			screen->_log.line(Common::String::format("Story: contact range -> %u cells", range));
		} else if (speaker == 3) {
			// Duncan puts his offers for the Emperor's shipment (seg000:2239).
			screen->_world.duncanOffers();
		} else if (speaker == 5) {
			// Stilgar: the Water of Life (seg000:2ccf, armed through ds:227e).
			const uint drink = screen->_world.stilgarWaterOfLife();
			if (drink == 2)
				screen->_pendingEnding = "Paul Atreides died as he tried";
			else if (drink == 1) {
				// After the line: 2d1e, ds:23 = 0x11, the room scan. The floppy
				// waits 600 ticks (0x258 at 200 Hz, 2fa8), the CD its voice.
				screen->_pendingWakeUp = true;
				screen->_wakeUpAt = screen->_system->getMillis() + 3000;
			}
		} else if (speaker == 12) {
			// Character 12 shows the hidden place whose pointer is at ds:11ce.
			screen->_world.revealPointedPlace(0x11ce);
		} else if (speaker == 13) {
			// callback_event_dialogue_line_08_Smugglers (seg000:2388): the
			// village chapter (phase 0x3c), then the trade is set up: ds:9e a
			// roll of 0-3, the smugglers' record (ds:10b4) + 3 today's date,
			// no argument yet; then the offer (23a5-23d4): the next item in
			// stock and its price (ds:9d).
			screen->setGamePhase(0x3c);
			state.setB(0x9e, (byte)(screen->_world.lcgRandMasked(3)));
			const uint16 smugglers = READ_LE_UINT16(&state.vars[screen->_world.ds(0x10b4)]);
			if (smugglers >= World::kCharacterTable && smugglers + 3 < GameState::kSize)
				state.vars[smugglers + 3] = (byte)(state.w(GameState::kGameTime) >> 4);
			state.setB(World::kArguing, 0);
			if (screen->_panel.findCommand("a spice-harvester") != 0xffff)
				screen->_world.setItemWords((uint16)(screen->_panel.findCommand("a spice-harvester") + 1));
			screen->_world.smugglerOffer();
		} else {
			// Every speaker with an event 8 is handled above (1 Jessica,
			// 3 Duncan seg000:2239, 5 Stilgar seg000:2ccf, 12 the captain,
			// 13 the smugglers seg000:2388); anything else is logged.
			screen->_log.line(Common::String::format("Story: event 8 of speaker %u not implemented", speaker));
		}
		break;
	case 9:  // 0xA157: 3 -> seg000:24ee, 5 -> 2d2c, 13 -> 2419
	case 15: // 0xA172: 1 -> ds:f5 + 1, 3 -> seg000:24a3
		if (event == 15 && speaker == 1) {
			state.setB(0xf5, (byte)(state.b(0xf5) + 1));
			break;
		}
		if (event == 9 && speaker == 5) {
			Common::Array<uint> ids;
			screen->_world.finalAttackTroops(ids); // seg000:2d2c
			break;
		}
		if (event == 9 && speaker == 3) {
			// seg000:24ee; ds:476d: the menu was about a smuggler's bill (action 5).
			screen->_world.duncanAccept(screen->_conversation && screen->_conversation->bargainParty() == 1);
			break;
		}
		if (event == 15 && speaker == 3) {
			// seg000:24a3: the bargaining is over; the Emperor answers by COMM.
			bool endTalk = false;
			const uint16 sighting = screen->_world.duncanClosing(endTalk);
			if (sighting)
				screen->_world.addSighting(sighting);
			if (endTalk && screen->_conversation)
				screen->_conversation->endAfterLine();
			break;
		}
		screen->_log.line(Common::String::format("Story: event %u of speaker %u not implemented", event, speaker));
		break;
	case 3: {
		// 0xA1F7: the phase's scripted scene (sub_11771) takes over after this line.
		screen->_pendingScene = screen->_world.phaseSceneScript();
		screen->_log.line(Common::String::format("Story: scripted scene %#x (phase %#x)", screen->_pendingScene,
				state.b(GameState::kPhase)));
		if (screen->_conversation)
			screen->_conversation->endAfterLine();
		break;
	}
	case 11:
		// 0xA219: phase + 1, the triggers, and Duncan comes at phase 1 (seg000:100b).
		state.setB(GameState::kPhase, (byte)(state.b(GameState::kPhase) + 1));
		state.setB(0xff, 0);
		screen->_log.line(Common::String::format("Story: phase -> %#x", state.b(GameState::kPhase)));
		if (state.b(GameState::kPhase) == 1)
			state.vars[World::kCharacterTable + 3 * World::kCharacterSize + 3] = 1;
		screen->_pendingTriggers = true;
		break;
	case 12:
		// 0xA235: the next chapter.
		screen->_pendingPhase = (byte)((state.b(GameState::kPhase) & 0xfc) + 4);
		break;
	default:
		break;
	}
}

void GameScreen::setGamePhase(byte phase) {
	if (phase <= _state.b(GameState::kPhase))
		return;
	_state.setB(GameState::kPhase, phase);
	_state.setB(0xff, 0);
	_log.line(Common::String::format("Story: chapter -> phase %#x", phase));
	runPhaseTriggers();
	if (phase <= 0x6c && !(phase & 3)) {
		uint16 cutscene, vision;
		_world.phaseCallback(phase, cutscene, vision);
		if (cutscene)
			_pendingScene = cutscene;
		if (vision)
			_log.line(Common::String::format("Story: vision / message %#x not shown yet", vision));
	}
	refreshRooms();
}

void GameScreen::runPhaseTriggers() {
	// present_game_phase_trigger_line (seg000:96b5): DIALOGUE slot 135, the
	// first entry whose condition holds, mask 0x80; its line is not shown,
	// its event fires. A separate conversation keeps the current one intact.
	if (!loadDialogue())
		return;
	Conversation triggers(*_sentences, *_dialogue, *_conditions, _state, _log);
	triggers.setEventHandler(&GameScreen::storyEvent, this);
	triggers.start(16, 7, 0x80, true, true);
	Common::String page;
	bool newSentence;
	if (triggers.next(page, newSentence))
		triggers.finishPending(); // only the selected entry's event fires
	_state.setW(GameState::kPersonsTalkingTo, 0);
}

void GameScreen::presentVerb(uint list) {
	// A verb presents one list of the speaker (seg000:95e2: topic 5 for COME
	// WITH ME / WORK FOR ME, 6 for STAY HERE) with the auto mask 0x20.
	_talkEnded = false;
	_talkLines.clear();
	_talkLine = 0;
	// One answer line (seg000:9f8b -> 9f9e: the first whose condition holds);
	// its action counts before the gate is read (95f7), and ds:23 is then
	// cleared (95f2).
	_conversation->start(MIN<uint>(_talkWho, World::kFremenChief), list, 0x20, true, true);
	_conversation->armGate(); // arm_dialogue_interrupt_gate
	advanceConversation();
	_conversation->finishPending();
	_state.setB(0x23, 0);
}
void GameScreen::endConversation() {
	// STOP TALKING.
	if (_pendingWakeUp) {
		// Paul cannot walk away from the Water of Life: he passes out first.
		waterOfLifeWakeUp();
		return;
	}
	_wakeResume.valid = false;
	delete _talkSheet;
	_talkSheet = nullptr;
	_talkLines.clear();
	_talkAnimating = false;
	_talkEnded = false;
	_talkRecruit = 0;
	_talkBargain = false;
	_talkKind = kTalkNormal;
	_visionDream = false;
	if (_conversation)
		_conversation->stop();
	_state.setW(GameState::kPersonsTalkingTo, 0);
	_mode = kRoom;
	_log.line("Conversation: over");
	if (_pendingEnding) {
		// pending_room_screen_request 3: Paul died drinking the Water of Life.
		_endingText = _pendingEnding;
		_pendingEnding = nullptr;
		emperorEnding();
		return;
	}
	// The room is drawn again, and init_room_persons stages a village's
	// smugglers again: no offer, a new first item (seg000:3166, 2318).
	stageVillageSmugglers();
	showRoom(_world.room());
}
int GameScreen::personAt(int x, int y) const {
	const Common::Array<byte> &sheet = _panel.characterSheet();
	if (sheet.empty())
		return -1;
	Sprite characters(_system, sheet);
	for (uint who = 0; who < ARRAYSIZE(_personPos); ++who) {
		if (_personPos[who].x < 0)
			continue;
		uint16 w, h;
		if (!characters.frameSize(_personFrame[who], w, h))
			continue;
		if (Common::Rect(_personPos[who].x, _personPos[who].y, _personPos[who].x + w, _personPos[who].y + h).contains(x, y))
			return (int)who;
	}
	return -1;
}

void GameScreen::startTalkAnimation() {
	// One of the sheet's talking animations per page (the last one is the
	// lip-sync set), at the resources' 12 fps. Harness and dump runs keep
	// the rest pose so their frames stay deterministic.
	const uint talking = _talkSheet && _talkSheet->animationCount() > 1 ?
			MIN<uint>(4, _talkSheet->animationCount() - 1) : 0;
	_talkAnimating = talking > 0 && !isDumpRun() && !isDuneHarnessRun();
	_talkAnimation = talking ? _talkPage % talking : 0;
	_talkFrame = 0;
	_talkStart = _system->getMillis();
}

void GameScreen::update() {
	// The clock runs in real time as the executable's does: a period per
	// 12000 ticks of its 200.3 Hz timer (seg000:ef6a), stopped while a
	// dialogue, the book or a menu holds the game (game_suspend_count).
	// Never in dump or harness runs, whose pictures must not depend on time.
	if (const uint periods = takeDuneHarnessPeriods()) {
		_log.line(Common::String::format("Harness: %u periods pass", periods));
		for (uint n = 0; n < periods && !_quitRequested; ++n)
			passTime(1);
		if (_mode == kRoom)
			drawRoom();
		else if (_mode == kMap)
			drawMapScreen();
	}
	if (_pendingWakeUp && _mode == kTalk && (isFastCapture() || _system->getMillis() >= _wakeUpAt))
		waterOfLifeWakeUp(); // Stilgar's line has had its time (seg000:2ccf)
	if (!isFastCapture()) {
		const uint32 now = _system->getMillis();
		if (!_clockStart || (_mode != kRoom && _mode != kMap))
			_clockStart = now;
		else if (now - _clockStart >= World::kPeriodMillis)
			passTime(1);
		checkIdle(now);
		updateCockpit(now);
		// The globe's rotation task (CD seg000:b9ae) turns it one phase a
		// pass while it is up, in both visions. Never in dump or harness
		// runs (isFastCapture above; nor a real-time harness), whose
		// pictures must not depend on time.
		if (_mode == kMap && _map && _map->mode() == MapScreen::kGlobe && !_cockpit && !isDuneHarnessRun() &&
				!isDumpRun() && _map->creep(now))
			drawMapScreen();
		// The DUNE MAP popup goes 1000 ticks after the view opened (seg000:5c03).
		if (_mode == kMap && _map && _map->caption() && now - _map->captionStart() >= MapScreen::kCaptionMillis) {
			_map->setCaption(false);
			drawMapScreen();
		}
	}
	if (_mode != kTalk || !_talkAnimating || !_talkSheet)
		return;
	const uint32 elapsed = _system->getMillis() - _talkStart;
	const uint count = _talkSheet->animationFrameCount(_talkAnimation);
	const uint frame = elapsed * 12 / 1000;
	if (frame >= count) {
		_talkAnimating = false;
		_talkFrame = 0;
	} else if (frame == _talkFrame) {
		return;
	} else {
		_talkFrame = frame;
	}
	drawTalk();
}

void GameScreen::drawTalk() {
	if (_visionDream) {
		// present_vision_dream (seg000:2bd2): the line over VIS.HSQ.
		_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
		_panel.applyPalette();
		Common::Array<byte> data;
		if (_resources.load("VIS.HSQ", data)) {
			Sprite vis(_system, data);
			vis.setPalette();
			vis.drawFrame(0, _surface.surfacePtr(), 0, 0);
		}
		const byte black[3] = { 0, 0, 0 };
		_system->getPaletteManager()->setPalette(black, 0, 1);
	} else if (_cabinView) {
		drawCabin();
	} else {
		composeView();
	}
	if (!_talkEnded) {
		// While a line is spoken the view zooms twice onto the speaker (the
		// room pixel-doubled, as the recording shows), the portrait comes up
		// on the left and the line sits in a balloon.
		if (!_visionDream && !_cabinView) {
		Common::Point p = _talkWho < ARRAYSIZE(_personPos) ? _personPos[_talkWho] : Common::Point(-1, -1);
		if (p.x < 0)
			p = Common::Point(160, 60);
		const int x0 = CLIP<int>(p.x + 12 - 80, 0, 160), y0 = CLIP<int>(p.y + 20 - 38, 0, 76);
		Graphics::ManagedSurface copy;
		copy.create(320, 152, Graphics::PixelFormat::createFormatCLUT8());
		copy.blitFrom(*_surface.surfacePtr(), Common::Rect(0, 0, 320, 152), Common::Point(0, 0));
		for (int y = 0; y < 152; ++y) {
			const byte *src = (const byte *)copy.getBasePtr(x0, y0 + y / 2);
			byte *dst = (byte *)_surface.getBasePtr(0, y);
			for (int x = 0; x < 320; ++x)
				dst[x] = src[x / 2];
		}
		copy.free();
		}
		if (_talkSheet) {
			_talkSheet->setPalette();
			const byte black[3] = { 0, 0, 0 };
			_system->getPaletteManager()->setPalette(black, 0, 1);
			if (_world.amiga())
				amigaMirrorUiColours(_system);
			const uint animation = _talkAnimating ? _talkAnimation : _talkIdle;
			const uint frame = _talkAnimating ? _talkFrame : 0;
			if (!_talkSheet->drawAnimationFrame(animation, frame, _surface.surfacePtr(), 0, 0) &&
					!_talkSheet->drawAnimationFrame(0, 0, _surface.surfacePtr(), 0, 0))
				_talkSheet->drawFrame(0, _surface.surfacePtr(), 0, 0);
		}
		// The three voice balloons (ds:2224): the first whose height fits
		// (paddings 40/16/16/16, seg000:9f40). The balloon starts right of the
		// speaker's mouth box (talking_head_mouth_box_table, ds:27fa), which
		// is where the recording's balloons begin (Leto 135, the Fremen ~155).
		// The three voice balloons (ds:2224): the first whose height fits
		// (paddings 40/16/16/16, seg000:9f40). The balloon starts right of the
		// speaker's mouth box (talking_head_mouth_box_table, ds:27fa). The
		// user prefers these smaller balloons to the recordings' larger ones.
		static const int16 kBalloons[3][4] = { { 80, 14, 192, 72 }, { 80, 16, 200, 86 }, { 80, 8, 208, 97 } };
		static const int16 kMouthRight[17] = { 99, 105, 140, 101, 104, 114, 109, 114, 101, 113, 126, 120, 84, 86, 119, 137, 100 };
		const uint count = MIN<uint>(bubbleLines(), _talkLines.size() - MIN<uint>(_talkLine, _talkLines.size()));
		uint b = 0;
		while (b < 2 && (int)(count * 10 + 32) > kBalloons[b][3])
			++b;
		const uint troop = _world.troopForPerson(_talkWho);
		const uint head = troop ? 14 + World::fremenHead(troop) : MIN<uint>(_talkWho, 16);
		const int left = MAX<int>(kBalloons[b][0], kMouthRight[head] + 24);
		const Common::Rect box(left, kBalloons[b][1], MIN<int>(320, kBalloons[b][0] + kBalloons[b][2] + 24),
				kBalloons[b][1] + kBalloons[b][3]);
		if (_talkLines.size() && _talkLine < _talkLines.size() && box.width() - 24 < 176) {
			// Re-wrap the page to the balloon's width.
			Common::String text;
			for (uint i = 0; i < _talkLines.size(); ++i)
				text += (i ? " " : "") + _talkLines[i];
			Common::Array<Common::String> narrow;
			_panel.wrapText(text, box.width() - 24, false, narrow);
			drawBubble(narrow, 0, MIN<uint>(bubbleLines(), narrow.size()), box, 0);
		} else {
			drawBubble(_talkLines, _talkLine, count, box, 0);
		}
	}
	setTalkRows();
	// The recordings keep the room's exits lit while someone talks; a
	// scripted scene's lines show the compass dark.
	bool exits[4] = { false, false, false, false };
	bool canLeave = false;
	roomNav(exits, canLeave);
	if (_sceneActive)
		_panel.setNavMode(Panel::kNavBlank);
	if (_cabinView) {
		// menu_go_towards_this_place (floppy ds:2618): one row; the flight
		// already homes on the place, so the compass is dark.
		RowAction actions[Panel::kCommandRows] = { kRowNone };
		int arguments[Panel::kCommandRows] = { 0 };
		uint16 commands[Panel::kCommandRows] = { _panel.findCommand("GO TOWARDS THIS PLACE") };
		setRows(actions, arguments, commands, 1);
		for (uint d = 0; d < 4; ++d)
			exits[d] = false;
		_panel.setNavMode(Panel::kNavBlank);
	}
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	_panel.draw(_surface, exits, -1, -1, day());
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

void GameScreen::drawBubble(const Common::Array<Common::String> &lines, uint first, uint count, const Common::Rect &box,
		byte ink) {
	// The balloon is ICONES 0x1c (33x29) tiled over the rect; the text is
	// justified as layout_subtitle_lines does (the spare width shared out
	// between the words, the last line of a paragraph left alone) and
	// centred vertically.
	if (_world.amiga()) {
		// The Amiga's balloon is a plain box in colour 16 (0xa9b in every
		// character sheet) with the text in colour 29 (dark red), as the
		// recording shows.
		_surface.fillRect(box, 16);
		ink = 29;
	} else {
		Graphics::Surface area = _surface.surfacePtr()->getSubArea(box);
		Graphics::ManagedSurface tiles;
		tiles.create(box.width(), box.height(), Graphics::PixelFormat::createFormatCLUT8());
		for (int y = 0; y < box.height(); y += 29)
			for (int x = 0; x < box.width(); x += 33)
				_panel.drawIcon(tiles, 0x1c, x, y);
		area.copyRectToSurface(*tiles.surfacePtr(), 0, 0, Common::Rect(0, 0, box.width(), box.height()));
		tiles.free();
	}
	const int lineHeight = 10, leftPad = 12, rightPad = 12;
	const int width = box.width() - leftPad - rightPad;
	int y = box.top + (box.height() - (int)count * lineHeight) / 2;
	for (uint i = first; i < first + count && i < lines.size(); ++i, y += lineHeight) {
		Common::String line = lines[i];
		const bool paragraphEnd = !line.empty() && line.lastChar() == '\x01';
		if (paragraphEnd)
			line.deleteLastChar();
		Common::Array<Common::String> words;
		Common::String word;
		for (uint k = 0; k <= line.size(); ++k) {
			if (k == line.size() || line[k] == ' ') {
				if (!word.empty())
					words.push_back(word);
				word.clear();
			} else {
				word += line[k];
			}
		}
		const bool last = i + 1 >= lines.size() || paragraphEnd;
		int used = 0;
		for (uint k = 0; k < words.size(); ++k)
			used += _panel.textWidth(words[k].c_str(), false);
		const int gaps = (int)words.size() - 1;
		const int space = (!last && gaps > 0) ? MAX(3, (width - used) / gaps) : _panel.textWidth(" ", false) + 1;
		int x = box.left + leftPad;
		for (uint k = 0; k < words.size(); ++k) {
			_panel.drawText(_surface, words[k].c_str(), x, y, ink, false);
			x += _panel.textWidth(words[k].c_str(), false) + space;
		}
	}
}

void GameScreen::setTalkRows() {
	// setup_npc_dialogue_menu: " TALK TO ME ", the speaker's verb, STOP TALKING.
	RowAction actions[Panel::kCommandRows];
	int arguments[Panel::kCommandRows];
	uint16 commands[Panel::kCommandRows];
	uint count = 0;
	auto add = [&](RowAction action, const char *text, int argument = 0) {
		const uint16 id = _panel.findCommand(text);
		if (count < Panel::kCommandRows && id != 0xffff) {
			actions[count] = action;
			arguments[count] = argument;
			commands[count] = id;
			++count;
		}
	};
	if (_talkBargain) {
		// menu ds:1ffe: ARGUE (seg000:2453), ACCEPT (241a), REFUSE (2432), WHAT ?
		add(kRowBargain, "ARGUE", 3);
		add(kRowBargain, "ACCEPT", 1);
		add(kRowBargain, "REFUSE", 2);
		add(kRowWhat, "\" WHAT ? \"");
		setRows(actions, arguments, commands, count);
		return;
	}
	if (_talkKind == kTalkScene || _talkKind == kTalkVision) {
		// menu ds:1fae: " Continue..." and WHAT ?
		add(kRowContinue, " Continue...");
		add(kRowWhat, "\" WHAT ? \"");
		setRows(actions, arguments, commands, count);
		return;
	}
	if (_talkKind == kTalkComm) {
		// menu ds:1ff2: " Viewed" and WHAT ?
		add(kRowViewed, " Viewed");
		add(kRowWhat, "\" WHAT ? \"");
		setRows(actions, arguments, commands, count);
		return;
	}
	add(kRowTalkMore, "\" TALK TO ME \"");
	if (_talkWho == World::kFremen)
		add(kRowWorkForMe, "\" WORK FOR ME \"");
	else if (_talkWho >= World::kFremenChief)
		add(kRowGiveOrders, "GIVE ORDERS TO TROOP");
	else if (_talkWho < World::kFremen && _talkWho != 0) // not the Duke in the palace
		add(kRowCompanion, (_state.w(GameState::kPersonsWith) & (1 << _talkWho)) ? "\" STAY HERE \"" : "\" COME WITH ME \"");
	else if (_talkWho == 0)
		add(kRowCompanion, "\" COME WITH ME \"");
	add(kRowStopTalking, "STOP TALKING");
	setRows(actions, arguments, commands, count);
}
// ---- The book ---------------------------------------------------------------

void GameScreen::openBook() {
	if (!loadDialogue()) {
		showStatus("Dune: dialogue data missing");
		return;
	}
	if (!_book)
		_book = new Book(_system, _resources, _log, _panel, *_sentences, *_dialogue, *_conditions, _state);
	if (!_book->open()) {
		showStatus("Dune: BOOK.HSQ missing");
		return;
	}
	_mode = kBook;
	debugSetScene("book/cover");
	_log.line("Book: opened");
	drawBook();
	dumpScreen(_system, "book-cover");
}

void GameScreen::bookAction(BookAction action, int row) {
	if (_mode != kBook || !_book)
		return;
	switch (action) {
	case kBookTopic:
		_book->setTopic((uint)row);
		break;
	case kBookNext:
		_book->nextPage();
		break;
	case kBookPrevious:
		_book->previousPage();
		break;
	}
	debugSetScene(Common::String::format("book/topic-%u/page-%u", _book->topic(), _book->page() + 1));
	drawBook();
	dumpScreen(_system, Common::String::format("book-topic-%u-page-%u", _book->topic(), _book->page() + 1).c_str());
}

void GameScreen::closeBook() {
	_panel.setBookOpen(false);
	_mode = kRoom;
	_log.line("Book: closed");
	showRoom(_world.room());
}

void GameScreen::drawBook() {
	_book->draw(_surface);
	// The command box lists the topics; the chosen one shows pressed.
	uint16 rows[Panel::kCommandRows];
	for (uint i = 0; i < Panel::kCommandRows; ++i)
		rows[i] = (uint16)(Book::kCommandAllTopics + i);
	_panel.setCommandRows(rows, Panel::kCommandRows);
	_panel.setBookOpen(true);
	const bool exits[4] = { false, false, false, false };
	_panel.draw(_surface, exits, _book->onCover() ? -1 : (int)_book->topic(), -1, day());
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

bool GameScreen::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
		return true;
	if (event.type == Common::EVENT_LBUTTONDOWN || event.type == Common::EVENT_KEYDOWN)
		_idleStart = _system->getMillis();
	if (_ending) {
		// The Emperor's ending: a tap starts a new game.
		if (event.type == Common::EVENT_LBUTTONDOWN || event.type == Common::EVENT_KEYDOWN) {
			_ending = false;
			startNewGame();
		}
		return false;
	}
	if (_mode == kRoom && _sceneActive && event.type == Common::EVENT_LBUTTONDOWN && event.mouse.y < 152) {
		sceneStep(); // a tap on the shot is " Continue..."
		return false;
	}

	if (_mode == kMap && _map) {
		if ((event.type == Common::EVENT_LBUTTONDOWN || event.type == Common::EVENT_RBUTTONDOWN) && _map->caption()) {
			// Either button dismisses the DUNE MAP popup (seg000:5c76, 5ce4).
			_map->setCaption(false);
			if (event.type == Common::EVENT_RBUTTONDOWN)
				drawMapScreen();
		}
		if (event.type == Common::EVENT_LBUTTONDOWN) {
			const int arrow = _map->hitArrow(event.mouse.x, event.mouse.y);
			if (arrow >= 0) {
				static const int dx[5] = { 0, 1, 0, -1, 0 }, dy[5] = { -1, 0, 1, 0, 0 };
				if (arrow == 4 && _map->mode() == MapScreen::kGlobe)
					_map->centreOnPlayer(); // sprite 53, CD seg000:ba9e
				else if (arrow == 4)
					_map->centreOn(_world.currentLocation());
				else if (_map->mode() == MapScreen::kFlat)
					_map->scroll(dx[arrow], dy[arrow]);
				else
					// The right arrow adds 0x20 phases (CD b9d3), the left one
					// takes them (b9cc); the up arrow adds 8 to the tilt (b9b9),
					// the down one takes 8 (b9c0).
					_map->rotate(dx[arrow] * 0x20, -dy[arrow] * 8);
				drawMapScreen();
				return false;
			}
			int row, arrowHit;
			const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrowHit);
			if (action == Panel::kActionCommand && row >= 0 && row < (int)Panel::kCommandRows) {
				_log.line(Common::String::format("Map command: %s", _panel.commandText(row) ? _panel.commandText(row) : "(none)"));
				switch (_rowActions[row]) {
				case kRowExitMap:
					_riding = false;
					if (_movingTroop) {
						_movingTroop = 0;
						openMap(MapScreen::kFlat, false);
						break;
					}
					leaveMap();
					break;
				case kRowWormTravel:
					rideWormTo(_rowArguments[row]);
					break;
				case kRowMoveDone: {
					// seg000:8214: the troop acknowledges and marches.
					const uint id = _movingTroop;
					_movingTroop = 0;
					if (_world.issueMoveOrder(id, (uint)_rowArguments[row]))
						dumpScreen(_system, "troop-moving");
					openMap(MapScreen::kFlat, false);
					break;
				}
				case kRowFly:
					if (_rowArguments[row] == -2)
						travelToward(_map->pointLongitude(), _map->pointLatitude());
					else
						travelTo((uint)_rowArguments[row]);
					break;
				case kRowResults: {
					// The panels slide over the planet (0.8 s in swift-dune).
					// The globe keeps its colours while they move: SEE RESULTS
					// (CS:B86A) sets the flag after the slide, STANDARD VISION
					// (CS:B860) clears it after the panels are shut.
					const bool open = !_map->results();
					const uint32 start = _system->getMillis();
					for (uint pixels = 0; !isFastCapture() && pixels < 100;) {
						pixels = MIN<uint>(100, (_system->getMillis() - start) * 100 / 800);
						_map->setResults(open ? pixels : 100 - pixels, !open);
						drawMapScreen();
						_system->delayMillis(15);
					}
					_map->setResults(open ? 100 : 0, open);
					drawMapScreen();
					if (open)
						dumpScreen(_system, "results");
					break;
				}
				case kRowDensity:
					_map->setDensity(!_map->density());
					drawMapScreen();
					if (_map->density())
						dumpScreen(_system, "map-density");
					break;
				case kRowOrders: {
					// The selected place's hired troop, or the one where Paul is.
					uint id = 0;
					Common::Array<uint> ids;
					_world.troopsAt(_map->destination() >= 0 ? (uint)_map->destination() : _world.currentLocation(), ids);
					for (uint i = 0; i < ids.size() && !id; ++i)
						if (!_world.troop(ids[i]).harkonnen() && _world.troop(ids[i]).hired())
							id = ids[i];
					if (id) {
						_recruiting = false;
						openTroop(id, true);
					}
					else
						showStatus(Common::String::format("Dune: %s", _panel.commandString(_panel.findCommand("no troops")).c_str())
										   .c_str());
					break;
				}
				case kRowFlatMap:
					openMap(MapScreen::kFlat, false);
					break;
				case kRowOrnithopter:
					// seg000:42e9: the orni cockpit, choosing a destination.
					openCockpit(false);
					break;
				case kRowCockpitCancel:
					cockpitCancel();
					break;
				case kRowProspectors:
					findProspectors();
					break;
				case kRowContact: {
					// seg000:86cc: the next rallied troop after the last one
					// contacted; the map centres on it and its popup opens.
					const uint id = nextRalliedTroop(_lastContacted);
					if (id) {
						_lastContacted = id;
						_recruiting = false;
						const Troop t = _world.troop(id);
						if (t.location >= Location::kTableOffset)
							_map->centreOn((t.location - Location::kTableOffset) / Location::kRecordSize);
						openTroop(id, true);
					}
					break;
				}
				case kRowSaveMenu:
					openMenu(kMenuSave);
					break;
				case kRowLoadMenu:
					openMenu(kMenuLoad);
					break;
				case kRowOptionsMenu:
					openMenu(kMenuOptions);
					break;
				case kRowMenuBack:
					openMenu((Menu)_rowArguments[row]);
					break;
				case kRowSaveSlot:
					saveSlot((uint)_rowArguments[row]);
					break;
				case kRowLoadSlot:
					loadSlot((uint)_rowArguments[row]);
					break;
				case kRowMusic:
					if (_rowArguments[row])
						_musicOrder = 0; // CS:ac42: game-relative selection clears DS:33fe.
					if (_musicOn != (_rowArguments[row] != 0))
						toggleMusic();
					openMenu(kMenuNone); // CS:ac4c/ae48 pop Options.
					break;
				case kRowMusicOrderMenu:
					openMenu(kMenuMusicOrder);
					break;
				case kRowMusicOrder:
					_musicOrder = (byte)_rowArguments[row]; // CS:ac64/ac6b: shuffle=3, standard=1.
					if (!_musicOn)
						toggleMusic();
					openMenu(kMenuNone); // CS:ac85/ac88 pop the order menu and Options.
					break;
				case kRowRestart:
					startNewGame();
					break;
				case kRowExitGame:
					openMenu(kMenuQuit);
					break;
				case kRowConfirmExit:
					_log.line("Options: exit confirmed");
					_quitRequested = true;
					break;
				case kRowCancelExit:
					openMenu(kMenuOptions);
					break;
				case kRowNone:
					break;
				default:
					if (_panel.commandText(row))
						showStatus(Common::String::format("Dune: %s", _panel.commandText(row)).c_str());
					break;
				}
				return false;
			}
			if (!_cockpit && _map->mode() == MapScreen::kFlat &&
					Common::Rect(22, 161, 68, 196).contains(event.mouse.x, event.mouse.y)) {
				// The planet in the flat map's left panel (ICONES 0x0d, drawn by
				// drawPanelExtras) opens the globe and its game menu; Paul's
				// head does too. The globe starts from the flat view's centre
				// (ds:2144/2146), scrolled or not.
				openMap(MapScreen::kGlobe, false, true);
				return false;
			}
			if (action == Panel::kActionHead || action == Panel::kActionBook) {
				// Paul's head and the book close the globe as they open it.
				if (_cockpit)
					cockpitCancel();
				else
					leaveMap();
				return false;
			}
			if (_map->density() && _map->mode() == MapScreen::kFlat &&
					Common::Rect(75, 15, 85, 25).contains(event.mouse.x, event.mouse.y)) {
				// The SPICE DENSITY panel's close box (ONMAP 0x8D's corner).
				_map->setDensity(false);
				drawMapScreen();
				return false;
			}
			if (event.mouse.y < 152)
				mapTap(event.mouse.x, event.mouse.y);
		} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			if (_menu != kMenuNone)
				openMenu(kMenuNone);
			else if (_cockpit)
				cockpitCancel();
			else
				leaveMap();
		}
		return false;
	}
	if (_mode == kBook) {
		if (event.type == Common::EVENT_LBUTTONDOWN) {
			int row, arrow;
			const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrow);
			// The box where the compass stands closes the book too (the
			// original's explore capture: a click at (275,176), then the room).
			const bool exitBox = event.mouse.y >= 152 && event.mouse.x >= 236;
			if (action == Panel::kActionBook || exitBox)
				closeBook();
			else if (action == Panel::kActionCommand && row >= 0)
				bookAction(kBookTopic, row);
			else if (event.mouse.y < 152)
				bookAction(event.mouse.x < 160 ? kBookPrevious : kBookNext);
		} else if (event.type == Common::EVENT_KEYDOWN) {
			if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
				closeBook();
			else if (event.kbd.keycode == Common::KEYCODE_LEFT)
				bookAction(kBookPrevious);
			else
				bookAction(kBookNext);
		}
		return false;
	}
	if (_mode == kTroop) {
		if (event.type == Common::EVENT_LBUTTONDOWN && _troopPicking && event.mouse.y < 152) {
			troopPickTap(event.mouse.x, event.mouse.y);
			return false;
		}
		if (event.type == Common::EVENT_LBUTTONDOWN && _troopEquipment && event.mouse.y < 152) {
			if (equipmentTap(event.mouse.x, event.mouse.y))
				drawTroop();
			return false;
		}
		if (event.type == Common::EVENT_LBUTTONDOWN) {
			int row, arrow;
			const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrow);
			if (action != Panel::kActionCommand || row < 0 || row >= (int)Panel::kCommandRows)
				return false;
			_log.line(Common::String::format("Troop command: %s", _panel.commandText(row) ? _panel.commandText(row) : "(none)"));
			switch (_rowActions[row]) {
			case kRowTroopTalk:
				_mode = kRoom;
				startConversation(World::kFremenChief);
				return false;
			case kRowAskMore:
				nextTroopLine();
				drawTroop();
				return false;
			case kRowTroopOccupation:
				_troopChoosing = true;
				drawTroop();
				return false;
			case kRowEquipment:
				_troopEquipment = true; // seg000:7cbb
				drawTroop();
				dumpScreen(_system, "troop-equipment");
				return false;
			case kRowEquipDone:
				// seg000:7d68: the job's viability is re-checked with the new kit.
				_troopEquipment = false;
				if ((_world.troop(_troopId).occupation & 0x0f) == Troop::kIrrigation)
					_state.vars[World::kTroopTable + (_troopId - 1) * World::kTroopSize + 3] &= (byte)~Troop::kStopped;
				stageTroopForConditions(_troopId);
				drawTroop();
				return false;
			case kRowComeWithMe:
				_recruiting = false;
				if (_world.troopAgreesToFollow(_troopId)) {
					_world.rallyTroop(_troopId);
					_log.line(Common::String::format("Troops: troop %u follows Paul", _troopId));
					drawTroop();
					dumpScreen(_system, "troop-rallied");
				} else {
					_log.line(Common::String::format("Troops: troop %u refuses (charisma %u)", _troopId,
							_state.b(World::kCharisma)));
					showStatus("Dune: the Fremen refuse to follow you yet");
					showRoom(_world.room());
				}
				return false;
			case kRowStopTalking:
				_recruiting = false;
				showRoom(_world.room());
				return false;
			case kRowMoveTroop:
				startTroopPick();
				return false;
			case kRowPickCancel:
				endTroopPick(-1);
				return false;
			case kRowPickAdd:
				return false; // seg000:8dc7 (floppy): a bare return, the next pick appends
			case kRowPickNew:
				// sub_ACA9 then loc_AE52: the queue emptied, the popup redrawn.
				_pickCount = 0;
				_pickQueue[0] = _pickQueue[1] = _pickQueue[2] = 0;
				drawTroop();
				return false;
			case kRowPickDone:
				endTroopPick(-3);
				return false;
			case kRowEspionage:
				_troopChoosing = false;
				if (_world.startEspionage(_troopId))
					_log.line(Common::String::format("Troops: troop %u goes spying", _troopId));
				drawTroop();
				dumpScreen(_system, "troop-espionage");
				return false;
			case kRowAttack: {
				_troopChoosing = false;
				const int here = _world.troopPlace(_troopId);
				if (here >= 0)
					_world.startAttack((uint)here);
				drawTroop();
				dumpScreen(_system, "troop-attack");
				return false;
			}
			case kRowSearchEquipment:
				searchForEquipment();
				return false;
			case kRowSetOccupation: {
				// troop_apply_occupation_choice (CD 6a89): the job (SPECIALIZE IN
				// SPICE makes the prospectors prospect, CD 6a76), then the troop's
				// answer: one line of its list 4 with pending_room_action 0x0A
				// (CD 7bbe). A line whose event drops the gate refuses, and the
				// order is taken back.
				byte job = (byte)_rowArguments[row];
				if (job == Troop::kSpiceMining && _troopId == World::kProspectorTroop)
					job = Troop::kProspecting;
				byte before[World::kTroopSize];
				_world.saveTroopRecord(_troopId, before);
				_world.setTroopOccupation(_troopId, job);
				_log.line(Common::String::format("Troops: troop %u now %s", _troopId, _panel.commandText(row)));
				_troopChoosing = false;
				if (_troopFromMap) {
					stageTroopForConditions(_troopId);
					_state.setB(0x23, 0x0a);
					_conversation->start(World::kFremenChief, 4, 0x20, true, true); // 7bbe -> 96f1 -> 9f8b: sentence mask 0x20
					_conversation->armGate();
					Common::String page;
					bool newSentence;
					if (_conversation->next(page, newSentence))
						_troopLine = page;
					if (!_conversation->gateHeld()) {
						_world.restoreTroopRecord(_troopId, before);
						_log.line(Common::String::format("Troops: troop %u refuses the order", _troopId));
					}
					if (_pendingScene == 0x12f8) {
						// The prospectors' answer (event 3) below phase 0x14: the map lesson, in the popup.
						_troopScene = _pendingScene;
						_pendingScene = 0;
						_troopSceneCursor = 0;
					}
				}
				drawTroop();
				dumpScreen(_system, "troop-ordered");
				return false;
			}
			case kRowContinue:
				troopSceneStep();
				return false;
			case kRowTroopDone:
				if (_troopChoosing) {
					_troopChoosing = false;
					drawTroop();
					return false;
				}
				break;
			default:
				return false;
			}
		} else if (!(event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE)) {
			return false;
		}
		if (_troopFromMap && !_troopFromRoom) {
			_mode = kMap;
			drawMapScreen();
		} else {
			_troopFromRoom = false;
			leaveMap();
		}
		return false;
	}
	if (_mode == kMirror) {
		int row = -1, arrow = -1;
		if (event.type == Common::EVENT_LBUTTONDOWN &&
				_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) == Panel::kActionCommand && row >= 0) {
			_log.line(Common::String::format("Mirror command: %s", _panel.commandText(row) ? _panel.commandText(row) : "(none)"));
			switch (_rowActions[row]) {
			case kRowRestart:
				startNewGame();
				break;
			case kRowLoadMenu:
				openMenu(kMenuLoad);
				break;
			case kRowSaveMenu:
				openMenu(kMenuSave);
				break;
			case kRowSaveSlot:
				saveSlot((uint)_rowArguments[row]);
				break;
			case kRowLoadSlot:
				loadSlot((uint)_rowArguments[row]);
				break;
			case kRowMenuBack:
			case kRowCancelExit:
				openMenu(kMenuNone);
				break;
			case kRowExitGame:
				openMenu(kMenuQuit);
				break;
			case kRowConfirmExit:
				_log.line("Options: exit confirmed");
				_quitRequested = true;
				break;
			case kRowMirrorAway:
				showRoom(_world.room());
				break;
			default:
				break;
			}
		} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			if (_menu != kMenuNone)
				openMenu(kMenuNone);
			else
				showRoom(_world.room());
		}
		return false;
	}
	if (_mode == kTalk) {
		int row = -1, arrow = -1;
		const Panel::Action action = event.type == Common::EVENT_LBUTTONDOWN ?
				_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) : Panel::kActionNone;
		if (action >= Panel::kActionUp && action <= Panel::kActionLeft && event.mouse.y >= 152 &&
				!_talkBargain && _talkKind == kTalkNormal && !_visionDream) {
			// A compass arrow ends the talk and walks on, even while a line
			// is up (checked on Spice86: Carthag-Harg's chief, "My troop is
			// settled...", then the down arrow leads to the exterior).
			_log.line("Tap: compass during a talk: the talk ends");
			endConversation();
			if (_mode == kRoom)
				panelAction(action, row, arrow);
			return false;
		}
		if (action == Panel::kActionCommand && row >= 0 && row < (int)Panel::kCommandRows) {
			_log.line(Common::String::format("Talk command: %s", _panel.commandText(row) ? _panel.commandText(row) : "(none)"));
			switch (_rowActions[row]) {
			case kRowTalkMore:
				if (_talkEnded) {
					_talkEnded = false;
					_conversation->start(MIN<uint>(_talkWho, World::kFremenChief));
				}
				advanceConversation();
				break;
			case kRowStopTalking:
				endConversation();
				break;
			case kRowBargain:
				answerQuestion((byte)_rowArguments[row]);
				break;
			case kRowWhat:
				// " WHAT ? " (seg000:9ed5): the last line again.
				if (!_talkLastPage.empty()) {
					_talkLines.clear();
					_talkLine = 0;
					_panel.wrapText(_talkLastPage, 208 - 2 * 16, false, _talkLines);
					const bool ended = _talkEnded;
					_talkEnded = false;
					drawTalk();
					_talkEnded = ended;
				}
				break;
			case kRowContinue:
				advanceConversation();
				break;
			case kRowViewed:
				endConversation();
				break;
			case kRowWorkForMe:
				workForMe();
				break;
			case kRowGiveOrders: {
				// seg000:5a03: the map opens on the troop behind this chief.
				Common::Array<uint> ids;
				_world.troopsAt(_world.currentLocation(), ids);
				uint k = _talkWho - World::kFremenChief;
				for (uint i = 0; i < ids.size(); ++i) {
					const Troop t = _world.troop(ids[i]);
					if (!t.harkonnen() && t.hired() && k-- == 0) {
						delete _talkSheet;
						_talkSheet = nullptr;
						_recruiting = false;
						openMap(MapScreen::kFlat, false);
						// The detour bumps map_view_reentry_count: no DUNE MAP popup (seg000:5a03).
						_map->setCaption(false);
						openTroop(ids[i], true);
						_troopFromRoom = true;
						break;
					}
				}
				break;
			}
			case kRowCompanion:
				companionVerb();
				break;
			default:
				break;
			}
			return false;
		}
		if ((event.type == Common::EVENT_LBUTTONDOWN && event.mouse.y < 152) ||
				(event.type == Common::EVENT_KEYDOWN && event.kbd.keycode != Common::KEYCODE_ESCAPE)) {
			_log.line(Common::String::format("Tap: conversation page %u", _talkPage));
			// In the original a click after the last line closes the talk and
			// the room's rows come back (checked on Spice86: Leto's three
			// pages, a WORK FOR ME answer); the verbs are offered only while a
			// line is up. The bargain menu, COMM messages and scenes keep theirs.
			const bool closes = !_talkBargain && _talkKind == kTalkNormal && !_visionDream;
			if (_talkBargain)
				; // the bargaining menu waits for its answer (a view click does nothing)
			else if (!_talkEnded)
				advanceConversation();
			else if (closes)
				endConversation();
			// The line just shown may have opened the bargain menu (events 4/5).
			if (closes && !_talkBargain && _mode == kTalk && _talkEnded)
				endConversation();
		} else if (event.type == Common::EVENT_KEYDOWN) {
			endConversation();
		}
		return false;
	}

	if (event.type == Common::EVENT_KEYDOWN) {
		switch (event.kbd.keycode) {
		case Common::KEYCODE_UP:
			panelAction(Panel::kActionUp, -1, -1);
			break;
		case Common::KEYCODE_RIGHT:
			panelAction(Panel::kActionRight, -1, -1);
			break;
		case Common::KEYCODE_DOWN:
			panelAction(Panel::kActionDown, -1, -1);
			break;
		case Common::KEYCODE_LEFT:
			panelAction(Panel::kActionLeft, -1, -1);
			break;
		default:
			break;
		}
	} else if (event.type == Common::EVENT_LBUTTONDOWN && event.mouse.y < 152 && !_desert && !_sceneActive &&
			_menu == kMenuNone && _commList < 0) {
		// The original ignores a click in the view unless it lands on
		// someone, whose talk it starts (checked on Spice86 in the day-1
		// tour: clicks in the throne room's view do nothing). That includes
		// Paul's head above the panel: the globe is reached from the map
		// (the user chose the original's way on 2026-09-28).
		const int who = personAt(event.mouse.x, event.mouse.y);
		_log.line(Common::String::format("Tap: room %u view at (%d, %d) -> %s", _room, event.mouse.x, event.mouse.y,
				who >= 0 ? characterName((uint)who) : "nothing"));
		if (who >= 0)
			startConversation((uint)who);
	} else if (event.type == Common::EVENT_LBUTTONDOWN) {
		int row, arrow;
		const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrow);
		// Taps are logged with what they hit: on a device this is the only
		// way to tell a touch-mapping problem from a hit-zone problem.
		static const char *const names[] = { "nothing", "up", "right", "down", "left", "book", "command", "head", "plan" };
		_log.line(Common::String::format("Tap: room %u at (%d, %d) -> %s%s%s", _room, event.mouse.x, event.mouse.y, names[action],
				action == Panel::kActionCommand && row >= 0 && _panel.commandText(row) ? " " : "",
				action == Panel::kActionCommand && row >= 0 && _panel.commandText(row) ? _panel.commandText(row) : ""));
		panelAction(action, row, arrow);
	}

	return false;
}

} // namespace Dune

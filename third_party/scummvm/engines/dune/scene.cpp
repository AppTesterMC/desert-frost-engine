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

#include "common/events.h"
#include "common/random.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/paletteman.h"

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
	return (isDumpRun() && !dumpEveryMillis()) || isDuneHarnessRun();
}

} // namespace

namespace {

// GLOBDATA is the original globe outline stream plus two 64x100 lookup
// tables, not a flat bitmap. This is the recovered vga_globe_setup path from
// dune-re, kept here so SEE DUNE MAP renders the actual resource data.
enum GlobeSection { kFarNorth, kNearNorth, kNearSouth, kFarSouth };
struct GlobeSectionLatitude { GlobeSection section; byte latitude; };

bool drawRecoveredGlobe(Graphics::Surface &surface, const Common::Array<byte> &globdata,
		const Common::Array<byte> &map, const Common::Array<byte> &tablat, uint16 rotation, int tilt) {
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
	tilt = CLIP(tilt, -96, 96);

	auto mapColour = [&](int offset) -> byte {
		const int address = (int)kMapStart + offset;
		if (address < 0 || address >= (int)map.size())
			return 0;
		return (map[address] & 0x0f) + 0x10;
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

} // namespace

bool drawDuneGlobe(OSystem *system, Graphics::Surface &surface, Resource &resources, uint16 rotation, int tilt) {
	Common::Array<byte> mapData, globeData, tablatData, freskData;
	if (!resources.load("MAP.HSQ", mapData) || !resources.load("GLOBDATA.HSQ", globeData)
			|| !resources.load("TABLAT.BIN", tablatData) || !resources.load("FRESK.HSQ", freskData))
		return false;
	Sprite fresk(system, freskData);
	if (!fresk.setPalette())
		return false;
	fresk.drawFrame(0, &surface, 0, 0);
	fresk.drawFrame(1, &surface, 214, 0);
	fresk.drawFrame(2, &surface, 91, 20);
	const bool ok = drawRecoveredGlobe(surface, globeData, mapData, tablatData, rotation, tilt);
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
		_saves(nullptr), _menu(kMenuNone), _menuStatus(0xffff), _music(nullptr), _musicOn(false),
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
	_commList = -1;
	_troopEquipment = false;
	_state.newGame();
	_world.loadInitialData();
	_world.prepareNewGame();
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
	if (_mode == kRoom)
		drawRoom(); // the day counter, and the sky when the hour turned
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
	_mode = kRoom;
	_menu = kMenuNone;
	_desert = false;
	_commList = -1;
	_world.setPosition(_world.currentLocation(), number);
	_room = number;
	refreshRooms();
	updateRoomVars();
	if (_world.placeType() == Location::kHarkonnenPalace && number == 2 && !isFastCapture() &&
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
	if (!fromDesert) {
		if (_world.room() != 1 && parkedOrnis())
			_world.setPosition(from, 1);
		_room = _world.room();
		refreshRooms();
		animateOrni(+1);
		_world.setOrnithopters(from, -1);
	}
	const uint arrived = flyToward(longitude, latitude, -1);
	if (arrived == kDesertLanding) {
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
	_world.setOrnithopters(arrived, +1);
	_room = 1;
	refreshRooms();
	animateOrni(-1);
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
	if (from != locationIndex && !fromDesert) {
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
		_world.setOrnithopters(arrived, +1);
		_room = 1;
		refreshRooms();
		if (!_world.floppy() && !isFastCapture())
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
	if (_finalPicture) {
		showFinal(_finalPicture);
		return;
	}
	if (_desert) {
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
		const bool outdoors = palace ? (salRoom == 10 || salRoom == 11)
									 : (floppy ? (slot == 6 || slot == 8 || slot == 9) : slot == 0);
		const bool video = !floppy && outdoors && !(palace && salRoom == 10);
		if (video) {
			if (!drawVideoBackdrop(placeType))
				_log.line(Common::String::format("Room: %s backdrop missing", World::arrivalVideo(placeType)));
			_panel.applyPalette(); // the interface colours over the video's
		} else if (outdoors) {
			if (palace && salRoom == 11)
				drawSky(_system, _resources, *_surface.surfacePtr(), kSkyLarge, 200, skyPalette());
			else
				drawSky(_system, _resources, *_surface.surfacePtr(), kSkyNarrow, 320, skyPalette());
			if (!palace)
				_surface.fillRect(Common::Rect(0, 78, 320, 152), 190);
		}

		Sprite sheet(_system, sheetData);
		Sprite characters(_system, _panel.characterSheet());
		// The characters stand at the room's markers as the executable
		// places them (sub_13D83 / sub_13DF4 / sub_13D2F): each person, in
		// ascending group order, takes slot (group + ds:0xC7) mod markers or
		// else the first free slot; the markers then read the slots from the
		// last one down. PERS frame 2 x group; groups from 15 on (the troop
		// chiefs) are all drawn as 15. ds:0xC7 is 0 in both executables.
		Common::Array<uint16> markerSprites;
		{
			Common::Array<byte> people;
			_world.peopleInRoom(people);
			const uint markers = MIN<uint>(Room(roomData).markerCount(salRoom), 23);
			byte slots[23];
			memset(slots, 0xff, sizeof(slots));
			const uint rotation = _state.b(0xc7) & 0x0f;
			if (_sceneActive && !_cast.empty()) {
				// A scripted scene places its cast itself (scene action 00:
				// sal_read_position_markers copies the list verbatim).
				for (uint i = 0; i < markers && i < _cast.size(); ++i)
					slots[i] = _cast[i];
				people.clear();
			}
			for (uint i = 0; i < people.size() && markers; ++i) {
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
			for (uint j = 0; j < markers; ++j) {
				const byte who = slots[markers - 1 - j];
				if (who != 0xff) {
					// A Fremen stands as PERS 14-16 by its troop (seg000:913b).
					const uint troop = _world.troopForPerson(who);
					const uint figure = troop ? World::kFremen + World::fremenHead(troop) : MIN<uint>(who, World::kFremenChief);
					markerSprites[j] = (uint16)(2 * figure);
					if (who < ARRAYSIZE(_personPos) && j < positions.size())
						_personPos[who] = positions[j];
				}
			}
		}

		// The exterior sheets (SIET0, VILG, FORT, BALCON) carry no palette of
		// their own and take the sky's colours; PERS's went in first through
		// the panel, so nothing here disturbs the sky.
		if (!sheet.setPalette())
			_log.line(Common::String::format("Room: %s has a broken palette chunk", sheetName.c_str()));
		if (floppy && palace && outdoors)
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
		if (!ok)
			_log.line(Common::String::format("Room: %s #%u did not parse", World::salFile(placeType), salRoom));
	}

	// Colour 0 is the black behind the view and the command box; several
	// room sheets set it to something else.
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);

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
		add(kRowCommCancel, 0, "  Cancel");
		return;
	}
	const byte phase = _state.b(GameState::kPhase);
	add(kRowMap, 0, "SEE DUNE MAP");
	if (_desert) {
		// Outside a place (seg000:2faa): CALL A WORM (greyed before phase
		// 0x4f; worms are not built), WAIT FOR EVENING before period 11, else
		// WAIT FOR MORNING. The way back is the ornithopter parked beside
		// Paul (its hotspot, person 0x2f, gives TAKE AN ORNITHOPTER).
		add(kRowWorm, 0, "CALL A WORM", true);
		if (_world.timeSlot() < 11)
			add(kRowWait, 0, "WAIT FOR EVENING");
		else
			add(kRowWait, 1, "WAIT FOR MORNING");
		add(kRowOrnithopter, 0, "TAKE AN ORNITHOPTER");
		return;
	}
	const bool palace = _world.placeType() == Location::kPalace;
	(void)canLeave;
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
	(void)phase;
	// build_persons_in_room_records: the people present.
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	for (uint i = 0; i < people.size(); ++i)
		add(kRowTalk, people[i], characterName(people[i]));
}

void GameScreen::drawRoom(int pressedRow, int pressedArrow) {
	composeView();
	const RoomRecord *record = currentRoom();

	bool exits[4] = { false, false, false, false };
	bool canLeave = false;
	if (record) {
		for (uint direction = 0; direction < 4; ++direction) {
			// Bit 7 marks a door still hidden: the story opens it (the phase
			// callbacks clear the bit, seg000:1027 and following).
			const byte e = record->exits[direction];
			exits[direction] = e != 0 && (e >= World::kExitLeave || !(e & 0x80));
			if (record->exits[direction] >= World::kExitLeave)
				canLeave = true;
		}
	}

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
	addRoomRows(actions, arguments, commands, greyed, count, canLeave);
	setRows(actions, arguments, commands, count);
	for (uint i = 0; i < count; ++i)
		if (greyed[i])
			_panel.setRowDisabled(i, true);
	if (_sceneActive || _desert)
		for (uint d = 0; d < 4; ++d)
			exits[d] = false;
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	_panel.draw(_surface, exits, pressedRow, pressedArrow, day());

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
		case kRowOrnithopter:
			openMap(MapScreen::kFlat, true);
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

	const RoomRecord *record = currentRoom();
	if (!record)
		return;
	const byte exit = record->exits[action - Panel::kActionUp];
	if (!exit || (exit < World::kExitLeave && (exit & 0x80)))
		return;
	if (exit >= World::kExitLeave) {
		// 252-254 leave the place (by foot, ornithopter or worm: not decoded);
		// the map chooses the destination.
		openMap(MapScreen::kFlat, true);
		return;
	}
	if (row >= 0 || arrow >= 0) {
		// Show the pressed control for a moment, as the original does.
		drawRoom(row, arrow);
		_system->delayMillis(120);
	}
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
	_state.setB(0x0c, exit & 0x7f);
	_state.setB(0x23, 5);
	showRoom(exit & 0x7f);
	if (_mode == kRoom)
		roomEntryScan();
}

// ---- The map and the globe --------------------------------------------------

void GameScreen::openMap(MapScreen::Mode mode, bool selectDestination) {
	loadDialogue(); // the sentences: place names and the box text
	if (!_map)
		_map = new MapScreen(_system, _resources, _log, _world);
	if (!_map->open(mode, selectDestination)) {
		showStatus("Dune: map data missing");
		return;
	}
	_mode = kMap;
	debugSetScene(mode == MapScreen::kFlat ? "map/flat" : "map/globe");
	drawMapScreen();
	dumpScreen(_system, mode == MapScreen::kFlat ? (selectDestination ? "map-select" : "map-flat") : "map-globe");
}

void GameScreen::drawMapScreen() {
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
		if (_map->selecting() && (_map->destination() >= 0 || _map->destination() == -2))
			add(kRowFly, _map->destination(), "GO THERE FLYING AN ORNI");
		if (_world.contactRange() < 2)
			add(kRowOrders, 0, "GIVE ORDERS TO TROOP", false, !hiredTroopAt(_world.currentLocation()));
		else
			add(kRowContact, 0, "CONTACT FREMEN TROOPS", false, _state.b(GameState::kFremenTroops) == 0);
		add(kRowDensity, 0, _map->density() ? "STANDARD VISION" : "SEE SPICE DENSITY", false, phase < 5);
		add(kRowOrnithopter, 0, "TAKE AN ORNITHOPTER", false, parkedOrnis() == 0);
		if (phase >= 5)
			add(kRowProspectors, 0, "FIND PROSPECTORS"); // seg000:5b1e
	} else if (_menu == kMenuNone) {
		// swift-dune Fresk.swift: the globe screen carries the game menu.
		// swift-dune Fresk.swift: EXIT GLOBE, SEE RESULTS (STANDARD VISION
		// while the results show), SAVE GAME, LOAD GAME, OPTIONS.
		add(kRowExitMap, 0, "EXIT GLOBE");
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
		// The four logs of the original: two free ones and the automatic
		// "last entering" saves, then the way back.
		static const char *const kSlots[SaveGame::kSlots] = {
			"Log 1:", "Log 2:", "LAST ENTERING INTO A PLACE", "LAST ENTERING NEW SIETCH"
		};
		for (uint slot = 0; slot < SaveGame::kSlots; ++slot)
			add(_menu == kMenuSave ? kRowSaveSlot : kRowLoadSlot, (int)slot, kSlots[slot], true);
		add(kRowExitMap, 0, "EXIT GLOBE");
	} else if (_menu == kMenuOptions) {
		add(kRowMusic, 0, _musicOn ? "MUSIC OFF" : "MUSIC ON (GAME RELATIVE)");
		add(kRowRestart, 0, "RESTART GAME");
		add(kRowExitGame, 0, "EXIT GAME");
		add(kRowExitMap, 0, "EXIT GLOBE");
	} else {
		add(kRowConfirmExit, 0, "YES I WANT TO EXIT GAME");
		add(kRowCancelExit, 0, _panel.findCommand("NO I WISH TO CONTINUE") != 0xffff ? "NO I WISH TO CONTINUE"
																				  : "NO I DON'T WANT TO FINISH");
	}
	setRows(actions, arguments, commands, count);
	for (uint i = 0; i < count; ++i)
		if (greyed[i])
			_panel.setRowDisabled(i, true);
	if (_menu == kMenuSave || _menu == kMenuLoad) {
		for (uint slot = 0; slot < 2 && slot < count; ++slot)
			_panel.setRowText(slot, slotLabel(slot));
		if (_menuStatus != 0xffff && count > SaveGame::kSlots) {
			// " SAVE SUCCESSFUL" / " *** SAVE ERROR " replace the last row for one look.
			_rowActions[SaveGame::kSlots] = kRowNone;
			_panel.setRowText(SaveGame::kSlots, _panel.commandString(_menuStatus));
		}
	}
	_panel.setLeftPanel(Panel::kLeftGlobe);
	const bool exits[4] = { false, false, false, false };
	_panel.draw(_surface, exits, -1, -1, day());
	_map->drawPanelExtras(_surface, _panel);
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

void GameScreen::mapTap(int x, int y) {
	if (_mode != kMap || !_map)
		return;
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
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	_menu = kMenuNone;
	_menuStatus = 0xffff;
	showRoom(_world.room());
}

// ---- Flight, results, troops ------------------------------------------------------

namespace {

/**
 * The floppy's flight view: the desert seen from the ornithopter, dune
 * pieces (DUNES.HSQ) streaming from the horizon under the sky of the hour,
 * as the intro's flight (Flight.swift) draws it; the gameplay recording shows
 * the same view with the minimap in the top right corner.
 */
struct DesertFlight {
	struct Piece {
		uint16 sprite;
		int16 endX, endY;
		uint32 born;
	};
	Sprite *dunes = nullptr;
	Common::Array<Piece> pieces;
	Common::RandomSource rng{"dune-game-flight"};
	uint32 nextSpawn = 0;
	bool evenSet = true;

	void render(Graphics::Surface &view, uint32 elapsed) {
		const int originX = 160, originY = 70, radius = 300;
		view.fillRect(Common::Rect(0, 78, 320, 152), 190);
		while (elapsed >= nextSpawn) {
			uint rays[7];
			uint rayCount = 0;
			for (uint k = evenSet ? 0 : 1; k < 13; k += 2)
				rays[rayCount++] = k;
			evenSet = !evenSet;
			for (uint i = 0; i < 5 && rayCount; ++i) {
				const uint pick = rng.getRandomNumber(rayCount - 1);
				const uint ray = rays[pick];
				rays[pick] = rays[--rayCount];
				const double angle = (ray == 12 ? 11.5 : (ray == 0 ? 0.5 : (double)ray)) * M_PI / 12.0;
				Piece p;
				p.sprite = (uint16)rng.getRandomNumber(7);
				p.endX = (int16)(originX + radius * cos(angle));
				p.endY = (int16)(originY + radius * sin(angle));
				p.born = nextSpawn;
				pieces.push_back(p);
			}
			nextSpawn += 1000;
		}
		for (uint i = 0; i < pieces.size();) {
			const Piece &p = pieces[i];
			const double t = (elapsed - p.born) / 1000.0;
			const double progress = MIN(1.0, t * t * t / 2.0);
			const int x = originX + (int)((p.endX - originX) * progress);
			const int y = originY + (int)((p.endY - originY) * progress);
			if (y >= 152 || progress >= 1.0) {
				pieces.remove_at(i);
				continue;
			}
			++i;
			const int scalePercent = (int)(120.0 * (y - 80) / 72.0);
			uint16 width, height;
			if (y < 78 || scalePercent <= 0 || !dunes->frameSize(p.sprite, width, height))
				continue;
			dunes->drawFrameScaled(p.sprite, &view, x - width * scalePercent / 100, y, 25600 / (uint)scalePercent);
		}
	}
};

} // namespace

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
	// at +(-81,-3). The sheet has no palette: it wears the room's colours.
	Common::Array<byte> data;
	if (!_resources.load("ORNYTK.HSQ", data))
		return;
	Sprite orni(_system, data);
	orni.drawFrame(0, &target, x, y);
	orni.drawFrame(1, &target, x + 6, y + 30);
	orni.drawFrame((uint16)(2 + CLIP<int>((int)frame - 15, 0, 5)), &target, x + 4, y + 50);
	orni.drawFrame((uint16)(8 + MIN<uint>(frame, 14)), &target, x - 81, y - 3);
}

void GameScreen::drawParkedOrnis(Graphics::Surface &target, uint skip) {
	// get_orni_position (seg000:3a95): the pad at (149, 57) for a sietch,
	// (202, 73) elsewhere; each further orni 70 to the right, 10 lower.
	const bool sietch = _world.placeType() < Location::kPalace;
	int x = sietch ? 0x95 : 0xca, y = sietch ? 0x39 : 0x49;
	const uint count = parkedOrnis();
	for (uint i = 0; i < count; ++i, x += 0x46, y += 0x0a)
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
	const int padX = sietch ? 0x95 : 0xca, padY = sietch ? 0x39 : 0x49;
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
		_system->delayMillis(100);
	}
	clean.free();
}

void GameScreen::openMirror() {
	// menu_callback_choice_palace_look_at_mirror (seg000:0ea6): the game
	// clock stops and the mirror still comes up.
	_mode = kMirror;
	_log.line("Mirror: Paul looks at himself");
	drawMirror();
	dumpScreen(_system, "mirror");
}

void GameScreen::drawMirror() {
	// callback_transition_look_at_mirror (seg000:0ed0): MIRROR.HSQ's
	// reflected bedroom (frame 1), Paul's face, then the gilt frame (2);
	// the menu is RESTART / LOAD / SAVE / EXIT GAME and Look away (ds:1d1e).
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	Common::Array<byte> data, paul;
	if (_resources.load("MIRROR.HSQ", data)) {
		Sprite mirror(_system, data);
		mirror.setPalette();
		mirror.drawFrame(1, _surface.surfacePtr(), 0, 0);
		if (_resources.load("PAUL.HSQ", paul)) {
			Sprite face(_system, paul);
			face.setPalette();
			// Centred in the frame; Paul ages with the clock (seg000:917a).
			const uint expression = MIN<uint>((uint)(_state.w(GameState::kGameTime) >> 6), 8u) * 2;
			if (!face.drawAnimationFrame(expression, 0, _surface.surfacePtr(), 86, 0))
				face.drawAnimationFrame(0, 0, _surface.surfacePtr(), 86, 0);
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
	add(kRowRestart, "RESTART GAME");
	add(kRowLoadMenu, "LOAD GAME");
	add(kRowSaveMenu, "SAVE GAME");
	add(kRowExitGame, "EXIT GAME");
	add(kRowMirrorAway, "Look away from the mirror");
	setRows(actions, arguments, commands, count);
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	const bool exits[4] = { false, false, false, false };
	_panel.draw(_surface, exits, -1, -1, day());
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
	// DESTINATION, seg000:4ffb). Passing near a sietch the story lets the
	// player find offers GO TOWARDS THIS PLACE / RESUME FLIGHT (seg000:40f9).
	// The CD's MNT videos and the floppy's cockpit are not shown yet.
	const uint from = _world.currentLocation();
	int destination = target;
	if ((int)from == destination)
		return from;
	Location a = _world.location(from), b = a;
	b.longitude = targetLongitude;
	b.latitude = targetLatitude;
	int lng = a.longitude, lat = a.latitude;
	uint total = _world.cellDistance(a.longitude, a.latitude, b.longitude, b.latitude);
	uint step = 0, steps = MAX<uint>(1, total);
	uint hostileSteps = 0;
	bool skipping = isFastCapture();
	uint declined = 0xffff;
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
	DesertFlight desert;
	Sprite *dunes = nullptr;
	if (!skipping && _resources.load("DUNES.HSQ", dunesData)) {
		dunes = new Sprite(_system, dunesData);
		desert.dunes = dunes;
	}
	const uint32 flightStart = _system->getMillis();
	const bool cdView = !skipping && !dunes && !_world.floppy() && startCdFlightView();
	auto present = [&]() {
		if (cdView) {
			// The terrain six cells ahead on the route (travel_probe_terrain_ahead, seg000:4e8e).
			const uint ahead = MIN<uint>(steps, step + 6);
			const uint16 aLng = (uint16)(a.longitude + (int)(int16)(b.longitude - a.longitude) * (int)ahead / (int)steps);
			const int16 aLat = (int16)(a.latitude + (b.latitude - a.latitude) * (int)ahead / (int)steps);
			const int c0 = _world.mapCell((uint16)lng, (int16)lat), c1 = _world.mapCell(aLng, aLat);
			const Common::Array<byte> &m = _world.map();
			const byte t0 = c0 >= 0 ? (m[c0] & 0x0f) : 0, t1 = c1 >= 0 ? (m[c1] & 0x0f) : 0;
			drawCdFlightView((byte)((t0 + t1) / 2));
			_map->drawMinimap(_surface, Common::Rect(202, 3, 318, 61), _panel);
			RowAction actions[Panel::kCommandRows] = { kRowNone, kRowNone };
			int arguments[Panel::kCommandRows] = { 0, 0 };
			uint16 commands[Panel::kCommandRows] = { _panel.findCommand("SKIP TO DESTINATION"), _panel.findCommand("CHANGE DESTINATION") };
			setRows(actions, arguments, commands, 2);
			_panel.setRowDisabled(1, true);
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
		drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette());
		desert.render(view, _system->getMillis() - flightStart);
		_map->drawMinimap(_surface, Common::Rect(202, 3, 318, 61), _panel);
		// The flight's verbs (seg000:4ffb, 497a).
		RowAction actions[Panel::kCommandRows] = { kRowNone, kRowNone };
		int arguments[Panel::kCommandRows] = { 0, 0 };
		uint16 commands[Panel::kCommandRows] = { _panel.findCommand("SKIP TO DESTINATION"), _panel.findCommand("CHANGE DESTINATION") };
		setRows(actions, arguments, commands, 2);
		_panel.setRowDisabled(1, true);
		_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
		const bool exits[4] = { false, false, false, false };
		_panel.draw(_surface, exits, -1, -1, day());
		_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
		_system->updateScreen();
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
			desert.dunes = dunes;
			_map->addFlightTrail(a.longitude, a.latitude);
			_map->setFlight(true, (uint16)(a.longitude + (int16)(b.longitude - a.longitude) / 3),
					(int16)(a.latitude + (b.latitude - a.latitude) / 3), destination);
			desert.render(*_surface.surfacePtr(), 0); // seeds the pieces; the frame below shows them in flight
			desert.pieces.clear();
			desert.nextSpawn = 0;
			const uint32 frozen = 1000;
			Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
			_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
			_panel.applyPalette();
			drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette());
			desert.render(view, frozen);
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
	for (;;) {
		if (step >= steps)
			break;
		if (!skipping) {
			while (_system->getMillis() < next && !skipping && !_quitRequested) {
				Common::Event event;
				while (pollDuneEvent(_system, event)) {
					if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
						_quitRequested = true;
					else if (event.type == Common::EVENT_LBUTTONDOWN) {
						int row, arrow;
						// SKIP TO DESTINATION, or a tap on the view.
						if (event.mouse.y < 152 ||
								(_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) == Panel::kActionCommand && row == 0))
							skipping = true;
					}
				}
				if (dunes || cdView)
					present();
				_system->delayMillis(dunes || cdView ? 40 : 10);
			}
			next += World::kFlightStepMillis;
		}
		if (_quitRequested)
			break;
		// One cell along the straight line (the executable re-aims its
		// heading every step; a straight line is what that converges to).
		++step;
		const uint16 stepLng = (uint16)(a.longitude + (int)(int16)(b.longitude - a.longitude) * (int)step / (int)steps);
		const int16 stepLat = (int16)(a.latitude + (b.latitude - a.latitude) * (int)step / (int)steps);
		lng = stepLng;
		lat = stepLat;
		if (step % 16 == 0) {
			_world.advanceTime(1);
			++periods;
		}
		// travel_route_hostile_zone_check (seg000:4182): flying to a place
		// that is not the Atreides', every step over Harkonnen land (cell
		// stage 0x30) takes 0x20 from the accumulator; the first warns
		// (ENTERING HARKONNEN ZONE, and SKIP TO DESTINATION stops), the
		// eighth in a row brings the ornithopter down (room screen 2).
		if (!isFastCapture()) {
			const bool safe = destination >= 0 && _world.friendlyPlace((uint)destination);
			if (!safe && _world.cellStage((uint16)lng, (int16)lat) == 0x30) {
				if (!hostileSteps) {
					skipping = false;
					if (!dunes)
						present();
					if (!askHostileZone()) {
						// BACK TO STARTING POINT: re-aimed at the place left.
						destination = (int)from;
						a = _world.location(from);
						a.longitude = (uint16)lng;
						a.latitude = (int16)lat;
						b = _world.location(from);
						steps = MAX<uint>(1, _world.cellDistance(a.longitude, a.latitude, b.longitude, b.latitude));
						step = 0;
					}
					next = _system->getMillis() + World::kFlightStepMillis;
				}
				if (++hostileSteps >= 8) {
					_log.line("Flight: shot down over Harkonnen land");
					if (_map)
						_map->setFlight(false, 0, 0, -1);
					delete dunes;
					return kShotDown;
				}
			} else {
				hostileSteps = 0;
			}
		}
		if (!skipping) {
			_map->addFlightTrail((uint16)lng, (int16)lat);
			_map->setFlight(true, (uint16)lng, (int16)lat, destination);
			present();
		}
		// A findable sietch within four cells (the 9x9 block of seg000:40f9).
		for (uint i = 0; i < _world.locationCount() && !skipping; ++i) {
			if ((int)i == destination || i == declined || !_world.discoverable(i))
				continue;
			const Location s = _world.location(i);
			if (_world.cellDistance((uint16)lng, (int16)lat, s.longitude, s.latitude) > 4)
				continue;
			if (askFlightStop(i)) {
				destination = (int)i;
				a = _world.location(from);
				a.longitude = (uint16)lng;
				a.latitude = (int16)lat;
				b = s;
				steps = MAX<uint>(1, _world.cellDistance(a.longitude, a.latitude, b.longitude, b.latitude));
				step = 0;
			} else {
				declined = i;
			}
			next = _system->getMillis() + World::kFlightStepMillis;
			break;
		}
	}
	if (_map)
		_map->setFlight(false, 0, 0, -1);
	delete dunes;
	stopCdFlightView();
	_clockStart = _system->getMillis();
	if (destination < 0) {
		// A flight to a point of the desert lands there (current_scene
		// 0xff); the dump and harness runs keep the old nearest-place end.
		if (!isFastCapture()) {
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
	_log.line(Common::String::format("Flight: place %u -> %d, %u cells, %u period(s)", from, destination, total, periods));
	return (uint)destination;
}

bool GameScreen::askFlightStop(uint sietch) {
	// pending_room_action 3 (seg000:3555): the verbs GO TOWARDS THIS PLACE / RESUME FLIGHT.
	RowAction actions[Panel::kCommandRows];
	int arguments[Panel::kCommandRows];
	uint16 commands[Panel::kCommandRows];
	uint count = 0;
	const uint16 go = _panel.findCommand("GO TOWARDS THIS PLACE"), resume = _panel.findCommand("RESUME FLIGHT");
	if (go == 0xffff || resume == 0xffff)
		return false;
	actions[count] = kRowFly;
	arguments[count] = (int)sietch;
	commands[count++] = go;
	actions[count] = kRowNone;
	arguments[count] = 0;
	commands[count++] = resume;
	setRows(actions, arguments, commands, count);
	_panel.setLeftPanel(Panel::kLeftGlobe);
	const bool exits[4] = { false, false, false, false };
	_map->draw(_surface, _panel, _sentences, _state.b(GameState::kFremenTroops));
	_panel.draw(_surface, exits, -1, -1, day());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
	_log.line(Common::String::format("Flight: passing near place %u", sietch));
	dumpScreen(_system, "flight-sietch-near");
	for (;;) {
		Common::Event event;
		while (pollDuneEvent(_system, event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				_quitRequested = true;
				return false;
			}
			if (event.type != Common::EVENT_LBUTTONDOWN)
				continue;
			int row, arrow;
			if (_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) == Panel::kActionCommand && row >= 0 && row < 2)
				return row == 0;
		}
		_system->delayMillis(10);
	}
}

bool GameScreen::askHostileZone() {
	// pending_room_action 4 (seg000:41ae): "  ****  WARNING  ****
	// ENTERING HARKONNEN ZONE" over the view; the flight goes on or turns
	// back (the original also offers CHANGE DESTINATION, not built here).
	// Returns true to resume.
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
	if (place >= _world.locationCount() || isFastCapture())
		return false;
	const Location l = _world.location(place);
	if (!(l.status & 2) && _world.friendlyPlace(place))
		return false;
	uint harkonnen = 0, attacking = 0;
	_world.countHostiles(place, harkonnen, attacking);
	if (attacking) {
		_log.line(Common::String::format("Arrival: place %u is under attack (the night battle is not built)", place));
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
		{ 216, 6, kTitle, Common::String::format("CHARISMA = %u", _state.b(World::kCharisma)) },
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
	// The duration phrase (sub_132c7; its thresholds are not transcribed):
	// "for a very short time" ... "for 12 days", or "but our job is finished".
	const uint periods = (uint16)(_state.w(GameState::kGameTime) - READ_LE_UINT16(r + 0x0a));
	const uint step = (t.occupation & Troop::kStopped) ? 4 : periods < 4 ? 0 : periods < 16 ? 1 : periods < 96 ? 2 : 3;
	_state.setW(names + 10, (uint16)(durations + step));
	_state.setW(names + 12, (uint16)(ranks + MIN<uint>(5, t.spiceSkill >> 4)));
	_state.setW(names + 14, (uint16)(ranks + MIN<uint>(5, t.armySkill >> 4)));
	_state.setW(names + 16, (uint16)(ranks + MIN<uint>(5, t.ecologySkill >> 4)));
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
	if (_troopFromMap && _map) {
		_map->draw(_surface, _panel, _sentences, _state.b(GameState::kFremenTroops));
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
			uint16 c[1] = { _panel.findCommand("  Done") };
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
		int y = panel.top + 3 + (63 - (int)MIN<uint>(lines.size(), 6) * lineHeight) / 2;
		for (uint i = 0; i < lines.size() && i < 6; ++i, y += lineHeight)
			_panel.drawText(_surface, lines[i].c_str(), panel.left + 0x49 + 4, y, Panel::kDarkColour, false);

		// menu_map_troop_dialog: the contact verbs (greyed when they cannot apply).
		if (!_troopChoosing) {
			add(kRowAskMore, 0, "ASK FOR MORE INFORMATION");
			add(kRowTroopOccupation, 0, job == Troop::kWaitingForOrders ? "SELECT TROOP OCCUPATION" : "CHANGE TROOP OCCUPATION");
			add(kRowEquipment, 0, "MODIFY EQUIPMENT");
			add(kRowNone, 0, "MOVE TROOP", true);
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
			switch (job & 0x0c) {
			case 0:
				add(kRowSetOccupation, Troop::kSpiceMining, "Spice Mining");
				add(kRowSetOccupation, Troop::kProspecting, "Spice Prospecting");
				break;
			case 4:
				add(kRowSetOccupation, Troop::kMilitaryTraining, "Military Training");
				add(kRowSetOccupation, Troop::kEspionage, "Espionage");
				break;
			default:
				// menu_map_troop_change_troop_occupation_for_ecology_troop
				// (ds:21a6): GO & SEARCH FOR EQUIPMENT (a march; not built),
				// ASSEMBLY WIND-TRAP (job | 1, seg000:6a2b), or another speciality.
				add(kRowNone, 0, "GO & SEARCH FOR EQUIPMENT", true);
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
	const uint recruit = _world.localTroop(false);
	_talkRecruitOk = _world.troopAgreesToFollow(recruit);
	_state.setB(0x23, _talkRecruitOk ? 0 : 2);
	_talkRecruit = recruit;
	presentVerb(5);
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
	if (_mode != kMap || !_map || _map->mode() != MapScreen::kGlobe) {
		openMap(MapScreen::kGlobe, false);
		if (_mode != kMap)
			return;
	}
	_menu = menu;
	_menuStatus = 0xffff;
	if (menu == kMenuSave || menu == kMenuLoad)
		ensureSaves();
	static const char *const kNames[] = { "menu-globe", "menu-save", "menu-load", "menu-options", "menu-quit" };
	_log.line(Common::String::format("Menu: %s", kNames[menu]));
	drawMapScreen();
	dumpScreen(_system, kNames[menu]);
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
	dumpScreen(_system, ok ? "menu-saved" : "menu-save-error");
	return ok;
}

bool GameScreen::loadSlot(uint slot) {
	if (!ensureSaves() || slot >= SaveGame::kSlots)
		return false;
	if (!_saves->load(slot)) {
		_menuStatus = _panel.findCommand("*** SAVE ERROR", true);
		if (_mode == kMap)
			drawMapScreen();
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
	_mode = kRoom;
	leaveMap();
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

void GameScreen::startConversation(uint character) {
	if (!loadDialogue()) {
		showStatus("Dune: dialogue data missing");
		return;
	}
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
	if (_mode != kTalk || !_conversation)
		return;
	applyStory();
	if (!_talkEnded && _talkLine + bubbleLines() < _talkLines.size()) {
		// The rest of a page that did not fit the balloon.
		_talkLine += bubbleLines();
	} else {
		Common::String page;
		bool newSentence = false;
		if (_talkEnded || !_conversation->next(page, newSentence)) {
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
			if (_talkRecruit) {
				// WORK FOR ME was answered: the troop follows unless the line
				// dropped the gate (the refusal's event 2, seg000:95e2).
				const uint recruit = _talkRecruit;
				_talkRecruit = 0;
				if (!_conversation->gateHeld()) {
					_log.line(Common::String::format("Troops: troop %u refused", recruit));
					showRoom(_world.room());
					return;
				}
				_world.rallyTroop(recruit);
				// The Fremen now answer as their troop's chief: the talk stays
				// open with GIVE ORDERS TO TROOP (setup_npc_dialogue_menu for a
				// rallied troop's person, seg000:90de).
				for (uint who = World::kFremenChief; who < World::kFremenChief + 8; ++who) {
					if (_world.troopForPerson(who) != recruit)
						continue;
					openTalk(who);
					_talkEnded = true;
					updateRoomVars();
					drawTalk();
					dumpScreen(_system, "troop-rallied-talk");
					return;
				}
				showRoom(_world.room());
				return;
			}
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
	startTalkAnimation();
	drawTalk();
	dumpScreen(_system, Common::String::format("talk-%u", ++_talkPage).c_str());
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
		} else if (speaker == 12) {
			// Character 12 shows the hidden place whose pointer is at ds:11ce.
			screen->_world.revealPointedPlace(0x11ce);
		} else {
			// 3 Duncan: seg000:2239 (the spice shipment); 5 Stilgar: arms
			// the Water of Life scene (ds:227e = 0x2ccf); 13 the smugglers
			// (seg000:2388). Not built yet.
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
			screen->_world.duncanAccept(); // seg000:24ee
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
	triggers.start(16, 7, 0x80, true);
	Common::String page;
	bool newSentence;
	if (triggers.next(page, newSentence))
		triggers.next(page, newSentence); // finishes the entry: its event fires
	_state.setW(GameState::kPersonsTalkingTo, 0);
}

void GameScreen::presentVerb(uint list) {
	// A verb presents one list of the speaker (seg000:95e2: topic 5 for COME
	// WITH ME / WORK FOR ME, 6 for STAY HERE) with the auto mask 0x20.
	_talkEnded = false;
	_talkLines.clear();
	_talkLine = 0;
	_conversation->start(MIN<uint>(_talkWho, World::kFremenChief), list, 0x20, true);
	_conversation->armGate(); // arm_dialogue_interrupt_gate
	advanceConversation();
}
void GameScreen::endConversation() {
	// STOP TALKING.
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
	showRoom(_world.room());
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
	if (!isFastCapture()) {
		const uint32 now = _system->getMillis();
		if (!_clockStart || (_mode != kRoom && _mode != kMap))
			_clockStart = now;
		else if (now - _clockStart >= World::kPeriodMillis)
			passTime(1);
		checkIdle(now);
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
	} else {
		composeView();
	}
	if (!_talkEnded) {
		// While a line is spoken the view zooms twice onto the speaker (the
		// room pixel-doubled, as the recording shows), the portrait comes up
		// on the left and the line sits in a balloon.
		if (!_visionDream) {
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
	const bool exits[4] = { false, false, false, false };
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
	Graphics::Surface area = _surface.surfacePtr()->getSubArea(box);
	Graphics::ManagedSurface tiles;
	tiles.create(box.width(), box.height(), Graphics::PixelFormat::createFormatCLUT8());
	for (int y = 0; y < box.height(); y += 29)
		for (int x = 0; x < box.width(); x += 33)
			_panel.drawIcon(tiles, 0x1c, x, y);
	area.copyRectToSurface(*tiles.surfacePtr(), 0, 0, Common::Rect(0, 0, box.width(), box.height()));
	tiles.free();
	const int padding = 12, lineHeight = 10;
	const int width = box.width() - 2 * padding;
	int y = box.top + (box.height() - (int)count * lineHeight) / 2;
	for (uint i = first; i < first + count && i < lines.size(); ++i, y += lineHeight) {
		const Common::String &line = lines[i];
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
		const bool last = i + 1 >= lines.size();
		int used = 0;
		for (uint k = 0; k < words.size(); ++k)
			used += _panel.textWidth(words[k].c_str(), false);
		const int gaps = (int)words.size() - 1;
		const int space = (!last && gaps > 0) ? MAX(3, (width - used) / gaps) : _panel.textWidth(" ", false) + 1;
		int x = box.left + padding;
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
				if (arrow == 4)
					_map->centreOn(_world.currentLocation());
				else if (_map->mode() == MapScreen::kFlat)
					_map->scroll(dx[arrow], dy[arrow]);
				else
					_map->rotate(dx[arrow] * 4096, dy[arrow] * 8);
				drawMapScreen();
				return false;
			}
			int row, arrowHit;
			const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrowHit);
			if (action == Panel::kActionCommand && row >= 0 && row < (int)Panel::kCommandRows) {
				_log.line(Common::String::format("Map command: %s", _panel.commandText(row) ? _panel.commandText(row) : "(none)"));
				switch (_rowActions[row]) {
				case kRowExitMap:
					leaveMap();
					break;
				case kRowFly:
					if (_rowArguments[row] == -2)
						travelToward(_map->pointLongitude(), _map->pointLatitude());
					else
						travelTo((uint)_rowArguments[row]);
					break;
				case kRowResults: {
					// The panels slide over the planet (0.8 s in swift-dune).
					const bool open = !_map->results();
					const uint32 start = _system->getMillis();
					for (uint pixels = 0; !isFastCapture() && pixels < 100;) {
						pixels = MIN<uint>(100, (_system->getMillis() - start) * 100 / 800);
						_map->setResults(open ? pixels : 100 - pixels);
						drawMapScreen();
						_system->delayMillis(15);
					}
					_map->setResults(open ? 100 : 0);
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
					// seg000:42e9: the map again, now choosing a destination.
					openMap(MapScreen::kFlat, true);
					_map->setCaption(false);
					drawMapScreen();
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
				case kRowSaveSlot:
					saveSlot((uint)_rowArguments[row]);
					break;
				case kRowLoadSlot:
					loadSlot((uint)_rowArguments[row]);
					break;
				case kRowMusic:
					toggleMusic();
					drawMapScreen();
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
			if (action == Panel::kActionHead || action == Panel::kActionBook) {
				// Paul's head and the book close the globe as they open it.
				leaveMap();
				return false;
			}
			if (event.mouse.y < 152)
				mapTap(event.mouse.x, event.mouse.y);
		} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			if (_menu != kMenuNone)
				openMenu(kMenuNone);
			else
				leaveMap();
		}
		return false;
	}
	if (_mode == kBook) {
		if (event.type == Common::EVENT_LBUTTONDOWN) {
			int row, arrow;
			const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrow);
			if (action == Panel::kActionBook)
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
			case kRowSetOccupation:
				_world.setTroopOccupation(_troopId, (byte)_rowArguments[row]);
				_log.line(Common::String::format("Troops: troop %u now %s", _troopId, _panel.commandText(row)));
				_troopChoosing = false;
				if (_troopFromMap) {
					// The chief answers with the list again (production, job).
					stageTroopForConditions(_troopId);
					_conversation->start(World::kFremenChief, 2, 0x80, true);
					nextTroopLine();
				}
				drawTroop();
				dumpScreen(_system, "troop-ordered");
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
			case kRowExitGame:
				openMenu(kMenuQuit);
				break;
			case kRowMirrorAway:
				showRoom(_world.room());
				break;
			default:
				break;
			}
		} else if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			showRoom(_world.room());
		}
		return false;
	}
	if (_mode == kTalk) {
		int row = -1, arrow = -1;
		const Panel::Action action = event.type == Common::EVENT_LBUTTONDOWN ?
				_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) : Panel::kActionNone;
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
				// seg000:241a/2432/2453: the choice, then the dialogue goes on (loc_19472).
				if (_conversation->bargainParty() == 0) {
					_world.bargainChoice((byte)_rowArguments[row]);
				} else {
					_state.setB(World::kChoice, (byte)_rowArguments[row]);
					_state.setB(World::kArguing, (byte)(_state.b(World::kArguing) + 1));
				}
				_log.line(Common::String::format("Talk: bargaining choice %d", _rowArguments[row]));
				_talkBargain = false;
				_talkEnded = false;
				_conversation->resume();
				advanceConversation();
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
			case kRowWorkForMe: {
				// seg000:95c1: the charisma check decides; the Fremen answer
				// with their topic-5 line, and a pass rallies the troop.
				const uint troop = _world.localTroop(false);
				if (!troop)
					break;
				_talkRecruitOk = _world.troopAgreesToFollow(troop);
				_state.setB(0x23, _talkRecruitOk ? 0 : 2); // pending_room_action: the check's outcome for the conditions
				_talkRecruit = troop;
				_log.line(Common::String::format("Troops: WORK FOR ME to troop %u: %s", troop, _talkRecruitOk ? "yes" : "no"));
				presentVerb(5);
				break;
			}
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
			case kRowCompanion: {
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
				break;
			}
			default:
				break;
			}
			return false;
		}
		if ((event.type == Common::EVENT_LBUTTONDOWN && event.mouse.y < 152) ||
				(event.type == Common::EVENT_KEYDOWN && event.kbd.keycode != Common::KEYCODE_ESCAPE)) {
			_log.line(Common::String::format("Tap: conversation page %u", _talkPage));
			if (!_talkEnded)
				advanceConversation();
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
	} else if (event.type == Common::EVENT_LBUTTONDOWN) {
		int row, arrow;
		const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrow);
		// Taps are logged with what they hit: on a device this is the only
		// way to tell a touch-mapping problem from a hit-zone problem.
		static const char *const names[] = { "nothing", "up", "right", "down", "left", "book", "command", "head" };
		_log.line(Common::String::format("Tap: room %u at (%d, %d) -> %s", _room, event.mouse.x, event.mouse.y, names[action]));
		panelAction(action, row, arrow);
	}

	return false;
}

} // namespace Dune

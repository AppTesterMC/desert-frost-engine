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

#include "common/system.h"
#include "engines/engine.h"
#include "engines/util.h"
#include "graphics/paletteman.h"

#include "dune/cursor.h"
#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/palace.h"
#include "dune/resource.h"
#include "dune/segacd_game.h"
#include "dune/segacd_resources.h"

namespace Dune {

namespace {

// Palette use of the 8-bit screen: the front screen's four CRAM lines, the
// companion's, then the placeholder panel's colours.
const byte kFrontColours = 0;
const byte kBackColours = 64;
const byte kPanelColours = 128; // SegaCdPanel's 64 colours
enum PanelColour {
	kPanelDark = 240,
	kPanelMid,
	kPanelLight,
	kPanelText,
	kPanelDim,
	kPanelArrow,
	kPanelArrowOff,
	kPanelPressed,
	kPanelColourCount = 8
};

// The panel is tinted by the place, as the Sega CD's own panel is
// (FINDINGS.md: violet outdoors, bronze in the palace, ivory in battle);
// these colours are measured by eye from the gameplay video, not decoded.
void panelColours(byte placeType, byte rgb[kPanelColourCount * 3]) {
	static const byte kViolet[3][3] = { { 40, 16, 72 }, { 88, 48, 150 }, { 150, 110, 210 } };
	static const byte kBronze[3][3] = { { 56, 32, 12 }, { 130, 84, 36 }, { 200, 150, 90 } };
	static const byte kRust[3][3] = { { 60, 14, 10 }, { 130, 40, 30 }, { 200, 90, 70 } };
	const byte (*tint)[3] = placeType == Location::kPalace ? kBronze : placeType >= Location::kFortressMin ? kRust : kViolet;
	for (uint i = 0; i < 3; ++i)
		for (uint c = 0; c < 3; ++c)
			rgb[3 * i + c] = tint[i][c];
	static const byte kFixed[5][3] = {
		{ 240, 240, 230 }, // text
		{ 150, 150, 150 }, // dim text
		{ 250, 220, 80 },  // exit available
		{ 70, 60, 70 },    // exit closed
		{ 255, 255, 255 }  // pressed
	};
	for (uint i = 0; i < 5; ++i)
		for (uint c = 0; c < 3; ++c)
			rgb[3 * (3 + i) + c] = kFixed[i][c];
}

// The direction pad's arrows (placeholder geometry, where the Sega CD's pad sits).
const int16 kArrows[4][4] = {
	{ 271, 172, 291, 186 }, // up
	{ 294, 188, 314, 202 }, // right
	{ 271, 206, 291, 220 }, // down
	{ 248, 188, 268, 202 }  // left
};

// COMMANDx records of the characters, in DIALOGUE.HSQ order (scene.cpp's list).
const char *const kPeopleCommands[World::kCharacters] = {
	"DUKE LETO ATREIDES", "JESSICA", "Thufir HAWAT", "Duncan IDAHO", "Gurney HALLECK", "STILGAR, Fremen leader",
	"KYNES, planetary ecologist", "CHANI", "HARAH", "BARON VLADIMIR HARKONNEN", "FEYD-RAUTHA HARKONNEN",
	"EMPEROR SHADDAM IV", "Harkonnen Captain", "Smuggler", "Fremen", "Fremen Chief"
};

// Short names for the placeholder panel.
const char *const kPeople[World::kCharacters] = {
	"DUKE LETO ATREIDES", "JESSICA", "Thufir HAWAT", "Duncan IDAHO", "Gurney HALLECK", "STILGAR",
	"KYNES", "CHANI", "HARAH", "BARON HARKONNEN", "FEYD-RAUTHA", "EMPEROR SHADDAM IV",
	"Harkonnen Captain", "Smuggler", "Fremen", "Fremen Chief"
};

} // namespace

SegaCdGame::SegaCdGame(OSystem *system, Resource &resources, SegaCdArchive &archive, StartupLog &log) :
		_system(system), _resources(resources), _archive(archive), _log(log), _world(_state, resources, log),
		_sentences(resources, log), _panel(system, resources),
		_conversation(_sentences, _dialogue, _conditions, _state, log), _room(0), _screen(0), _pressedArrow(-1) {
	_surface.create(kWidth, kHeight, Graphics::PixelFormat::createFormatCLUT8());
	_world.setSegaCdProgram(&_archive.program());
	_sega.load(archive, log);
}

SegaCdGame::~SegaCdGame() {
	delete _mapRenderer;
}

bool SegaCdGame::loadScreen(uint file, SegaCdScreen &screen) const {
	Common::Array<byte> data;
	return _archive.load(file, data) && screen.parse(data);
}

void SegaCdGame::setPalette(const SegaCdScreen *const screens[], const byte bases[], uint count) {
	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	for (uint i = 0; i < count; ++i)
		screens[i]->palette(rgb + 3 * bases[i]);
	if (_sega.loaded())
		_sega.palette(count ? screens[count - 1] : nullptr, rgb + 3 * kPanelColours);
	panelColours(_world.placeType(), rgb + 3 * kPanelDark);
	_system->getPaletteManager()->setPalette(rgb, 0, 256);
}

bool SegaCdGame::showScreens(const uint16 *files, uint count, const Common::String &caption, uint planes) {
	SegaCdScreen screens[2];
	const SegaCdScreen *order[2];
	byte bases[2];
	uint loaded = 0;
	for (uint i = 0; i < count && loaded < 2; ++i) {
		if (!loadScreen(files[i], screens[loaded])) {
			_log.line(Common::String::format("Sega CD: file %u is not a tile screen", files[i]));
			continue;
		}
		if (planes)
			screens[loaded].limitPlanes(planes);
		order[loaded] = &screens[loaded];
		bases[loaded] = loaded + 1 == count || count == 1 ? kFrontColours : kBackColours;
		++loaded;
	}
	if (!loaded)
		return false;
	if (loaded == 1)
		bases[0] = kFrontColours;
	_surface.fillRect(Common::Rect(0, 0, kWidth, kHeight), 0);
	SegaCdScreen::compose(order, bases, loaded, *_surface.surfacePtr());
	setPalette(order, bases, loaded);
	_caption = caption;
	return true;
}

uint SegaCdGame::roomScreen(byte code) const {
	return kRoomScreenBase + code;
}

const RoomRecord *SegaCdGame::currentRoom() const {
	return _room >= 1 && _room <= _rooms.size() ? &_rooms[_room - 1] : nullptr;
}

bool SegaCdGame::startNewGame() {
	_state.newGame();
	if (!_world.loadInitialData() || !_world.ready()) {
		_log.line("Sega CD: the initial game data could not be built");
		return false;
	}
	_world.prepareNewGame();
	_state.setB(0xfc, 1); // the always-true condition byte (as GameScreen::startNewGame)
	if (!_sentences.load(1))
		_log.line("Sega CD: the English COMMAND/PHRASE files are missing");
	_dialogue.load(_resources, _log);
	_conditions.load(_resources, _log);
	_log.line(Common::String::format("Sega CD: new game, %u locations, first is %s", _world.locationCount(),
			_world.locationName(0, _sentences).c_str()));
	_world.setPosition(_world.currentLocation(), kPalaceFirstRoom);
	return showRoom(kPalaceFirstRoom);
}

bool SegaCdGame::enterLocation(uint location, uint room) {
	if (location >= _world.locationCount())
		return false;
	_world.setPosition(location, room);
	return showRoom(room);
}

bool SegaCdGame::showRoom(uint room) {
	_world.roomTable(_world.placeType(), _rooms);
	_room = room;
	const RoomRecord *record = currentRoom();
	if (!record) {
		_log.line(Common::String::format("Sega CD: place type %#x has no room %u", _world.placeType(), room));
		return false;
	}
	_world.setPosition(_world.currentLocation(), room);
	_screen = roomScreen(record->code);
	uint16 files[2];
	uint count = 0;
	SegaCdScreen probe;
	const uint companion = _screen >= kRoomScreenBase ? _screen + kCompanionOffset : kEarlyCompanion;
	if (loadScreen(companion, probe))
		files[count++] = (uint16)companion;
	files[count++] = (uint16)_screen;
	const bool ok = showScreens(files, count, _world.locationName(_world.currentLocation(), _sentences));
	_log.line(Common::String::format("Sega CD: place %u (type %#x) room %u -> screen %u over %u: %s",
			_world.currentLocation(), _world.placeType(), room, _screen, companion, ok ? "drawn" : "FAILED"));
	drawPanel();
	return ok;
}

void SegaCdGame::commandRows(Common::Array<Common::String> &rows) const {
	rows.clear();
	auto command = [&](const char *name) {
		const uint16 id = _panel.findCommand(name, false);
		return id == 0xffff ? Common::String(name) : _panel.commandString(id);
	};
	if (_mapOpen) {
		// The recording's map menu; only EXIT MAPS works so far.
		rows.push_back(command("EXIT MAPS"));
		rows.push_back(command("CONTACT FREMEN TROOPS"));
		rows.push_back(command("SEE SPICE DENSITY"));
		rows.push_back(command("TAKE AN ORNITHOPTER"));
		rows.push_back(command("FIND PROSPECTORS"));
		return;
	}
	if (_talking) {
		// The recording's talk menu: ">>>> TALK <<<<" over the verbs; the
		// verbs themselves (COME WITH ME, WHAT ?) are not wired yet.
		rows.push_back(">>>> TALK <<<<");
		rows.push_back(command("STOP TALKING"));
		return;
	}
	rows.push_back(command("SEE DUNE MAP"));
	Common::Array<byte> people;
	if (_room)
		_world.peopleInRoom(people);
	for (uint i = 0; i < people.size(); ++i) {
		if (rows.size() == SegaCdPanel::kRows - 1 && people.size() > i + 1) {
			rows.push_back(command("Others..."));
			break;
		}
		rows.push_back(people[i] < World::kCharacters ? command(kPeopleCommands[people[i]]) : Common::String("?"));
	}
}

void SegaCdGame::drawPanel() {
	if (!_sega.loaded()) {
		drawPlaceholderPanel();
		return;
	}
	SegaCdPanel::State state;
	const RoomRecord *record = currentRoom();
	if (record)
		for (uint d = 0; d < 4; ++d) {
			const byte e = record->exits[d];
			if (e != 0 && (e >= World::kExitLeave || !(e & 0x80)))
				state.exitMask |= 1u << d;
			if (e >= World::kExitLeave)
				state.canLeave = true;
		}
	state.pressedArrow = _pressedArrow;
	state.map = _mapOpen;
	state.day = (uint)((_state.w(GameState::kGameTime) + 3) >> 4) % 365 + 1;
	state.night = _world.timeSlot() > 12;
	state.companions[0] = _world.companion(0);
	state.companions[1] = _world.companion(1);
	commandRows(state.rows);
	_surface.fillRect(Common::Rect(0, kPanelTop, kWidth, kHeight), kPanelColours);
	_sega.draw(_surface, kPanelColours, state, _panel);
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, kWidth, kHeight);
	_system->updateScreen();
}

void SegaCdGame::drawPlaceholderPanel() {
	// Placeholder panel (the Sega CD's panel tiles are not decoded yet):
	// place and day on the left, the people present in the middle, the
	// direction pad with the room's open exits on the right.
	Graphics::ManagedSurface &s = _surface;
	s.fillRect(Common::Rect(0, kPanelTop, kWidth, kHeight), kPanelMid);
	s.frameRect(Common::Rect(0, kPanelTop, kWidth, kHeight), kPanelLight);
	s.fillRect(Common::Rect(4, kPanelTop + 4, 92, kHeight - 4), kPanelDark);
	s.fillRect(Common::Rect(96, kPanelTop + 4, 240, kHeight - 4), kPanelDark);
	s.fillRect(Common::Rect(244, kPanelTop + 2, 316, kHeight - 2), kPanelDark);

	const uint day = (uint)((_state.w(GameState::kGameTime) + 3) >> 4) % 365 + 1;
	_panel.drawText(s, Common::String::format("Day %u", day).c_str(), 8, kPanelTop + 8, kPanelText, false);
	Common::String place = _caption;
	while (place.size() > 1 && _panel.textWidth(place.c_str(), true) > 82)
		place.deleteLastChar();
	_panel.drawText(s, place.c_str(), 8, kPanelTop + 22, kPanelText, true);
	_panel.drawText(s, Common::String::format("screen %u", _screen).c_str(), 8, kPanelTop + 36, kPanelDim, true);

	Common::Array<byte> people;
	if (_room)
		_world.peopleInRoom(people);
	int y = kPanelTop + 7;
	if (people.empty())
		_panel.drawText(s, "(nobody here)", 100, y, kPanelDim, true);
	for (uint i = 0; i < people.size() && i < 5; ++i, y += 9)
		_panel.drawText(s, people[i] < World::kCharacters ? kPeople[people[i]] : "?", 100, y, kPanelText, true);

	const RoomRecord *record = currentRoom();
	for (uint d = 0; d < 4; ++d) {
		byte colour = kPanelArrowOff;
		if (record) {
			const byte e = record->exits[d];
			if (e != 0 && (e >= World::kExitLeave || !(e & 0x80)))
				colour = (int)d == _pressedArrow ? kPanelPressed : kPanelArrow;
		}
		const Common::Rect r(kArrows[d][0], kArrows[d][1], kArrows[d][2], kArrows[d][3]);
		// A triangle pointing outwards from the pad's centre.
		for (int i = 0; i < 7; ++i) {
			const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
			switch (d) {
			case 0: s.hLine(cx - i, r.top + 3 + i, cx + i, colour); break;
			case 2: s.hLine(cx - i, r.bottom - 3 - i, cx + i, colour); break;
			case 1: s.vLine(r.right - 3 - i, cy - i, cy + i, colour); break;
			default: s.vLine(r.left + 3 + i, cy - i, cy + i, colour); break;
			}
		}
	}
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, kWidth, kHeight);
	_system->updateScreen();
}

bool SegaCdGame::move(uint direction) {
	if (_talking)
		return false;
	const RoomRecord *record = currentRoom();
	if (!record || direction > 3)
		return false;
	const byte exit = record->exits[direction];
	if (!exit || (exit < World::kExitLeave && (exit & 0x80)))
		return false;
	if (exit >= World::kExitLeave) {
		// Leaving the place: the map is not ported for this release yet, so
		// the next location stands in for choosing a destination.
		return enterLocation((_world.currentLocation() + 1) % _world.locationCount());
	}
	_state.setB(0x0c, exit & 0x7f);
	return showRoom(exit & 0x7f);
}

bool SegaCdGame::handleEvent(const Common::Event &event) {
	switch (event.type) {
	case Common::EVENT_QUIT:
	case Common::EVENT_RETURN_TO_LAUNCHER:
		return true;
	case Common::EVENT_KEYDOWN:
		switch (event.kbd.keycode) {
		case Common::KEYCODE_UP: move(0); break;
		case Common::KEYCODE_RIGHT: move(1); break;
		case Common::KEYCODE_DOWN: move(2); break;
		case Common::KEYCODE_LEFT: move(3); break;
		case Common::KEYCODE_m: // developer travel until the map is ported
			enterLocation((_world.currentLocation() + 1) % _world.locationCount());
			break;
		case Common::KEYCODE_n:
			enterLocation((_world.currentLocation() + _world.locationCount() - 1) % _world.locationCount());
			break;
		default:
			break;
		}
		return false;
	case Common::EVENT_LBUTTONDOWN:
		if (_sega.loaded()) {
			const int arrow = SegaCdPanel::arrowAt(event.mouse.x, event.mouse.y);
			if (arrow >= 0) {
				static const int kScroll[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
				if (_mapOpen)
					scrollMap(kScroll[arrow][0], kScroll[arrow][1]);
				else
					move((uint)arrow);
				return false;
			}
			const int row = SegaCdPanel::rowAt(event.mouse.x, event.mouse.y);
			if (_mapOpen) {
				if (row == 0) {
					closeMap();
				} else if (event.mouse.y < kPanelTop) {
					const int place = mapPlaceAt(event.mouse.x, event.mouse.y);
					if (place >= 0) {
						_mapOpen = false;
						enterLocation((uint)place);
					}
				}
				return false;
			}
			if (_talking) {
				if (row == 1)
					stopTalking();
				else if (event.mouse.y < kPanelTop)
					advanceTalk();
				return false;
			}
			// SEE DUNE MAP.
			if (row == 0)
				openMap();
			else if (row > 0) {
				Common::Array<byte> people;
				_world.peopleInRoom(people);
				if ((uint)row - 1 < people.size())
					startTalking(people[row - 1]);
			}
			return false;
		}
		for (uint d = 0; d < 4; ++d)
			if (Common::Rect(kArrows[d][0], kArrows[d][1], kArrows[d][2], kArrows[d][3]).contains(event.mouse)) {
				move(d);
				return false;
			}
		if (Common::Rect(4, kPanelTop + 4, 92, kHeight - 4).contains(event.mouse))
			enterLocation((_world.currentLocation() + 1) % _world.locationCount());
		return false;
	default:
		return false;
	}
}

namespace {

/**
 * The talking portraits' rest poses: which sprite bank shows a character
 * and where its frames go (top-left of the frame's canvas, screen pixels).
 * MEASURED, not decoded: scripts/segacd_portrait_pose.py matched every
 * frame of the bank against the longplay's close-up of that character
 * (notes/segacd-gameplay/longplay/key_*_first.png) and kept the frames that
 * fit, largest first; the program's own placement table is not found yet.
 * Banks without a row (Duncan, Kynes, Harah, Feyd-Rautha, the Fremen) were
 * not identified; the smuggler's and the chief's heads live in other banks
 * (1801-1815) whose palettes differ, so they show their bodies only.
 */
struct PortraitPart {
	int8 frame;
	int16 x, y;
};

struct Portrait {
	byte character;
	uint16 bank;
	PortraitPart parts[8];
};

const Portrait kPortraits[] = {
	{ 0, 1782, { { 5, -2, 78 }, { 3, 30, 19 }, { 6, 55, 38 }, { 0, 66, 71 }, { -1, 0, 0 } } },
	{ 1, 1783, { { 2, 1, 18 }, { 17, -2, 140 }, { 1, 38, 63 }, { 7, 66, 138 }, { 0, 43, 100 }, { 14, 40, 129 }, { 11, 111, 155 }, { 18, 60, 87 } } },
	{ 2, 1784, { { 9, -2, 108 }, { 1, 28, 19 }, { 5, 19, 82 }, { 8, 73, 42 }, { 3, 83, 66 }, { 11, 55, 105 }, { 0, 61, 105 }, { -1, 0, 0 } } },
	{ 4, 1786, { { 1, -2, 87 }, { 5, 12, 18 }, { 2, 33, 52 }, { 9, 35, 51 }, { 12, 68, 57 }, { 14, 44, 79 }, { 0, 34, 99 }, { -1, 0, 0 } } },
	{ 5, 1806, { { 6, 36, 19 }, { 8, 88, 108 }, { 7, -2, 127 }, { 9, 19, 81 }, { 10, 54, 111 }, { 1, 129, 87 }, { 0, 65, 113 }, { -1, 0, 0 } } },
	{ 7, 1789, { { 1, 17, 20 }, { 2, 53, 114 }, { 20, -2, 135 }, { 19, 85, 133 }, { 21, 55, 59 }, { 0, 61, 91 }, { 11, 35, 86 }, { 13, 25, 119 } } },
	{ 9, 1791, { { 1, 50, 34 }, { 2, 0, 34 }, { 20, 11, 118 }, { 6, 8, 68 }, { 22, 67, 118 }, { 23, 67, 118 }, { 13, 111, 47 }, { 15, 39, 51 } } },
	{ 11, 1793, { { 1, -2, 95 }, { 24, 26, 16 }, { 0, 64, 77 }, { 12, 58, 38 }, { 28, 35, 38 }, { 10, 42, 68 }, { 27, 56, 68 }, { 26, 57, 77 } } },
	{ 12, 1794, { { 6, -1, 46 }, { 9, 11, 66 }, { 7, 82, 48 }, { 0, 56, 29 }, { -1, 0, 0 } } },
	{ 13, 1795, { { 17, -2, 66 }, { 25, -2, 124 }, { 24, -2, 127 }, { -1, 0, 0 } } },
	{ 15, 1833, { { 5, -2, 74 }, { -1, 0, 0 } } }
};

const byte kPortraitColours = 192; ///< 15 colours from the bank's CRAM
const byte kTalkBox = 208, kTalkInk = 209;
// The text box, measured: cells 20-38, rows 2-15.
enum { kTalkLeft = 160, kTalkTop = 16, kTalkRight = 312, kTalkBottom = 128 };

// The box's colour by place (measured from the longplay; the source of the
// colour in the data is not found yet): palace orange, sietch beige,
// village blue, fortresses and the Harkonnen palace pale.
void talkBoxColour(byte placeType, byte rgb[3]) {
	static const byte kOrange[3] = { 230, 131, 0 }, kBeige[3] = { 196, 164, 130 }, kBlue[3] = { 130, 165, 228 },
			kPale[3] = { 196, 197, 162 };
	const byte *c = placeType == Location::kPalace ? kOrange : placeType <= Location::kSietchMax ? kBeige :
			placeType <= Location::kVillageMax ? kBlue : kPale;
	memcpy(rgb, c, 3);
}

} // namespace

bool SegaCdGame::drawPortrait(uint character) {
	for (uint i = 0; i < ARRAYSIZE(kPortraits); ++i) {
		if (kPortraits[i].character != character)
			continue;
		Common::Array<byte> data;
		SegaCdSpriteBank bank;
		if (!_archive.load(kPortraits[i].bank, data) || !bank.parse(data))
			return false;
		byte rgb[15 * 3];
		bank.palette(rgb);
		_system->getPaletteManager()->setPalette(rgb, kPortraitColours, 15);
		for (uint p = 0; p < 8 && kPortraits[i].parts[p].frame >= 0; ++p)
			bank.drawFrame(*_surface.surfacePtr(), (uint)kPortraits[i].parts[p].frame, kPortraits[i].parts[p].x,
					kPortraits[i].parts[p].y, kPortraitColours);
		return true;
	}
	return false;
}

bool SegaCdGame::startTalking(uint character) {
	if (!_dialogue.loaded() || character >= World::kCharacters)
		return false;
	_talker = character;
	_talking = true;
	_conversation.start(character);
	advanceTalk();
	return _talking;
}

void SegaCdGame::advanceTalk() {
	bool newSentence = false;
	if (!_talking || !_conversation.next(_page, newSentence)) {
		stopTalking();
		return;
	}
	_log.line(Common::String::format("Sega CD: %s says: %s", kPeopleCommands[_talker], _page.c_str()));
	drawTalk();
}

void SegaCdGame::stopTalking() {
	_conversation.stop();
	_talking = false;
	if (_room)
		showRoom(_room);
}

void SegaCdGame::drawTalk() {
	// The close-up backdrop: the room's second screen (FINDINGS.md: the
	// "+66" screens are the rooms' conversation backgrounds).
	const uint16 backdrop = (uint16)(_screen >= kRoomScreenBase ? _screen + kCompanionOffset : kEarlyCompanion);
	if (!showScreens(&backdrop, 1, _caption))
		_surface.fillRect(Common::Rect(0, 0, kWidth, kPanelTop), 0);
	if (!drawPortrait(_talker))
		_log.line(Common::String::format("Sega CD: no portrait measured for character %u", _talker));

	// The text box and the line, in the PC font's large set, word-wrapped.
	byte rgb[6];
	talkBoxColour(_world.placeType(), rgb);
	rgb[3] = rgb[4] = rgb[5] = 0;
	_system->getPaletteManager()->setPalette(rgb, kTalkBox, 2);
	_surface.fillRect(Common::Rect(kTalkLeft, kTalkTop, kTalkRight, kTalkBottom), kTalkBox);
	const int left = kTalkLeft + 10, right = kTalkRight - 8;
	int y = kTalkTop + 10;
	Common::String line, word;
	auto flush = [&]() {
		if (!line.empty() && y + 9 <= kTalkBottom) {
			_panel.drawText(_surface, line.c_str(), left, y, kTalkInk, false);
			y += 10;
		}
		line.clear();
	};
	Common::String text = _page + " ";
	for (uint i = 0; i < text.size(); ++i) {
		const char ch = text[i];
		if (ch == ' ' || ch == '\r' || ch == '\n') {
			const Common::String candidate = line.empty() ? word : line + " " + word;
			if (!word.empty() && left + _panel.textWidth(candidate.c_str(), false) > right) {
				flush();
				line = word;
			} else if (!word.empty()) {
				line = candidate;
			}
			word.clear();
			if (ch != ' ')
				flush();
		} else {
			word += ch;
		}
	}
	flush();
	drawPanel();
}

namespace {

// The map view (measured from the longplay): the terrain inside a blue frame
// over the room view; the sand's two main tones and the frame colour.
enum { kMapLeft = 8, kMapTop = 8, kMapRight = 312, kMapBottom = 160 };
const byte kMapColours = 210; ///< 16 terrain shades, then frame, marker, marker outline
const byte kMapYellow[3] = { 231, 230, 98 }, kMapTan[3] = { 229, 197, 130 }, kMapOchre[3] = { 196, 131, 65 },
		kMapFrame[3] = { 65, 131, 229 }, kMapMarker[3] = { 97, 0, 0 };
const uint16 kMapPanelTint = 1853; ///< its CRAM line 0 is the blue panel tint (guess)

} // namespace

bool SegaCdGame::openMap() {
	if (!_mapRenderer) {
		if (_world.map().size() < MapRenderer::kMapSize || !_resources.load("TABLAT.BIN", _tablat) ||
				_tablat.size() < MapRenderer::kTablatSize) {
			_log.line("Sega CD: MAP.HSQ or TABLAT.BIN missing for the map");
			return false;
		}
		_mapRenderer = new MapRenderer(_world.map(), _tablat);
		_mapView.create(320, 200, Graphics::PixelFormat::createFormatCLUT8());
	}
	const Location here = _world.location(_world.currentLocation());
	// Centre the band window on the current place (36 rows of the PC view).
	_mapLatitude = (int16)(here.latitude - MapRenderer::kViewRows / 4);
	_mapLongitude = here.longitude;
	_mapOpen = true;
	_mapLogged = false;
	drawMap();
	return true;
}

void SegaCdGame::scrollMap(int dx, int dy) {
	_mapLongitude = (uint16)(_mapLongitude + dx * 0x400);
	_mapLatitude = (int16)CLIP<int>(_mapLatitude + dy * 4, -75, 75 - MapRenderer::kViewRows / 2);
	drawMap();
}

int SegaCdGame::mapPlaceAt(int x, int y) const {
	if (!_mapRenderer)
		return -1;
	int best = -1, bestDistance = 12 * 12;
	for (uint i = 0; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (l.hidden())
			continue;
		int px, py;
		if (!_mapRenderer->project(_mapLatitude, _mapLongitude, l.latitude, l.longitude, px, py))
			continue;
		// PC view pixel (kViewX + column, kViewY + row) to the zoomed view.
		const int sx = kMapLeft + 2 * (px - 84), sy = kMapTop + 2 * (py - MapRenderer::kViewY);
		const int d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
		if (d < bestDistance) {
			bestDistance = d;
			best = (int)i;
		}
	}
	return best;
}

void SegaCdGame::drawMap() {
	_mapView.fillRect(Common::Rect(0, 0, 320, 200), 0x10);
	_mapRenderer->draw(*_mapView.surfacePtr(), _mapLatitude, _mapLongitude);

	// Palette: 16 shades thresholded to the Sega CD's tones (the mapping of
	// the PC's shades to them is a guess), the frame, the markers.
	byte rgb[19 * 3];
	for (uint i = 0; i < 16; ++i)
		memcpy(rgb + 3 * i, i < 7 ? kMapYellow : i < 12 ? kMapTan : kMapOchre, 3);
	memcpy(rgb + 48, kMapFrame, 3);
	memcpy(rgb + 51, kMapMarker, 3);
	rgb[54] = rgb[55] = rgb[56] = 255;
	// The panel's colours: the blue tint from file 1853's line 0.
	SegaCdScreen tint;
	byte full[256 * 3];
	_system->getPaletteManager()->grabPalette(full, 0, 256);
	if (loadScreen(kMapPanelTint, tint) && _sega.loaded())
		_sega.palette(&tint, full + 3 * kPanelColours);
	memcpy(full + 3 * kMapColours, rgb, sizeof(rgb));
	_system->getPaletteManager()->setPalette(full, 0, 256);

	_surface.fillRect(Common::Rect(0, 0, kWidth, kPanelTop), kMapColours + 16);
	// The PC view's centre 152 x 76 pixels (x 84-235), zoomed twice.
	for (int y = kMapTop; y < kMapBottom; ++y) {
		const int sy = MapRenderer::kViewY + (y - kMapTop) / 2;
		byte *row = (byte *)_surface.getBasePtr(0, y);
		for (int x = kMapLeft; x < kMapRight; ++x) {
			const byte shade = sy < 200 ? *(const byte *)_mapView.getBasePtr(84 + (x - kMapLeft) / 2, sy) : 0x10;
			row[x] = (byte)(kMapColours + (shade & 0x0f));
		}
	}
	// Places: a dark marker with a light outline (placeholder for the
	// Sega CD's rock and palace icons, bitmap set 1858, not wired yet).
	for (uint i = 0; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		int px, py;
		if (l.hidden() || !_mapRenderer->project(_mapLatitude, _mapLongitude, l.latitude, l.longitude, px, py))
			continue;
		const int sx = kMapLeft + 2 * (px - 84), sy = kMapTop + 2 * (py - MapRenderer::kViewY);
		if (sx < kMapLeft || sx >= kMapRight || sy < kMapTop || sy >= kMapBottom)
			continue;
		if (!_mapLogged)
			_log.line(Common::String::format("Sega CD map: place %u (type %#x) at %d,%d", i, l.type, sx, sy));
		Common::Rect r(sx - 4, sy - 3, sx + 5, sy + 4);
		r.clip(Common::Rect(kMapLeft, kMapTop, kMapRight, kMapBottom));
		_surface.fillRect(r, kMapColours + 18);
		r.grow(-1);
		_surface.fillRect(r, i == _world.currentLocation() ? kMapColours + 18 : kMapColours + 17);
	}
	_mapLogged = true;
	drawPanel();
}

void SegaCdGame::reachableRooms(Common::Array<uint> &rooms) const {
	// Breadth first through the exits, hidden doors included (the story
	// opens them), from room 1. The tables are windows into shared lists and
	// the last one runs on into the pointer table, so indexing past the
	// rooms a place's exits reach would show another place's rooms.
	rooms.clear();
	if (_rooms.empty())
		return;
	rooms.push_back(1);
	for (uint i = 0; i < rooms.size(); ++i) {
		const RoomRecord &record = _rooms[rooms[i] - 1];
		for (uint d = 0; d < 4; ++d) {
			const byte e = record.exits[d];
			const uint to = e & 0x7f;
			if (!e || e >= World::kExitLeave || to < 1 || to > _rooms.size())
				continue;
			bool seen = false;
			for (uint j = 0; j < rooms.size(); ++j)
				seen = seen || rooms[j] == to;
			if (!seen)
				rooms.push_back(to);
		}
	}
}

void SegaCdGame::present(const Common::String &name) {
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, kWidth, kHeight);
	_system->updateScreen();
	dumpScreen(_system, name.c_str());
}

void SegaCdGame::dumpTour() {
	// The language screen (three planes: the choice, then each flag alone).
	uint16 language = kLanguageScreen;
	if (showScreens(&language, 1, "", 1)) {
		_surface.fillRect(Common::Rect(0, 200, kWidth, kHeight), 0);
		present("segacd-language");
	}
	// A new game: every palace room, then each kind of place.
	if (!startNewGame())
		return;
	present("segacd-throne-room");
	Common::Array<uint> rooms;
	reachableRooms(rooms);
	for (uint i = 0; i < rooms.size(); ++i)
		if (showRoom(rooms[i]))
			present(Common::String::format("segacd-palace-room-%u", rooms[i]));
	// Duke Leto's conversation, page by page, then every measured portrait
	// on the throne room's close-up backdrop (a sample line: the name).
	showRoom(kPalaceFirstRoom);
	if (startTalking(0)) {
		for (uint page = 1; _talking && page <= 6; ++page) {
			present(Common::String::format("segacd-talk-leto-%u", page));
			advanceTalk();
		}
		stopTalking();
	}
	for (uint i = 0; i < ARRAYSIZE(kPortraits); ++i) {
		_talking = true;
		_talker = kPortraits[i].character;
		_page = kPeopleCommands[_talker];
		drawTalk();
		present(Common::String::format("segacd-portrait-%u", _talker));
	}
	_talking = false;
	// The map, centred on the palace, then scrolled east.
	showRoom(kPalaceFirstRoom);
	if (openMap()) {
		present("segacd-map");
		scrollMap(1, 0);
		present("segacd-map-scrolled");
		closeMap();
	}
	// Walking: from the throne room through its first open exit and back.
	showRoom(kPalaceFirstRoom);
	for (uint d = 0; d < 4; ++d) {
		const RoomRecord *record = currentRoom();
		if (record && record->exits[d] && !(record->exits[d] & 0x80)) {
			_pressedArrow = (int)d;
			drawPanel();
			present("segacd-walk-press");
			_pressedArrow = -1;
			move(d);
			present("segacd-walk-arrived");
			break;
		}
	}
	// The first sietch, village, fortress and the Harkonnen palace.
	const byte kinds[4][2] = { { 0x00, 0x1f }, { 0x21, 0x27 }, { 0x28, 0x2f }, { 0x30, 0x30 } };
	const char *const names[4] = { "sietch", "village", "fortress", "harkonnen" };
	for (uint k = 0; k < 4; ++k) {
		for (uint l = 0; l < _world.locationCount(); ++l) {
			const byte type = _world.location(l).type;
			if (type < kinds[k][0] || type > kinds[k][1])
				continue;
			enterLocation(l, 1);
			reachableRooms(rooms);
			for (uint i = 0; i < rooms.size(); ++i)
				if (showRoom(rooms[i]))
					present(Common::String::format("segacd-%s-room-%u", names[k], rooms[i]));
			break;
		}
	}
	// The end credits' cards.
	for (uint16 f = kCreditsFirst; f <= kCreditsLast; ++f)
		if (showScreens(&f, 1, "")) {
			_surface.fillRect(Common::Rect(0, 200, kWidth, kHeight), 0);
			present(Common::String::format("segacd-credits-%u", f - kCreditsFirst));
		}
}

Common::Error runSegaCd(OSystem *system, StartupLog &log) {
	SegaCdArchive archive;
	if (!archive.open(log)) {
		warning("Dune: the Sega CD data (DUNE.DAT or the data track image) could not be read");
		return Common::kNoGameDataFoundError;
	}
	Resource resources(false);
	resources.setSegaCd(&archive);

	preferDirectTouch(); // before initGraphics(), as for the DOS releases
	initGraphics(SegaCdGame::kWidth, SegaCdGame::kHeight);
	system->fillScreen(0);
	system->updateScreen();
	debugBegin(system);
	log.line("Graphics initialized: 320x224 (Sega CD)");

	SegaCdGame game(system, resources, archive, log);
	if (isDumpRun()) {
		game.dumpTour();
		debugEnd();
		return Common::kNoError;
	}
	if (!game.startNewGame()) {
		debugEnd();
		return Common::kNoGameDataFoundError;
	}
	showGameCursor();
	Common::Event event;
	while (!Engine::shouldQuit()) {
		while (pollDuneEvent(system, event))
			if (game.handleEvent(event)) {
				debugEnd();
				return Common::kNoError;
			}
		system->updateScreen();
		system->delayMillis(10);
	}
	debugEnd();
	return Common::kNoError;
}

} // namespace Dune

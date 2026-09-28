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

#ifndef ENGINES_DUNE_SEGACD_GAME_H
#define ENGINES_DUNE_SEGACD_GAME_H

#include "common/array.h"
#include "common/error.h"
#include "common/events.h"
#include "common/str.h"
#include "graphics/managed_surface.h"

#include "dune/dialogue.h"
#include "dune/map.h"
#include "dune/panel.h"
#include "dune/segacd_gfx.h"
#include "dune/segacd_panel.h"
#include "dune/text.h"
#include "dune/world.h"

class OSystem;

namespace Dune {

class Resource;
class SegaCdArchive;
class StartupLog;

/**
 * The Sega CD release's screen host (bring-up, first iteration).
 *
 * It shares the engine's game model with the DOS releases: GameState and
 * World hold the Sega CD's own initial data (rebuilt in the PC layout by
 * segacd_world.cpp), the room tables and exits are the original's, and the
 * text comes from the disc's COMMAND/PHRASE files. What differs is the
 * presentation, which the Sega CD redid for its 320x224 screen:
 *
 * - rooms are ready-made tile screens (SegaCdScreen), not .SAL polygons and
 *   sprite sheets: a room's code is a screen number (1678 + code), drawn
 *   over a second layer at screen + 66 (the program's routine at sub-CPU
 *   0x1016c: screens from 1678 on take a companion 66 files later, earlier
 *   ones take 1614);
 * - the picture is 168 lines (21 tiles) and the panel below it 56.
 *
 * Not decoded yet, so drawn as labelled placeholders: the Sega CD panel
 * graphics (its tiles are not in the screen files), the characters' sprites
 * (files 1782-1850), the open-desert and sietch exteriors that the program
 * picks from the location (routine at sub-CPU 0xfd9c: 1627 + n for a sietch
 * and 1632 + hash % 19 or 1667 + hash % 10 for the open desert), the map,
 * dialogue, videos and audio. See FINDINGS.md, "Sega CD".
 */
class SegaCdGame {
public:
	enum {
		kWidth = 320,
		kHeight = 224,
		kViewHeight = 168,        ///< 21 tile rows
		kPanelTop = 168,
		kRoomScreenBase = 1678,   ///< sub-CPU 0x10226: addi.w #$68e
		kCompanionOffset = 66,    ///< sub-CPU 0x1017e: addi.w #$42
		kEarlyCompanion = 1614,   ///< sub-CPU 0x10172: #$64e
		kLanguageScreen = 11,     ///< "Select your language": French A, English B
		kCreditsFirst = 1911,     ///< the end credits' cards: 1911-1924
		kCreditsLast = 1924
	};

	SegaCdGame(OSystem *system, Resource &resources, SegaCdArchive &archive, StartupLog &log);
	~SegaCdGame();

	/** A new game in the throne room, as the original starts. */
	bool startNewGame();

	/** Draw and show a room of the current place (1-based). */
	bool showRoom(uint room);
	/** Move through exit 0-3 (up, right, down, left) of the current room. */
	bool move(uint direction);
	/** Put Paul at a location's first room (developer travel until the map is ported). */
	bool enterLocation(uint location, uint room = 1);

	/**
	 * Show screen files composed back to front (dump tour and credits).
	 * @param planes draw only this many planes of each (0: all)
	 */
	bool showScreens(const uint16 *files, uint count, const Common::String &caption, uint planes = 0);

	/**
	 * Talk to a character (DIALOGUE order) with the Sega CD's presentation:
	 * the room's close-up backdrop (room screen + 66), the character's
	 * portrait, the line in the text box at the upper right.
	 */
	bool startTalking(uint character);
	/** The next page; ends the conversation when nothing is left. */
	void advanceTalk();
	void stopTalking();
	bool talking() const { return _talking; }

	/**
	 * The map (bring-up): the PC renderer's terrain from the world's live
	 * MAP.HSQ, zoomed twice and recoloured with the Sega CD's sand tones, in
	 * the blue frame; the places as markers; tapping one travels there.
	 */
	bool openMap();
	void closeMap() { _mapOpen = false; if (_room) showRoom(_room); }
	bool mapOpen() const { return _mapOpen; }
	void scrollMap(int dx, int dy);

	/** Returns true when the game should end. */
	bool handleEvent(const Common::Event &event);

	/** The automated desktop check: every room of the palace, a sietch, a village, a fortress, the credits. */
	void dumpTour();

	uint room() const { return _room; }

private:
	bool loadScreen(uint file, SegaCdScreen &screen) const;
	uint roomScreen(byte code) const;
	void setPalette(const SegaCdScreen *const screens[], const byte bases[], uint count);
	void drawPanel();
	void drawPlaceholderPanel();
	/** The command rows as the panel shows them: SEE DUNE MAP, the people, Others... */
	void commandRows(Common::Array<Common::String> &rows) const;
	void present(const Common::String &name);
	/** The rooms of the current place that its first room's exits lead to, in walking order. */
	void reachableRooms(Common::Array<uint> &rooms) const;
	const RoomRecord *currentRoom() const;

	void drawTalk();
	void drawMap();
	int mapPlaceAt(int x, int y) const;
	bool drawPortrait(uint character);

	OSystem *_system;
	Resource &_resources;
	SegaCdArchive &_archive;
	StartupLog &_log;
	GameState _state;
	World _world;
	SentenceBank _sentences;
	Panel _panel;       ///< the PC font (DNCHAR.BIN) and the COMMAND strings
	SegaCdPanel _sega;  ///< the Sega CD's own panel graphics
	Graphics::ManagedSurface _surface;
	Dialogue _dialogue;
	Conditions _conditions;
	Conversation _conversation;
	bool _talking = false;
	bool _mapOpen = false;
	bool _mapLogged = false; ///< the markers are logged once per opening
	int16 _mapLatitude = 0;
	uint16 _mapLongitude = 0;
	Common::Array<byte> _tablat;
	MapRenderer *_mapRenderer = nullptr;
	Graphics::ManagedSurface _mapView; ///< the PC renderer's 320x200 output
	uint _talker = 0;
	Common::String _page;
	Common::Array<RoomRecord> _rooms;
	uint _room;
	uint _screen;       ///< the room's screen file, for the log and the panel
	Common::String _caption;
	int _pressedArrow;
};

/**
 * DuneEngine::run() for the Sega CD release: open the disc data, start a new
 * game and run the event loop (or, on a dump run, the dump tour).
 */
Common::Error runSegaCd(OSystem *system, StartupLog &log);

} // namespace Dune

#endif // ENGINES_DUNE_SEGACD_GAME_H

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

// The ornithopter's destination screen (notes/orni-cockpit-spec.md): TAKE AN
// ORNITHOPTER and CHANGE DESTINATION show the cockpit (ORNYPAN.HSQ) with the
// map in its window, "SELECT DESTINATION ON MAP" typed out above it, Paul's
// ornithopter blinking where he is, and a single "Cancel" row. A tap in the
// window picks the destination and the flight starts at once. Checked against
// the DOSBox-X capture duneprg_003.png.

#include "dune/scene.h"

#include "common/system.h"
#include "graphics/paletteman.h"

#include "dune/amiga.h"
#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/resource.h"
#include "dune/sky.h"
#include "dune/sprite.h"
#include "dune/text.h"
#include "dune/world.h"

namespace Dune {

namespace {

/** data_046e3_rect (CD ds:149c, floppy ds:14a1): the map's window. */
Common::Rect windowRect() {
	return Common::Rect(81, 45, 241, 134);
}
const int kCaptionX = 0x55, kCaptionY = 0x26;  ///< the caption's pen in cockpit mode
const uint32 kGlyphMillis = 120;               ///< 0x18 ticks a glyph (map_caption_frame_task)
const uint32 kBlinkMillis = 1500;              ///< 0x12c ticks (the player marker's blink task)
const uint16 kPlayerIcon = 0x4c;               ///< ICONES: the red ornithopter, 22 x 10

} // namespace

void GameScreen::openCockpit(bool changing) {
	// menu_callback_choice_map_main_take_an_ornithopter (CD 42d9) and
	// CHANGE DESTINATION (CD 497a) into map_screen_open (CD 430b).
	loadDialogue();
	if (!_map)
		_map = new MapScreen(_system, _resources, _log, _world);
	if (changing) {
		// The flight's map stays open (its trail too): only recentre it.
		_map->select(-1);
		_map->centreOnPosition(_flightLng, _flightLat);
	} else if (!_map->open(MapScreen::kFlat, true)) {
		showStatus("Dune: map data missing");
		return;
	}
	_map->setCaption(false);
	if (_desert && !changing)
		_map->centreOnPosition(_walkLng, _walkLat);
	_cockpit = true;
	_cockpitChanging = changing;
	_cockpitStart = _system->getMillis();
	_mode = kMap;
	debugSetScene("map/cockpit");
	_log.line(changing ? "Cockpit: CHANGE DESTINATION" : "Cockpit: select destination");
	drawMapScreen();
	dumpScreen(_system, changing ? "orni-cockpit-change" : "orni-cockpit");
}

bool GameScreen::cockpitPlayer(int &x, int &y) const {
	const Common::Rect kWindow = windowRect();
	if (!_map)
		return false;
	uint16 lng;
	int16 lat;
	if (_cockpitChanging) {
		lng = _flightLng;
		lat = _flightLat;
	} else if (_desert) {
		lng = _walkLng;
		lat = _walkLat;
	} else {
		const Location l = _world.location(_world.currentLocation());
		lng = l.longitude;
		lat = l.latitude;
	}
	return _map->windowProject(kWindow, _map->centreLongitude(), _map->centreLatitude(), lng, lat, x, y);
}

void GameScreen::drawCockpit() {
	const Common::Rect kWindow = windowRect();
	// map_screen_draw_base (CD 439f) in cockpit mode, then map_view_redraw (CD 4377).
	const uint32 now = _system->getMillis();
	// The map (floppy map_view_redraw, CD 4377): the palettes, the sky, the
	// cockpit (ORNYPAN 0, 1), the zoomed window (map_draw_zoomed_globe), the
	// markers, the grid (ORNYPAN 2) over them, Paul's blinking ornithopter.
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	_panel.applyPalette();
	_map->applyPalette();
	Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
	if (_world.amiga())
		amigaDesertView(_system, _resources, view, _state.w(GameState::kGameTime));
	else
		drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette(), true);
		setSkyPalette(false); // the hour's light, or the running blend (floppy 3b13)
	Common::Array<byte> ornypanData;
	Sprite *ornypan = nullptr;
	if (_resources.load("ORNYPAN.HSQ", ornypanData)) {
		ornypan = new Sprite(_system, ornypanData);
		ornypan->setPalette();
		ornypan->drawFrame(0, &view, 0, 19);
		ornypan->drawFrame(1, &view, 10, 43);
	}
	const uint16 centreLng = _map->centreLongitude();
	const int16 centreLat = _map->centreLatitude();
	_map->drawZoomedWindow(_surface, kWindow, centreLng, centreLat, false);
	_map->drawWindowMarkers(_surface, kWindow, centreLng, centreLat, _panel);
	if (ornypan)
		ornypan->drawFrame(2, &view, 79, 45);
	delete ornypan;

	// map_arm_player_marker_task (CD 445d): ICONES 0x4c at (x - 13, y - h), blinking.
	int px, py;
	const bool blinkOn = ((now - _cockpitStart) / kBlinkMillis) % 2 == 0 || isDumpRun();
	if (blinkOn && cockpitPlayer(px, py)) {
		// Drawn through a copy of the window so that it is clipped to it.
		Graphics::ManagedSurface window;
		window.create(kWindow.width(), kWindow.height(), Graphics::PixelFormat::createFormatCLUT8());
		window.blitFrom(_surface, kWindow, Common::Point(0, 0));
		_panel.drawIcon(window, kPlayerIcon, px - kWindow.left - 13, py - kWindow.top - 10);
		_surface.blitFrom(window, Common::Point(kWindow.left, kWindow.top));
		window.free();
	}

	// The caption, typed one glyph per 0x18 ticks (spaces are free): "SELECT
	// DESTINATION ON MAP" (floppy COMMAND 0x4c, CD 0x56), red on the dark strip.
	const uint16 captionId = _panel.findCommand("SELECT DESTINATION", true);
	if (captionId != 0xffff) {
		const Common::String caption = _panel.commandString(captionId);
		uint glyphs = isDumpRun() || isDuneFastHarness() ? 0xffff : (now - _cockpitStart) / kGlyphMillis;
		Common::String shown;
		for (uint i = 0; i < caption.size(); ++i) {
			if (caption[i] != ' ') {
				if (!glyphs)
					break;
				--glyphs;
			}
			shown += caption[i];
		}
		byte pal[256 * 3];
		_system->getPaletteManager()->grabPalette(pal, 0, 256);
		uint red = 0, best = 0xffffffff;
		for (uint i = 1; i < 256; ++i) {
			const int dr = pal[3 * i] - 232, dg = pal[3 * i + 1] - 16, db = pal[3 * i + 2] - 16;
			const uint d = (uint)(dr * dr + dg * dg + db * db);
			if (d < best) {
				best = d;
				red = i;
			}
		}
		_panel.drawText(_surface, shown.c_str(), kCaptionX, kCaptionY, (byte)red, true);
	}

	// menu_multiple_cancel (floppy ds:2794): one row, "  Cancel".
	RowAction actions[Panel::kCommandRows] = { kRowCockpitCancel };
	int arguments[Panel::kCommandRows] = { 0 };
	uint16 commands[Panel::kCommandRows] = { _panel.findCommand("Cancel", true) };
	setRows(actions, arguments, commands, commands[0] != 0xffff ? 1 : 0);
	_panel.setLeftPanel(Panel::kLeftBook);
	_panel.setCompanions(_world.companion(0), _world.companion(1));
	_panel.setNavMode(Panel::kNavBlank);
	const bool exits[4] = { false, false, false, false };
	_panel.draw(_surface, exits, -1, -1, day());
	// ui_nav_panel_map_scroll (floppy ds:235c): the four arrows round the orni.
	_panel.drawIcon(_surface, 41, 266, 171);
	_panel.drawIcon(_surface, 37, 267, 162);
	_panel.drawIcon(_surface, 38, 285, 171);
	_panel.drawIcon(_surface, 39, 267, 184);
	_panel.drawIcon(_surface, 40, 254, 171);
	_panel.drawIcon(_surface, 53, 266, 171);
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
	_cockpitDrawn = now;
}

void GameScreen::cockpitTap(int x, int y) {
	const Common::Rect kWindow = windowRect();
	// map_mouse_lmb_select_destination (CD 450e, floppy 4d4e): only inside
	// the window; the current place is inert unless a flight is under way.
	if (!kWindow.contains(x, y))
		return;
	const uint16 centreLng = _map->centreLongitude();
	const int16 centreLat = _map->centreLatitude();
	const int hit = _map->windowHit(kWindow, centreLng, centreLat, x, y);
	if (hit >= 0 && (uint)hit == _world.currentLocation() && !_desert && !_cockpitChanging)
		return;
	if (hit < 0) {
		uint16 lng;
		int16 lat;
		if (!_map->windowUnproject(kWindow, centreLng, centreLat, x, y, lng, lat))
			return;
		_map->selectPosition(lng, lat);
	}
	_cockpit = false;
	if (_cockpitChanging) {
		// A later confirm re-aims the flight under way (map_confirm_travel_and_close).
		_cockpitChanging = false;
		_changeTarget = hit >= 0 ? hit : -2;
		_changeLongitude = _map->pointLongitude();
		_changeLatitude = _map->pointLatitude();
		_log.line(hit >= 0 ? Common::String::format("Cockpit: new destination, place %d", hit)
						   : Common::String::format("Cockpit: new destination, the desert at %u/%d",
								   _changeLongitude, _changeLatitude));
		return;
	}
	if (hit >= 0) {
		_log.line(Common::String::format("Cockpit: destination place %d", hit));
		travelTo((uint)hit);
	} else {
		_log.line(Common::String::format("Cockpit: destination the desert at %u/%d", _map->pointLongitude(),
				_map->pointLatitude()));
		travelToward(_map->pointLongitude(), _map->pointLatitude());
	}
}

void GameScreen::cockpitCancel() {
	// map_screen_cleanup (CD 4415): with no travel pending, back to the pad
	// (room 1), or to the open desert; from CHANGE DESTINATION, back to the flight.
	_cockpit = false;
	_log.line("Cockpit: Cancel");
	if (_cockpitChanging) {
		_cockpitChanging = false;
		_changeTarget = -1;
		return;
	}
	if (_desert) {
		_mode = kRoom;
		_panel.setLeftPanel(Panel::kLeftBook);
		drawRoom();
		return;
	}
	leaveMap();
}

void GameScreen::updateCockpit(uint32 now) {
	// The caption's typewriter and the marker's blink.
	if (_cockpit && _mode == kMap && now - _cockpitDrawn >= kGlyphMillis)
		drawCockpit();
}

void GameScreen::drawCabin() {
	// travel_show_companion_cabin (floppy 3924), the ornithopter case: the
	// sky, then ORNYCAB.HSQ sprite 0 over the game area.
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	_panel.applyPalette();
	Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
	if (_world.amiga())
		amigaDesertView(_system, _resources, view, _state.w(GameState::kGameTime));
	else
		drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette(), true);
		setSkyPalette(false); // the hour's light, or the running blend (floppy 3b13)
	Common::Array<byte> data;
	if (_resources.load("ORNYCAB.HSQ", data)) {
		Sprite cabin(_system, data);
		cabin.setPalette();
		cabin.drawFrame(0, &view, 0, 0);
	}
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);
	_viewOk = true;
}

void GameScreen::showSighting(uint place, byte relativeBearing) {
	// travel_pick_speaking_companion (floppy 3908): the only companion, or
	// one of the two by bit 7 of the clock word. The line is PHRASE12 0x1A0,
	// "Wait a minute, I'm not sure... I think I've just seen \x84 \x85.",
	// with the place's kind and its side (floppy 43A0: left under -8, ahead,
	// right from 8).
	const byte first = _world.companion(0), second = _world.companion(1);
	uint speaker = first != 0xff ? first : second;
	if (first != 0xff && second != 0xff && (_state.w(0) & 0x80))
		speaker = second;
	if (speaker == 0xff || !loadDialogue())
		return;
	const Location l = _world.location(place);
	static const char *const kKinds[4] = { "a sietch", "a palace", "a village", "a fortress" };
	const uint kind = l.type < 0x20 ? 0 : l.type == 0x20 || l.type == 0x30 ? 1 : l.type < 0x28 ? 2 : 3;
	const int r = (int8)relativeBearing;
	const char *side = r < -8 ? "on the left" : r < 8 ? "ahead" : "on the right";
	const uint16 kindId = _panel.findCommand(kKinds[kind]), sideId = _panel.findCommand(side);
	Common::String line;
	const uint16 names = _state.nameTable;
	if (kindId != 0xffff && sideId != 0xffff) {
		const uint16 oldKind = _state.w(names + 8), oldSide = _state.w(names + 10);
		_state.setW(names + 8, (uint16)(kindId + 1));
		_state.setW(names + 10, (uint16)(sideId + 1));
		line = _sentences->text((uint16)(0x800 + 0x1a0 + 1), true, _state);
		_state.setW(names + 8, oldKind);
		_state.setW(names + 10, oldSide);
	}
	if (!line.hasPrefix("Wait a minute"))
		line = Common::String::format("Wait a minute, I'm not sure... I think I've just seen %s %s.",
				kindId != 0xffff ? _panel.commandString(kindId).c_str() : kKinds[kind],
				sideId != 0xffff ? _panel.commandString(sideId).c_str() : side);
	_log.line(Common::String::format("Flight: companion %u sights place %u: %s", speaker, place, line.c_str()));
	openTalk(speaker);
	_cabinView = true;
	_talkLines.clear();
	_talkLine = 0;
	_talkLastPage = line;
	_panel.wrapText(line, 208 - 2 * 16, false, _talkLines);
	startTalkAnimation();
	drawTalk();
	dumpScreen(_system, "flight-sighting");
	// The row closes the talk; the flight goes on toward the place.
	while (!_quitRequested && !isDumpRun() && !isDuneFastHarness()) {
		Common::Event event;
		bool done = false;
		while (pollDuneEvent(_system, event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				_quitRequested = true;
			int row, arrow;
			if (event.type == Common::EVENT_LBUTTONDOWN &&
					_panel.hitTest(event.mouse.x, event.mouse.y, row, arrow) == Panel::kActionCommand && row == 0)
				done = true;
		}
		if (done)
			break;
		update();
		_system->delayMillis(10);
	}
	_cabinView = false;
	_talkAnimating = false;
	_mode = kMap;
}

void GameScreen::testCockpit(int mode) {
	showRoom(1);
	if (mode != 2) {
		openCockpit(false);
		return;
	}
	_world.addCompanion(4); // Gurney, travelling with Paul (ds:10), as COME WITH ME does
	_world.setTravelling(4, true);
	const Location home = _world.location(_world.currentLocation());
	int best = -1;
	uint bestDistance = 0xffff;
	for (uint i = 0; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		const uint d = _world.cellDistance(home.longitude, home.latitude, l.longitude, l.latitude);
		if (l.hidden() && l.discoverPhase != 0xff && d > 2 && d < bestDistance) {
			bestDistance = d;
			best = (int)i;
		}
	}
	if (best < 0) {
		_log.line("Test: no hidden place to sight");
		return;
	}
	const Location target = _world.location((uint)best);
	if (_state.b(GameState::kPhase) < target.discoverPhase)
		_state.setB(GameState::kPhase, target.discoverPhase);
	_log.line(Common::String::format("Test: Gurney aboard, free flight toward hidden place %d (%u cells)", best, bestDistance));
	travelToward((uint16)(target.longitude + (int16)(target.longitude - home.longitude) / 2),
			(int16)(target.latitude + (target.latitude - home.latitude) / 2));
}

void GameScreen::dumpCockpit() {
	const Common::Rect kWindow = windowRect();
	// TAKE AN ORNITHOPTER from the palace front, then a tap on the first
	// known place the window shows.
	travelTo(0);
	showRoom(1);
	openCockpit(false);
	for (uint i = 1; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		int x, y;
		if (l.hidden() || !_map->windowProject(kWindow, _map->centreLongitude(), _map->centreLatitude(), l.longitude,
				l.latitude, x, y) || _map->windowHit(kWindow, _map->centreLongitude(), _map->centreLatitude(), x, y) != (int)i)
			continue;
		cockpitTap(x, y);
		dumpScreen(_system, "orni-cockpit-arrived");
		return;
	}
	_log.line("Cockpit: no known place in the window; Cancel");
	cockpitCancel();
}

} // namespace Dune

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
 * The story's screens: the scripted "Continue..." scenes (seg000:11771 and
 * the action table at cs:1475), the COMM-room message viewer (seg000:283a),
 * the vision messages (seg000:2ad8, 2bd2), the open desert and the Emperor's
 * ending. The rules behind them are in story.cpp; see
 * notes/research/gameplay-rules.md.
 */

#include "common/config-manager.h"
#include "common/system.h"

#include "graphics/paletteman.h"

#include "dune/amiga.h"
#include "dune/debug.h"
#include "dune/dialogue.h"
#include "dune/harness.h"
#include "dune/palace.h"
#include "dune/resource.h"
#include "dune/scene.h"
#include "dune/sky.h"
#include "dune/sprite.h"
#include "dune/world.h"

namespace Dune {

namespace {

bool fastCapture() {
	return (isDumpRun() && !dumpEveryMillis()) || isDuneHarnessRun();
}

enum {
	kIdleSpeakerMillis = 250,   ///< 0x32 timer ticks (seg000:2b5e)
	kIdleDreamMillis = 2247,    ///< 0x1c2 ticks: the vision dream (seg000:2b8a)
	kIdleFirstVisionMillis = 4993 ///< 1000 ticks alone in the desert at phase 0x14 (seg000:2bb3)
};

} // namespace

void GameScreen::presentLine(uint speaker, uint character, uint list, byte mask, TalkKind kind) {
	if (!loadDialogue())
		return;
	openTalk(speaker);
	_talkKind = kind;
	stageIllnessNames(speaker);
	_conversation->start(character, list, mask, true, true);
	advanceConversation();
}

// ---- Scripted scenes -----------------------------------------------------------

void GameScreen::startScene(uint16 cdOffset) {
	Common::Array<byte> bytes;
	if (!_world.sceneScript(cdOffset, bytes)) {
		_log.line(Common::String::format("Scene: script %#x not found", cdOffset));
		return;
	}
	_scene = bytes;
	_sceneCursor = 0;
	_sceneActive = true;
	_sceneReturnRoom = _room;
	_cast.clear();
	_sceneKiss = 0;
	_log.line(Common::String::format("Scene: script %#x starts", cdOffset));
	sceneStep();
}

bool GameScreen::maybeStartScene() {
	if (!_pendingScene || _sceneActive || _mode == kTalk)
		return false;
	const uint16 script = _pendingScene;
	_pendingScene = 0;
	if (fastCapture()) {
		// The dump and harness pictures must not wait on a Continue.
		_log.line(Common::String::format("Scene: script %#x skipped in a capture run", script));
		return false;
	}
	startScene(script);
	return true;
}

void GameScreen::sceneStep() {
	// menu_callback_choice_continue (seg000:171a): the next action byte, a
	// byte offset into the table at cs:1475; 0xff ends the scene.
	auto arg = [&]() -> byte { return _sceneCursor < _scene.size() ? _scene[_sceneCursor++] : 0xff; };
	if (_finalPicture == 1) {
		// seg000:14f7: the second FINAL picture follows the first.
		_finalPicture = 2;
		drawRoom();
		dumpScreen(_system, "final-2");
		return;
	}
	while (_sceneActive && _sceneCursor < _scene.size()) {
		const byte op = _scene[_sceneCursor++];
		if (op == 0xff)
			break;
		switch (op) {
		case 0x00:   // action 00 [room, count, cast...]: the shot (sub_113c8)
		case 0x12: { // action 09: the same through a transition (seg000:148d)
			const byte room = arg();
			const byte count = _sceneCursor < _scene.size() ? _scene[_sceneCursor] : 0;
			_cast.clear();
			for (uint i = 0; i < count && _sceneCursor + 1 + i < _scene.size(); ++i)
				_cast.push_back(_scene[_sceneCursor + 1 + i]);
			_sceneCursor += 1 + count;
			_world.setPosition(_world.currentLocation(), room);
			_room = room;
			refreshRooms();
			updateRoomVars();
			_sceneKiss = 0;
			break; // falls into action 02: redraw and step on
		}
		case 0x04: // action 02: redraw, step on
			break;
		case 0x02: { // action 01 [speaker]: the speaker's next topic-7 line (seg000:9761)
			const byte who = arg();
			presentLine(who, who, 7, 0x80, kTalkScene);
			return;
		}
		case 0x06: // action 03 [speaker]: the head, silent; the next line shows it
			arg();
			break;
		case 0x08: // action 04: wait for " Continue..."
			_mode = kRoom;
			drawRoom();
			dumpScreen(_system, "scene-wait");
			return;
		case 0x0a: // action 05 (seg000:1422): the evening comes, CHANKISS sprite 0
			while (_world.timeSlot() < 13)
				_world.advanceTime(1);
			_sceneKiss = 1;
			_mode = kRoom;
			drawRoom();
			return;
		case 0x0c: // action 06: CHANKISS sprite 1
			_sceneKiss = 2;
			_mode = kRoom;
			drawRoom();
			return;
		case 0x0e: // action 07: the prospector's map (spice density) and his line
		case 0x10: // action 08: the density goes, his next line
			presentLine(World::kFremenChief, World::kFremenChief, 7, 0x80, kTalkScene);
			return;
		case 0x14: // action 0a (seg000:14c9): FINAL.HSQ, sprites 0-2 at the origin, then 3 and 4
			_finalPicture = 1;
			_mode = kRoom;
			drawRoom();
			dumpScreen(_system, "final-1");
			return;
		case 0x16: // action 0b (seg000:167c): the cast list, then the game is over
			_sceneActive = false;
			_finalPicture = 0;
			_endingText.clear();
			emperorEnding();
			return;
		default:
			_log.line(Common::String::format("Scene: action byte %#x not built", op));
			break;
		}
	}
	endScene();
}

void GameScreen::endScene() {
	// seg000:11736: back to the room the scene started in.
	_sceneActive = false;
	_cast.clear();
	_sceneKiss = 0;
	if (_conversation)
		_conversation->stop();
	_talkKind = kTalkNormal;
	delete _talkSheet;
	_talkSheet = nullptr;
	_log.line("Scene: over");
	showRoom(_sceneReturnRoom ? _sceneReturnRoom : _room);
}

void GameScreen::showFinal(uint picture) {
	// seg000:14ac draws FINAL.HSQ sprites 0-2 at (0,0); seg000:1517 sprite 3
	// at (0x34, 0) and seg000:1546 sprite 4 at (0x5a, 0x40).
	Common::Array<byte> data;
	if (!_resources.load("FINAL.HSQ", data))
		return;
	Sprite final(_system, data);
	final.setPalette();
	_surface.fillRect(Common::Rect(0, 0, 320, 152), 0);
	if (_world.amiga()) {
		// The Amiga's FINAL.HSQ: "THE END" (1) over the worm's head (picture
		// 5), as the recording shows it, then "with (in order of
		// Appearance)" (0) at (0x40, 0x34) (code 0x250c).
		if (picture == 1) {
			final.drawFrame(5, _surface.surfacePtr(), 0, 0);
			final.drawFrame(1, _surface.surfacePtr(), 0x5a, 0x40);
		} else {
			final.drawFrame(0, _surface.surfacePtr(), 0x40, 0x34);
		}
	} else if (picture == 1) {
		for (uint16 f = 0; f < 3; ++f)
			final.drawFrame(f, _surface.surfacePtr(), 0, 0);
	} else {
		final.drawFrame(3, _surface.surfacePtr(), 0x34, 0);
		final.drawFrame(4, _surface.surfacePtr(), 0x5a, 0x40);
	}
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);
	_viewOk = true;
}

void GameScreen::drawKiss() {
	// The draw lists at ds:2290 (sprite 0 at 78,33) and ds:2298 (sprite 1 at 26,4).
	Common::Array<byte> data;
	if (!_sceneKiss || !_resources.load("CHANKISS.HSQ", data))
		return;
	Sprite kiss(_system, data);
	kiss.setPalette();
	if (_sceneKiss == 1)
		kiss.drawFrame(0, _surface.surfacePtr(), 78, 33);
	else
		kiss.drawFrame(1, _surface.surfacePtr(), 26, 4);
}

// ---- The COMM room ---------------------------------------------------------------

void GameScreen::openComm(bool seen) {
	// VIEW NEW MESSAGES (seg000:283a) / Messages already seen (283e).
	_commList = seen ? 1 : 0;
	_world.purgeArrivalVisions();
	_mode = kRoom;
	drawRoom();
	dumpScreen(_system, "comm-list");
}

void GameScreen::stageVillageSmugglers() {
	if (_desert || _world.placeType() != Location::kVillageMin)
		return;
	const uint16 harvester = _panel.findCommand("a spice-harvester");
	if (harvester != 0xffff)
		_world.setItemWords((uint16)(harvester + 1));
	_world.stageSmugglers(_world.currentLocation()); // seg000:3166
}

void GameScreen::updateRoomVars() {
	// current_room (ds:0b), current_scene (ds:8) and persons_in_room (ds:12),
	// which the dialogue conditions and the story routines read.
	_state.setB(World::kCurrentRoom, (byte)_room);
	_state.setB(World::kCurrentScene, _desert ? 0xff : _world.placeType());
	uint16 bits = 0;
	if (!_desert) {
		Common::Array<byte> people;
		_world.peopleInRoom(people);
		for (uint i = 0; i < people.size(); ++i)
			if (people[i] < 16)
				bits |= (uint16)(1 << people[i]);
	}
	_state.setW(GameState::kPersonsInRoom, bits);
	stageVillageSmugglers();
	if (!_desert)
		_world.stageLocationForConditions(_world.currentLocation());
}

bool GameScreen::roomEntryScan(bool always) {
	// The room-entry scan (seg000:35b4 via 36ee): with pending_room_action 5
	// from the move (seg000:3fca), the first person here with a topic-4 line
	// whose condition holds says it and the talk opens on them (93df); a
	// person with no such line may deliver a queued vision message instead.
	// Capture runs skip it (@p always: a story step that needs it, the
	// Water of Life's wake-up).
	if ((fastCapture() && !always) || _sceneActive || !loadDialogue())
		return false;
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	for (uint i = 0; i < people.size(); ++i) {
		const uint who = people[i];
		const uint group = MIN<uint>(who, World::kFremenChief);
		if (const uint troop = _world.troopForPerson(who))
			stageTroopForConditions(troop); // the Fremen's lines read their troop
		if (_conversation->hasLine(group, 4, 0x80)) {
			_log.line(Common::String::format("Room: character %u speaks on entry", who));
			presentLine(who, group, 4, 0x80, kTalkNormal);
			_state.setB(0x23, 0);
			return true;
		}
	}
	_state.setB(0x23, 0);
	return false;
}

// ---- The CD's flight views and approach clips -----------------------------------------

bool GameScreen::startCdFlightView() {
	static const char *const kClips[4] = { "MNT1.HNM", "MNT2.HNM", "MNT3.HNM", "MNT4.HNM" };
	for (uint i = 0; i < 4; ++i)
		if (_mntData[i].empty() && !_resources.load(kClips[i], _mntData[i]))
			return false;
	delete _flightVideo;
	_flightVideo = new HnmPlayer(_system);
	_mntClip = 0;
	_mntNextFrame = 0;
	return _flightVideo->begin(_mntData[0]);
}

void GameScreen::drawCdFlightView(byte terrainAhead) {
	if (!_flightVideo)
		return;
	const uint32 now = _system->getMillis();
	if (now >= _mntNextFrame) {
		_mntNextFrame = now + 83; // the HNM frame time without a soundtrack
		if (!_flightVideo->step()) {
			// The clip is over: the next one by the terrain ahead (seg000:4ec6).
			const bool rock = terrainAhead >= 8;
			int next;
			if (!rock)
				next = _mntClip == 0 ? 0 : (_mntClip == 1 || _mntClip == 2) ? 3 : 0;
			else
				next = (_mntClip == 0 || _mntClip == 3) ? 1 : 2;
			_mntClip = next;
			_flightVideo->begin(_mntData[next]);
			_flightVideo->step();
		}
	}
	// The flight clips carry no palette: the CD's SKYDN.HSQ record of the hour
	// colours them, as it does the approach clips and the exterior backdrops.
	if (_map)
		_map->applyPalette(); // the minimap's colours first; the sky's record over them
	setVideoSkyPalette();
	_panel.applyPalette();
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	for (int y = 0; y < 152; ++y)
		memcpy(_surface.getBasePtr(0, y), _flightVideo->screen() + 320 * y, 320);
	if (isDumpRun() && _flightVideo->frameNumber() % 40 == 1) {
		uint lit = 0, pal = 0, lo = 255, hi = 0;
		for (uint i = 0; i < 320 * 152; ++i) {
			const byte c = _flightVideo->screen()[i];
			lit += c != 0;
			if (c) {
				lo = MIN<uint>(lo, c);
				hi = MAX<uint>(hi, c);
			}
		}
		for (uint i = 0; i < 768; ++i)
			pal += _flightVideo->palette()[i];
		_log.line(Common::String::format("Flight view: clip %d frame %u, %u lit pixels, colours %u-%u, palette sum %u",
				_mntClip + 1, _flightVideo->frameNumber(), lit, lo, hi, pal));
	}
	if (isDumpRun() && _flightVideo->frameNumber() % 40 == 1)
		_pendingFlightDump = Common::String::format("cd-flight-mnt%d-%u", _mntClip + 1, _flightVideo->frameNumber());
}

void GameScreen::setVideoSkyPalette() {
	Common::Array<byte> skyData;
	if (_resources.load("SKYDN.HSQ", skyData)) {
		Sprite sky(_system, skyData);
		sky.setPaletteRecord(8 + skyPalette());
	}
}

void GameScreen::stopCdFlightView() {
	delete _flightVideo;
	_flightVideo = nullptr;
	_mntClip = -1;
}

void GameScreen::playArrivalVideo(byte placeType) {
	// travel_arrival_landing_sequence (seg000:488a) on the CD: the approach
	// clip of the place kind to its end (a tap skips it), after which the
	// room keeps its last picture as the backdrop.
	const char *name = World::arrivalVideo(placeType);
	Common::Array<byte> video;
	if (!_resources.load(name, video))
		return;
	setVideoSkyPalette();
	HnmPlayer player(_system);
	const HnmPlayer::Result r = player.play(video, name);
	_log.line(Common::String::format("Travel: %s approach clip (result %d)", name, (int)r));
	if (r == HnmPlayer::kQuit)
		_quitRequested = true;
	_panel.applyPalette();
}

// ---- The open desert ---------------------------------------------------------------

void GameScreen::landInDesert() {
	_desert = true;
	_walking = false;
	if (!_landAtSet) {
		// No landing point given: the landscape around the current place.
		const Location l = _world.location(_world.currentLocation());
		_walkLng = l.longitude;
		_walkLat = l.latitude;
		_walkFine = 0;
	}
	_landAtSet = false;
	_mode = kRoom;
	_menu = kMenuNone;
	_commList = -1;
	_orniFrame = 0;
	updateRoomVars();
	_log.line("Desert: Paul lands in the open desert");
	debugSetScene("desert");
	drawRoom();
	dumpScreen(_system, "desert");
	_idleStart = _system->getMillis();
}

void GameScreen::drawDesert() {
	// The sky over the sand and the landscape around the landing point
	// (desert.cpp). The ornithopter beside Paul is not drawn: TAKE AN
	// ORNITHOPTER stands for its hotspot.
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	_panel.applyPalette();
	if (_world.amiga()) {
		// The executable draws DUNES3 pieces here (code 0x5094, not ported):
		// the sky gradient over the time of day's sand colour.
		_surface.fillRect(Common::Rect(0, 0, 320, 78), 1);
		_surface.fillRect(Common::Rect(0, 78, 320, 152), 2);
		amigaSkyPalette(_system, _resources, _state.w(GameState::kGameTime), true);
		amigaSkyGradient(*_surface.surfacePtr());
	} else {
		Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
		drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette(), true);
		setSkyPalette(false); // the hour's light, or the running blend (floppy 3b13)
		_surface.fillRect(Common::Rect(0, 77, 320, 152), 0xbf); // floppy 3AF8: the sand from y 77
		drawLandscape(view, _walkLng, _walkLat, 0, _walkLng, false);
	}
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);
	_viewOk = true;
}

// ---- Vision messages ---------------------------------------------------------------

void GameScreen::checkIdle(uint32 now) {
	// idle_room_message_check (seg000:2b2a), run while the room waits. A
	// harness run keeps its clock (and so the messages) stopped unless it
	// plays in real time (dune_real_time).
	if ((isDumpRun() && !dumpEveryMillis()) || isDuneFastHarness() || _mode != kRoom || _sceneActive || _commList >= 0 || _ending || _menu != kMenuNone) {
		_idleStart = now;
		return;
	}
	const uint32 idle = now - _idleStart;
	const byte phase = _state.b(GameState::kPhase);
	if (phase < 0x14)
		return;
	if (phase == 0x14) {
		// loc_02ba1: alone in the desert, the first vision.
		if (_desert && !_state.w(GameState::kPersonsWith) && idle >= kIdleFirstVisionMillis) {
			_world.firstVision();
			runPhaseTriggers();
			_idleStart = now;
			presentVision(true);
		}
		return;
	}
	if (!_world.visionCount())
		return;
	uint16 id, location;
	_world.vision(0, id, location);
	const byte sender = id >> 8;
	// seg000:2ad8: the sender in the room delivers it; a troop chief's
	// (sender 0x0f) only at the place the message is about (ds:114e).
	const bool present = sender < 16 && ((_state.w(GameState::kPersonsInRoom) >> sender) & 1) &&
						 (sender != 0x0f || (!_desert && location == _world.placeOffset(_world.currentLocation())));
	if (present && idle >= kIdleSpeakerMillis)
		presentVision(false);
	else if (idle >= kIdleDreamMillis)
		presentVision(true);
}

void GameScreen::presentVision(bool dream) {
	// present_vision_message (seg000:2b00): the fixed block, DIALOGUE
	// character 16 topic 4, with vision_message_type_ds_ea = the id's low
	// byte selecting the sentence and the sender as the speaker; or the dream
	// (seg000:2bd2) over VIS.HSQ.
	if (!_world.visionCount())
		return;
	uint16 id, location;
	_world.vision(0, id, location);
	_world.dequeueVision();
	const byte sender = id >> 8;
	if (!dream)
		_world.purgeVisions(sender, location); // loc_03542: delivered in person
	_state.setB(World::kVisionType, (byte)id);
	_log.line(Common::String::format("Vision: message %#x (%s)", id, dream ? "dream" : "in person"));
	_visionDream = dream;
	_dreamTroop = 0;
	_dreamPlace = -1;
	// The message's place is staged for the conditions (331e: 2b09 in
	// person, 2c1d in the dream); the dream also names it for the text codes
	// 0x81/0x82 (2c20 -> 2e98: "There are saboteurs here in \x81-\x82!") and
	// leaves the names so. In person the staged place is put back (2b25).
	const int where = _world.placeIndex(location);
	if (where >= 0) {
		_world.stageLocationForConditions((uint)where);
		if (dream) {
			const Location l = _world.location((uint)where);
			_state.setW(_state.nameTable + 2, l.firstName);
			_state.setW(_state.nameTable + 4, (uint16)(12 + l.lastName));
			_dreamPlace = where;
		}
	}
	if (dream) {
		// 2c23-2c43 (floppy 2eed-2f0d): a report from a Fremen (sender 0x0e
		// or 0x0f) comes from the troop at the message's place, the place
		// record's byte 9 (troop 3 for message 0x0e), whose chief's head
		// speaks it (lip-sync 0x0e, fremen1_troop_ptr).
		if (sender >= World::kFremen && where >= 0) {
			const uint troop = (byte)id == 0x0e ? (uint)World::kProspectorTroop : _world.location((uint)where).troop;
			if (troop)
				_dreamTroop = troop;
		}
		// vision_dream_backdrop (CD 2c9a) sets ds:E8 = 0 and draws it with the
		// clouds; the floppy's (2f54) leaves the head as it is.
		if (!_world.floppy()) {
			_state.setB(GameState::kHeadIndex, 0);
			_panel.setHeadIndex(0);
		}
		_dreamStart = _system->getMillis();
		// wait_interruptable (CD 2c60: 0xbb8 ticks; floppy 2f25: 0x7d0), 200.3 Hz.
		_dreamUntil = _dreamStart + (_world.floppy() ? 0x7d0u : 0xbb8u) * 4993u / 1000u;
		_dreamPaletteValid = false;
		_dreamStep = 0xffff;
		_log.line(Common::String::format("Vision: the dream, troop %u, place %d, %u ms", _dreamTroop, _dreamPlace,
				_dreamUntil - _dreamStart));
	}
	presentLine(MIN<uint>(sender, World::kFremenChief), 16, 4, 0, kTalkVision);
	if (where >= 0 && !dream)
		_world.stageLocationForConditions(_world.currentLocation());
	_state.setB(World::kVisionType, 0xff);
	_idleStart = _system->getMillis();
}

// ---- The Emperor's ending -------------------------------------------------------------

void GameScreen::emperorEnding() {
	// pending_room_screen_request 7 (seg000:215f): COMMAND "As Paul Atreides
	// failed to respond to my spice demands..." and the game is over.
	_ending = true;
	_log.line(Common::String::format("Story: the ending \"%s...\"", _endingText.c_str()));
	// The CD's game over plays DEAD.HNM with the head going down as its
	// first frame shows (0dc2 -> 0e66 -> 0e6c, bp = 181e); the floppy's has
	// no such call.
	if (!_world.floppy())
		headDown("game over");
	drawEnding();
	dumpScreen(_system, "emperor-ending");
}

void GameScreen::drawEnding() {
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	_panel.applyPalette();
	if (_endingText.empty()) {
		// The final credits (seg000:167c): the cast, COMMAND "Paul Atreides"
		// on. (The original shows each over its own scene.)
		// The cast runs from "Paul Atreides" to "Liet Kynes" (COMMAND
		// 0x122-0x12e on the CD); the search is by the last, unique, name.
		const uint16 kynes = _panel.findCommand("Liet Kynes");
		const uint16 first = kynes != 0xffff && kynes >= 12 ? (uint16)(kynes - 12) : 0xffff;
		int y = 20;
		for (uint16 id = first; first != 0xffff && id < first + 13; ++id, y += 11) {
			const Common::String name = _panel.commandString(id);
			_panel.drawText(_surface, name.c_str(), 160 - (int)_panel.textWidth(name.c_str(), false) / 2, y,
					Panel::kLightColour, false);
		}
		_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
		_system->updateScreen();
		return;
	}
	const uint16 id = _panel.findCommand(_endingText.c_str(), true);
	if (id != 0xffff) {
		Common::String text = _panel.commandString(id);
		for (uint i = 0; i < text.size(); ++i)
			if (text[i] == '\r')
				text.setChar(' ', i);
		Common::Array<Common::String> lines;
		_panel.wrapText(text, 280, false, lines);
		int y = 100 - (int)lines.size() * 5;
		for (uint i = 0; i < lines.size(); ++i, y += 10)
			_panel.drawText(_surface, lines[i].c_str(), 20, y, Panel::kLightColour, false);
	}
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

// ---- FIND PROSPECTORS ---------------------------------------------------------------

void GameScreen::findProspectors() {
	// seg000:5b1e: troop 3, the prospectors: the map centres on them and,
	// when they can be reached, their contact opens as CONTACT FREMEN TROOPS
	// would (seg000:86cc).
	const uint id = 3;
	const Troop t = _world.troop(id);
	if (!t.id || !_map)
		return;
	if (t.location >= Location::kTableOffset)
		_map->centreOn((t.location - Location::kTableOffset) / Location::kRecordSize);
	drawMapScreen();
	const uint here = Location::kTableOffset + _world.currentLocation() * Location::kRecordSize;
	if (_world.contactRange() <= 1 && ((t.occupation & 0x40) || t.location != here))
		return;
	if (_map->density())
		return;
	_lastContacted = id;
	_recruiting = false;
	openTroop(id, true);
}

void GameScreen::storySetup(const Common::String &what) {
	if (!loadDialogue())
		return;
	_log.line(Common::String::format("Story setup: %s", what.c_str()));
	if (what == "gathering") {
		setGamePhase(0x0c);
		_pendingScene = 0;
		startScene(0x1321);
		return;
	}
	if (what == "stillsuit") {
		// Phase 2 (Leto has asked for stillsuits), two troops rallied: at
		// Carthag-Tuek the chief (names 0x205) sends Paul east.
		_state.setB(GameState::kPhase, 2);
		if (!_world.troop(1).hired())
			_world.rallyTroop(1);
		if (!_world.troop(3).hired())
			_world.rallyTroop(3);
		travelTo(12);
		for (uint room = 1; room <= 6; ++room) {
			showRoom(room);
			Common::Array<byte> people;
			_world.peopleInRoom(people);
			for (uint i = 0; i < people.size(); ++i)
				if (people[i] == World::kFremenChief) {
					startConversation(World::kFremenChief);
					return;
				}
		}
		return;
	}
	if (what == "ecology") {
		prepareEcologyTest(true);
		return;
	}
	if (what == "saboteurs") {
		// Harkonnen saboteurs and harvester worms (World::harvesterEvents,
		// CD seg000:714c / 71bc, floppy 7eb5 / 7f25; the hunt 725f / 7fc3):
		// scripts/check_saboteurs.sh. Phase 0x35, Paul at the palace, no
		// Emperor's demands (ds:bf cleared) so the days can pass.
		auto clickRow = [&](RowAction action) -> bool {
			for (uint i = 0; i < Panel::kCommandRows; ++i) {
				if (_rowActions[i] != action)
					continue;
				_log.line(Common::String::format("Story setup: row %u (%s)%s", i, _panel.commandText(i) ? _panel.commandText(i) : "",
						_panel.rowDisabled(i) ? " is greyed" : ""));
				if (_panel.rowDisabled(i))
					return false;
				Common::Event event;
				event.type = Common::EVENT_LBUTTONDOWN;
				event.mouse = Common::Point(160, 163 + 8 * (int)i);
				handleEvent(event);
				return true;
			}
			_log.line("Story setup: no such row");
			return false;
		};
		auto contact = [&](uint id, const char *when) {
			openTroop(id, true);
			_log.line(Common::String::format("Story setup: %s, troop %u says \"%s\"", when, id, _troopLine.c_str()));
			for (uint g = 0; g < 5 && nextTroopLine(); ++g)
				_log.line(Common::String::format("Story setup: %s, troop %u says \"%s\"", when, id, _troopLine.c_str()));
		};
		auto bits = [&](uint id) {
			return READ_LE_UINT16(_state.vars + World::kTroopTable + (id - 1) * World::kTroopSize + 0x10);
		};
		auto speech = [&](uint id) {
			return READ_LE_UINT16(_state.vars + World::kTroopTable + (id - 1) * World::kTroopSize + 0x12);
		};
		_world.firstVision(); // the events and their messages need ds:0a bit 0 (it sets phase 0x15)
		setGamePhase(0x35);
		while (_world.visionCount())
			_world.dequeueVision();
		_state.setB(World::kShipmentFlags, 0);
		_world.setPosition(0, 10);
		uint miner, place;
		if (!_world.prepareSaboteurTest(miner, place)) {
			_log.line("Story setup: no troop the saboteurs can reach");
			return;
		}
		// An orni keeps the worms from the harvester in this part (717a).
		_world.placeTroopForTest(miner, place, Troop::kSpiceMining, 0xc0);
		_log.line(Common::String::format("Story setup: troop %u mines at place %u (region %u), speech %#x", miner, place,
				_world.location(place).firstName, speech(miner)));
		// 1. Periods pass until the saboteurs strike (1 in 8 a day at the troop's slot).
		uint periods = 0;
		for (; periods < 60 * World::kSlotsPerDay && !(bits(miner) & 0x8000); ++periods)
			_world.advanceTime(1);
		_log.line(Common::String::format("Story setup: sabotaged after %u period(s): troop %u occupation %#x bits %#x, place status %#x, %u vision(s)",
				periods, miner, _world.troop(miner).occupation, bits(miner), _world.location(place).status, _world.visionCount()));
		// 2. Paul learns of it: the troop chief's message (vision 0x0f03).
		if (_world.visionCount())
			presentVision(true);
		// 3. Over the map: the troop's lines, then an order it refuses and the greyed MODIFY EQUIPMENT.
		loadDialogue();
		openMap(MapScreen::kFlat, false);
		contact(miner, "sabotaged");
		clickRow(kRowEquipment);
		clickRow(kRowTroopOccupation);
		clickRow(kRowSetOccupation);
		_log.line(Common::String::format("Story setup: after the order, troop %u occupation %#x, answer \"%s\"", miner,
				_world.troop(miner).occupation, _troopLine.c_str()));
		_mode = kMap;
		// 4. The repair: the troop's slot the next day.
		const uint16 stock = _state.w(World::kSpiceStock);
		periods = 0;
		for (; periods < 2 * World::kSlotsPerDay && (bits(miner) & 0x200); ++periods)
			_world.advanceTime(1);
		_log.line(Common::String::format("Story setup: repaired after %u period(s): troop %u occupation %#x bits %#x, stock %u -> %u",
				periods, miner, _world.troop(miner).occupation, bits(miner), stock * 10u, _state.w(World::kSpiceStock) * 10u));
		contact(miner, "repaired");
		_mode = kMap;
		// 5. An army troop at the place hunts the saboteurs down.
		uint hunter = 0;
		for (uint id = 1; id <= World::kTroops && !hunter; ++id) {
			const Troop t = _world.troop(id);
			if (t.id && id != miner && !t.harkonnen() && _world.troopPlace(id) >= 0 && !(speech(id) & 0x40) &&
					(uint)_world.troopPlace(id) != _world.currentLocation())
				hunter = id;
		}
		if (!hunter) {
			_log.line("Story setup: no troop to hunt the saboteurs");
			return;
		}
		_world.placeTroopForTest(hunter, place, Troop::kMilitaryTraining, 0);
		_log.line(Common::String::format("Story setup: troop %u (army %u) trains at place %u", hunter,
				_world.troop(hunter).armySkill, place));
		periods = 0;
		for (; periods < 0x50 && (_world.location(place).status & 0x04); ++periods)
			_world.advanceTime(1);
		_log.line(Common::String::format("Story setup: saboteurs found after %u period(s): place status %#x, troop %u speech %#x, troop %u speech %#x",
				periods, _world.location(place).status, hunter, speech(hunter), miner, speech(miner)));
		contact(hunter, "the hunt");
		_mode = kMap;
		// 6. Rid of them: no more sabotage for 20 days.
		uint struck = 0;
		for (uint p = 0; p < 20 * World::kSlotsPerDay; ++p) {
			const uint16 before = bits(miner);
			WRITE_LE_UINT16(_state.vars + World::kTroopTable + (miner - 1) * World::kTroopSize + 0x10, before & 0x7fff);
			_world.advanceTime(1);
			if (bits(miner) & 0x8000)
				++struck;
		}
		_log.line(Common::String::format("Story setup: after the hunt, %u sabotage(s) in 20 days", struck));
		// 7. Worms: a harvester without an orni and one with, 40 days at the
		// first sietch of the wormiest region (chance 0x80: 5 or 12).
		uint field = place;
		for (uint i = 0; i < _world.locationCount(); ++i)
			if (_world.location(i).isSietch() && i != _world.currentLocation() && _world.wormChance(i) > _world.wormChance(field))
				field = i;
		_world.prepareFieldForTest(field);
		_state.vars[World::kTroopTable + (miner - 1) * World::kTroopSize + 0x19] &= 0x80; // the orni stays behind
		_world.placeTroopForTest(miner, field, Troop::kSpiceMining, 0x80);
		_world.placeTroopForTest(hunter, field, Troop::kSpiceMining, 0xc0);
		_log.line(Common::String::format("Story setup: worms at place %u (region %u, chance %#x a day)", field,
				_world.location(field).firstName, _world.wormChance(field)));
		uint outcomes[5] = { 0, 0, 0, 0, 0 };
		for (uint p = 0; p < 40 * World::kSlotsPerDay; ++p) {
			const uint16 a = bits(miner), b = bits(hunter);
			const byte pop = _state.vars[World::kTroopTable + (miner - 1) * World::kTroopSize + 0x1a];
			const byte kit = _world.troop(miner).equipment;
			_world.advanceTime(1);
			if ((bits(hunter) & 0x4000) && !(b & 0x4000))
				++outcomes[4];
			if (!(bits(miner) & 0x4000) || (a & 0x4000))
				continue;
			if (!(_world.troop(miner).equipment & 0x80) && (kit & 0x80))
				++outcomes[3];
			else if ((bits(miner) & 0x200) && !(a & 0x200))
				++outcomes[2];
			else if (_state.vars[World::kTroopTable + (miner - 1) * World::kTroopSize + 0x1a] < pop)
				++outcomes[1];
			else
				++outcomes[0];
			if (outcomes[3] && outcomes[2] && outcomes[1] && outcomes[0])
				break;
			// The next day's worm is a new one; the harvester comes back.
			contact(miner, "after the worm");
			_mode = kMap;
			WRITE_LE_UINT16(_state.vars + World::kTroopTable + (miner - 1) * World::kTroopSize + 0x10, bits(miner) & 0x0fff);
			WRITE_LE_UINT16(_state.vars + World::kTroopTable + (hunter - 1) * World::kTroopSize + 0x10, bits(hunter) & 0x0fff);
			_state.vars[World::kTroopTable + (miner - 1) * World::kTroopSize + 0x19] |= 0x80;
			_state.vars[World::kTroopTable + (miner - 1) * World::kTroopSize + 0x1a] = MAX<byte>(pop, 20);
		}
		_log.line(Common::String::format("Story setup: worms: %u passed by, %u took men, %u damaged the harvester, %u swallowed it; %u seen by the orni troop",
				outcomes[0], outcomes[1], outcomes[2], outcomes[3], outcomes[4]));
		leaveMap();
		return;
	}
	if (what == "chani" || what == "chani-gurney") {
		chaniSetup(what == "chani-gurney");
		return;
	}
	if (what == "epidemic") {
		epidemicSetup();
		return;
	}
	if (what == "hemispheres") {
		// The north/south quarrel (World::fremenQuarrel, floppy sub_9A58
		// 9A95): two hired troops from both halves at one sietch, Paul at the
		// palace; a day passes, then the chief of the place is contacted and
		// the southern troop is sent home (scripts/check_hemispheres.sh).
		setGamePhase(0x14);
		_world.firstVision(); // the quarrel's message needs the first vision (sub_4BB0, ds:0a bit 0)
		while (_world.visionCount())
			_world.dequeueVision();
		_world.setPosition(0, 10);
		uint north, south, place;
		if (!_world.prepareQuarrelTest(north, south, place)) {
			_log.line("Story setup: no northern and southern troop pair");
			return;
		}
		_log.line(Common::String::format("Story setup: troops %u (north) and %u (south) at place %u", north, south, place));
		_world.advanceTime(World::kSlotsPerDay - _world.timeSlot()); // to the next day's first period
		_log.line(Common::String::format("Story setup: after the day, troop %u occupation %#x speech %#x, troop %u occupation %#x speech %#x, %u vision(s)",
				north, _world.troop(north).occupation, _world.troop(north).dissatisfaction, south,
				_world.troop(south).occupation, _world.troop(south).dissatisfaction, _world.visionCount()));
		loadDialogue();
		openMap(MapScreen::kFlat, false);
		openTroop(north, true);
		for (uint g = 0; g < 3 && nextTroopLine(); ++g)
			; // ASK FOR MORE INFORMATION: "We refuse to work anymore." (condition 513: speech & 0x30)
		leaveMap();
		// In person at the sietch (the chief's list 1: conditions 488/489 read
		// the quarrel bit with the south bit, PHRASE12 248/249).
		_world.setPosition(place, 1);
		showRoom(1);
		talkThrough(World::kFremenChief);
		_world.setPosition(0, 10);
		showRoom(10);
		_world.issueMoveOrder(south, 0);
		_log.line(Common::String::format("Story setup: after the move, troop %u occupation %#x speech %#x", north,
				_world.troop(north).occupation, _world.troop(north).dissatisfaction));
		return;
	}
	if (what == "smugglers") {
		// The smugglers' trade (scripts/check_smugglers.sh; the original's
		// run: a Spice86 capture of the original, notes/smugglers.md):
		// Tuono-Pyons (place 20, its smugglers' record ds:10e9) at phase
		// 0x2c; talk: "let me see" makes the offer, ARGUE once, then ACCEPT
		// the offer on the table. The item goes into the village's stock and
		// the price on the bill. A day later the smuggler wants his bill
		// paid; Duncan pays it from the stock (action 5, event 9).
		const uint village = 20;
		setGamePhase(0x2c);
		travelTo(village);
		const uint16 record = READ_LE_UINT16(&_state.vars[_world.ds(0x10b4)]);
		_log.line(Common::String::format("Story setup: at place %u (type %#x), smugglers' record %#x, stock %u kg",
				_world.currentLocation(), _world.placeType(), record, _world.spiceStock()));
		auto count = [&](uint item) -> uint {
			return _state.vars[Location::kTableOffset + village * Location::kRecordSize + 0x14 + item];
		};
		// Play a talk to its end; @p choices answer the bargaining menus in turn.
		auto talk = [&](uint who, const Common::Array<byte> &choices) {
			startConversation(who);
			uint next = 0;
			for (uint guard = 0; guard < 80 && inConversation(); ++guard) {
				if (_talkBargain) {
					if (next >= choices.size())
						break; // STOP TALKING at the offer
					// 0: ACCEPT a smuggler's bill (party 1), REFUSE anything else.
					const byte c = choices[next++];
					answerQuestion(c ? c : (_conversation->bargainParty() == 1 ? 1 : 2));
					continue;
				}
				if (!talking())
					break;
				advanceConversation();
			}
			if (inConversation())
				endConversation();
		};
		uint before[5];
		for (uint i = 0; i < 5; ++i)
			before[i] = count(i);
		talk(World::kSmuggler, { 3, 1 }); // ARGUE, then ACCEPT
		for (uint i = 0; i < 5; ++i)
			if (count(i) != before[i])
				_log.line(Common::String::format("Story setup: village item %u count %u -> %u", i, before[i], count(i)));
		if (record >= 0x10d8 && record < 0x1140)
			_log.line(Common::String::format("Story setup: bill %u kg, ds:22 = %u, stock %u kg",
					READ_LE_UINT16(&_state.vars[record + 0x0e]) * 10, _state.b(0x22), _world.spiceStock()));
		// The next day: the sold-out goods may be refilled, and the smuggler
		// wants his bill paid first (condition 441 CD / 438 floppy).
		_world.advanceTime(World::kSlotsPerDay - _world.timeSlot());
		showRoom(1);
		talk(World::kSmuggler, {});
		// Duncan, with enough spice: the bill (his list 2, conditions 198-206).
		if (record >= 0x10d8 && record < 0x1140) {
			const uint16 bill = READ_LE_UINT16(&_state.vars[record + 0x0e]);
			if (_world.spiceStock() / 10 <= bill)
				_state.setW(0xa0, (uint16)(bill + 7));
			_log.line(Common::String::format("Story setup: Duncan, stock %u kg, bill %u kg", _world.spiceStock(), bill * 10));
		}
		talk(3, { 0, 0, 0, 0 }); // REFUSE the Emperor's offer if it comes first, ACCEPT the bill
		if (record >= 0x10d8 && record < 0x1140)
			_log.line(Common::String::format("Story setup: after Duncan, bill %u kg, ds:22 = %u, stock %u kg",
					READ_LE_UINT16(&_state.vars[record + 0x0e]) * 10, _state.b(0x22), _world.spiceStock()));
		// Back at the village with the bill paid: a new offer.
		showRoom(1);
		talk(World::kSmuggler, {});
		return;
	}
	if (what == "search-equipment") {
		// GO & SEARCH FOR EQUIPMENT (scripts/check_search_equipment.sh):
		// every step through the troop popup's rows over the map, as a
		// player's taps. Troop 1 (Carthag-Tuek) in ecology looks for bulbs;
		// troop 2 (Carthag-Harg) in the army for krys knives.
		auto clickRow = [&](RowAction action) -> bool {
			for (uint i = 0; i < Panel::kCommandRows; ++i) {
				if (_rowActions[i] != action)
					continue;
				if (_panel.rowDisabled(i))
					_log.line(Common::String::format("Story setup: row %u (%s) is greyed", i, _panel.commandText(i) ? _panel.commandText(i) : ""));
				Common::Event event;
				event.type = Common::EVENT_LBUTTONDOWN;
				event.mouse = Common::Point(160, 163 + 8 * (int)i);
				handleEvent(event);
				return true;
			}
			_log.line("Story setup: no such row");
			return false;
		};
		auto place = [&](uint index, uint offset) -> byte & {
			return _state.vars[Location::kTableOffset + index * Location::kRecordSize + offset];
		};
		// The item only at the nearest other sietch 16-49 cells away (made
		// known), or nowhere; @p atHome also puts one at the troop's own place.
		auto stock = [&](uint from, uint item, bool anywhere, bool atHome) -> int {
			int best = -1;
			uint bestDistance = 0xffff;
			const Location h = _world.location(from);
			for (uint i = 2; i < _world.locationCount(); ++i) {
				place(i, 0x14 + item) = 0;
				const Location l = _world.location(i);
				if (i == from || !l.isSietch())
					continue;
				const uint d = MAX<uint>((uint)ABS((int16)(uint16)(l.longitude - h.longitude)) >> 8, (uint)ABS(l.latitude - h.latitude));
				// 16 cells or more, so the march does not end within the order's 7 sub-steps.
				if (d >= 16 && d < 50 && d < bestDistance) {
					bestDistance = d;
					best = (int)i;
				}
			}
			if (atHome)
				place(from, 0x14 + item) = 1;
			if (!anywhere || best < 0)
				return -1;
			place((uint)best, 10) &= 0x7f;
			place((uint)best, 0x14 + item) = 1;
			return best;
		};
		auto contact = [&](uint id, const char *when) {
			openTroop(id, true);
			_log.line(Common::String::format("Story setup: %s, troop %u says \"%s\"", when, id, _troopLine.c_str()));
			for (uint g = 0; g < 4 && nextTroopLine(); ++g)
				_log.line(Common::String::format("Story setup: %s, troop %u says \"%s\"", when, id, _troopLine.c_str()));
			_mode = kMap;
		};
		auto order = [&](uint id) {
			const byte before = _world.troop(id).motivation;
			openTroop(id, true);
			clickRow(kRowTroopOccupation);
			clickRow(kRowSearchEquipment);
			const byte motivation = _world.troop(id).motivation;
			_log.line(Common::String::format("Story setup: after the order, troop %u occupation %#x equipment %#x at place %d, motivation %u -> %u, answer \"%s\"",
					id, _world.troop(id).occupation, _world.troop(id).equipment, _world.troopPlace(id), before, motivation, _troopLine.c_str()));
			_mode = kMap;
		};
		// Marches until the troop is home again (at most 24 periods).
		auto march = [&](uint id, int target, bool takeAway) {
			bool turned = false;
			for (uint p = 0; p < 24 && (_world.troop(id).occupation & 0x40); ++p) {
				if (takeAway && target >= 0)
					place((uint)target, 0x14 + (READ_LE_UINT16(_state.vars + World::kTroopTable + (id - 1) * World::kTroopSize + 0x0e) & 0xff)) = 0;
				_world.advanceTime(1);
				const bool back = _world.troopPlace(id) != target;
				if (back && !turned && (_world.troop(id).occupation & 0x40)) {
					turned = true;
					contact(id, "on the way back");
				}
			}
			_log.line(Common::String::format("Story setup: troop %u home at place %d, occupation %#x, equipment %#x", id,
					_world.troopPlace(id), _world.troop(id).occupation, _world.troop(id).equipment));
		};
		setGamePhase(0x14);
		_world.setPosition(0, 10);
		for (uint id = 1; id <= 2; ++id)
			if (!_world.troop(id).hired())
				_world.rallyTroop(id);
		_world.setTroopOccupation(1, Troop::kIrrigation);
		_world.setTroopOccupation(2, Troop::kMilitaryTraining);
		_state.vars[World::kTroopTable + 0 * World::kTroopSize + 0x19] = 0;
		_state.vars[World::kTroopTable + 1 * World::kTroopSize + 0x19] = 0;
		const uint home1 = (uint)MAX(0, _world.troopPlace(1)), home2 = (uint)MAX(0, _world.troopPlace(2));
		_log.line(Common::String::format("Story setup: troop 1 at place %u (occupation %#x), troop 2 at place %u (occupation %#x)",
				home1, _world.troop(1).occupation, home2, _world.troop(2).occupation));
		openMap(MapScreen::kFlat, false);
		// 1. Below phase 0x10 the row is greyed (seg000:69f6).
		_state.setB(GameState::kPhase, 0x0f);
		openTroop(1, true);
		clickRow(kRowTroopOccupation);
		clickRow(kRowSearchEquipment);
		_log.line(Common::String::format("Story setup: phase 0xf, troop 1 occupation %#x", _world.troop(1).occupation));
		_mode = kMap;
		_state.setB(GameState::kPhase, 0x14);
		// 2. No bulbs anywhere: "I don't think I can find some bulbs ..." (0x0e).
		stock(home1, 6, false, false);
		order(1);
		// 3. Bulbs at the nearest sietch: the march, the pickup, the way back.
		const int target1 = stock(home1, 6, true, false);
		_log.line(Common::String::format("Story setup: bulbs at place %d", target1));
		order(1);
		contact(1, "on the way out");
		march(1, target1, false);
		_log.line(Common::String::format("Story setup: place %d bulbs left %u", target1, target1 >= 0 ? place((uint)target1, 0x1a) : 0));
		// 4. Everything the class needs: "I have all the equipment I need!" (0x0f).
		order(1);
		// 5. Krys at the nearest sietch, gone before troop 2 arrives: back with nothing.
		const int target2 = stock(home2, 2, true, false);
		_log.line(Common::String::format("Story setup: krys at place %d", target2));
		order(2);
		march(2, target2, true);
		// 6. Krys at troop 2's own place: the CD takes them there (0x0c);
		// the floppy only searches elsewhere, and finds none (0x0e).
		stock(home2, 2, false, true);
		order(2);
		leaveMap();
		return;
	}
	if (what == "celimyn") {
		// Celimyn-Tuek (names 0x0c, 0x05): its discovery phase (location
		// byte 0x0b) at new game, whether the flight search would find it
		// just before and at phase 0x58, and the byte after a save and
		// reload with the in-memory byte put back to the original's 0xff
		// (scripts/check_celimyn_tuek.sh, option off and on).
		int place = -1;
		for (uint i = 0; i < _world.locationCount() && place < 0; ++i)
			if (_world.location(i).firstName == 0x0c && _world.location(i).lastName == 0x05)
				place = (int)i;
		if (place < 0) {
			_log.line("Story setup: Celimyn-Tuek not found");
			return;
		}
		const byte phase = _state.b(GameState::kPhase);
		_log.line(Common::String::format("Story setup: Celimyn-Tuek is place %d, status %#x, discovery phase %#x", place,
				_world.location(place).status, _world.location(place).discoverPhase));
		_state.setB(GameState::kPhase, 0x57);
		const bool before = _world.discoverable(place);
		_state.setB(GameState::kPhase, 0x58);
		const bool at = _world.discoverable(place);
		_state.setB(GameState::kPhase, phase);
		_log.line(Common::String::format("Story setup: Celimyn-Tuek findable at phase 0x57 %s, at 0x58 %s",
				before ? "yes" : "no", at ? "yes" : "no"));
		_state.vars[Location::kTableOffset + place * Location::kRecordSize + 11] = 0xff;
		saveSlot(2);
		loadSlot(2);
		_log.line(Common::String::format("Story setup: Celimyn-Tuek after a save with 0xff and a load: discovery phase %#x",
				_world.location(place).discoverPhase));
		return;
	}
	if (what == "letodead") {
		// Right after the Duke's death (phase 0x4c, CD sub_11166): the throne
		// room. As in the original Leto still stands there and is listed;
		// with dune_fix_leto_loop he is gone (scripts/check_leto_loop.sh).
		setGamePhase(0x4c);
		_pendingScene = 0;
		while (_world.visionCount())
			_world.dequeueVision();
		_world.setPosition(0, 10);
		showRoom(10);
		Common::Array<byte> people;
		_world.peopleInRoom(people);
		Common::String list;
		for (uint i = 0; i < people.size(); ++i)
			list += Common::String::format(" %u", people[i]);
		_log.line(Common::String::format("Story setup: the throne room after Leto's death, people:%s", list.c_str()));
		return;
	}
	if (what == "ecology-win") {
		ecologyWinSetup();
		return;
	}
	if (what == "endless-play" || what == "final-battle") {
		endlessPlaySetup(what == "endless-play");
		return;
	}
	if (what == "water-of-life") {
		waterOfLifeSetup();
		return;
	}
	if (what == "comm") {
		_state.setB(GameState::kPhase, 0x14);
		_world.firstVision();
		runPhaseTriggers();
		while (_world.visionCount())
			_world.dequeueVision();
		byte *duncan = _state.vars + World::kCharacterTable + 3 * World::kCharacterSize;
		duncan[0] = 8;
		duncan[1] = Location::kPalace;
		duncan[3] = 1;
		_state.setW(World::kSpiceStock, 200);
		_world.setPosition(0, 8);
		showRoom(8);
	}
}

// ---- The Water of Life (scripts/check_water_of_life.sh) --------------------------------

void GameScreen::waterOfLifeSetup() {
	// Stilgar's offer (DIALOGUE 5 list 1, conditions 299-301: a sietch,
	// room 4, ds:0a bit 1 clear), answered three ways: REFUSE, ACCEPT with
	// charisma >= 100 (Paul lives, CD seg000:2ccf / floppy 2f96), then
	// Jessica's lesson (a186: unlimited range), and ACCEPT below 100 (the
	// death, ds:46d9 / floppy ds:4235 = 3). Stilgar travels with Paul.
	_world.firstVision();
	while (_world.visionCount())
		_world.dequeueVision();
	setGamePhase(0x50);
	_pendingScene = 0;
	// Stilgar met (ds:0a bit 4, persons_met ds:0e bit 5), Jessica too.
	_state.vars[World::kPaulEvents] |= 0x10;
	_state.setW(GameState::kPersonsMet, (uint16)(_state.w(GameState::kPersonsMet) | 0x22));
	int sietch = -1;
	for (uint i = 2; i < _world.locationCount() && sietch < 0; ++i) {
		const Location l = _world.location(i);
		if (!l.isSietch())
			continue;
		_world.setPosition(i, 1);
		refreshRooms();
		if (_rooms.size() >= 4)
			sietch = (int)i;
	}
	if (sietch < 0) {
		_log.line("Story setup: no sietch with a room 4");
		return;
	}
	auto events = [&]() { return _state.b(World::kPaulEvents); };
	auto report = [&](const char *when) {
		_log.line(Common::String::format("Story setup: %s: ds:0a %#x, ds:d5 %#x, charisma %u, range %u, day %u period %u",
				when, events(), _state.b(0xd5), _state.b(0x29), _world.contactRange(), _world.day(), _world.timeSlot()));
	};
	Common::String logged;
	auto say = [&](const char *who) {
		if (_mode == kTalk && !_talkLastPage.empty() && _talkLastPage != logged && (logged = _talkLastPage, true))
			_log.line(Common::String::format("Story setup: %s says \"%s\"", who, _talkLastPage.c_str()));
	};
	// Talk to Stilgar until his question, answer it, then to the end.
	auto offer = [&](byte choice) {
		_world.setPosition((uint)sietch, 4);
		showRoom(4);
		startConversation(5);
		for (uint g = 0; g < 20 && _mode == kTalk && !_talkBargain; ++g) {
			say("Stilgar");
			if (_talkEnded)
				break;
			advanceConversation();
		}
		if (!_talkBargain) {
			_log.line("Story setup: Stilgar asks nothing");
			return;
		}
		say("Stilgar");
		answerQuestion(choice);
		for (uint g = 0; g < 20 && _mode == kTalk && !_talkEnded; ++g) {
			say("Stilgar");
			advanceConversation();
		}
		say("Stilgar");
		if (inConversation())
			endConversation();
	};
	_state.setW(GameState::kPersonsWith, (uint16)(_state.w(GameState::kPersonsWith) | 0x20));
	_state.vars[World::kPaulEvents] &= (byte)~0x0a;
	_state.setB(0x29, 120);
	_log.line(Common::String::format("Story setup: Stilgar at place %d room 4 (type %#x)", sietch, _world.placeType()));
	report("start");
	_log.line("Story setup: case 1, REFUSE");
	offer(2);
	report("after REFUSE");
	_log.line("Story setup: case 2, ACCEPT with charisma 120");
	offer(1);
	report("after ACCEPT");
	// Jessica's lesson with ds:d5 = 0xff (her lines 76-78, event 8, a186).
	_state.setW(GameState::kPersonsWith, (uint16)(_state.w(GameState::kPersonsWith) | 0x02));
	showRoom(_world.room());
	startConversation(1);
	for (uint g = 0; g < 20 && _mode == kTalk && !_talkEnded; ++g) {
		say("Jessica");
		advanceConversation();
	}
	say("Jessica");
	if (inConversation())
		endConversation();
	report("after Jessica");
	// A day later ds:d5 has not moved (0 or 0xff stay, seg000:1c62).
	_world.advanceTime(World::kSlotsPerDay);
	report("a day later");
	_log.line("Story setup: case 3, ACCEPT with charisma 50");
	_state.setW(GameState::kPersonsWith, (uint16)(_state.w(GameState::kPersonsWith) & ~0x02));
	_state.vars[World::kPaulEvents] &= (byte)~0x0a;
	_state.setB(0x29, 50);
	offer(1);
	report("after the fatal ACCEPT");
}

// ---- The end-game paths (scripts/check_ecology_win.sh, check_endless_play.sh) ---------

namespace {
Common::String placeLine(World &world, uint index) {
	const Location l = world.location(index);
	return Common::String::format("place %u type %#x status %#x density %u", index, l.type, l.status, l.spiceDensity);
}
} // namespace

void GameScreen::ecologyWinSetup() {
	// The vegetation's own route to the end (queue item 3b). Every fort but
	// one is taken; a sietch's vegetation disc is pointed at the last fort,
	// then at the Harkonnen palace. The disc's callback (CD seg000:653a,
	// floppy 72da) takes a Harkonnen place under it through the fortress-won
	// routine (CD 7443, floppy 81a7): the last fort starts the final attack
	// (ds:c2 = 1, 7493 / 81f7) and Thufir says so. The CD spares the palace
	// (6582: cmp di, 138h); the floppy takes it, and the Baron's hall is
	// open: Paul walks in and the game ends.
	firstVisionForSetup();
	setGamePhase(0x58);
	_pendingScene = 0;
	int fort = -1, sietch = -1;
	for (uint i = 2; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (fort < 0 && l.isFortress())
			fort = (int)i;
		else if (sietch < 0 && l.isSietch() && i != 0)
			sietch = (int)i;
	}
	if (fort < 0 || sietch < 0) {
		_log.line("Story setup: no fort or sietch");
		return;
	}
	const uint left = _world.takeFortsForTest(fort);
	_log.line(Common::String::format("Story setup: forts taken but place %d, %u Harkonnen place(s) left, stage %u, %s",
			fort, left, _state.b(0xc2), placeLine(_world, (uint)fort).c_str()));
	_world.greenOverForTest((uint)sietch, (uint)fort, 2);
	_log.line(Common::String::format("Story setup: after the vegetation at place %d: stage %u, %s", fort,
			_state.b(0xc2), placeLine(_world, (uint)fort).c_str()));
	// Thufir's advice (DIALOGUE 2 list 1, condition 127: ds:c2 == 1).
	byte *thufir = _state.vars + World::kCharacterTable + 2 * World::kCharacterSize;
	thufir[0] = 10;
	thufir[1] = Location::kPalace;
	thufir[2] = 0x80;
	thufir[3] = 1;
	_state.setB(World::kUnread, 0); // no COMM message waiting ("View the message before anything else.")
	_world.setPosition(0, 10);
	showRoom(10);
	Common::String logged;
	startConversation(2);
	for (uint g = 0; g < 30 && _mode == kTalk && !_talkEnded; ++g) {
		if (_talkLastPage != logged) {
			logged = _talkLastPage;
			_log.line(Common::String::format("Story setup: Thufir says \"%s\"", logged.c_str()));
		}
		advanceConversation();
	}
	if (inConversation())
		endConversation();
	// A day on: no demand from the Emperor once ds:c2 is set (seg000:20ae).
	const uint16 shipments = _state.b(World::kShipments);
	_world.advanceTime(World::kSlotsPerDay);
	_log.line(Common::String::format("Story setup: a day later, demands %u -> %u, stage %u", shipments,
			_state.b(World::kShipments), _state.b(0xc2)));
	// The palace under the disc.
	_world.greenOverForTest((uint)sietch, 1, 2);
	const bool held = _world.friendlyPlace(1);
	_log.line(Common::String::format("Story setup: after the vegetation at the Harkonnen palace: %s, %s, stage %u",
			placeLine(_world, 1).c_str(), held ? "taken" : "still Harkonnen", _state.b(0xc2)));
	// Paul flies in (the arrival rule, seg000:503c) and walks to room 2.
	forceRules(true);
	_world.setPosition(1, 1);
	if (arrivalIsFatal(1)) {
		_log.line("Story setup: Paul is shot at the Harkonnen palace");
		return;
	}
	_log.line("Story setup: Paul lands at the Harkonnen palace");
	showRoom(1);
	showRoom(2);
	_log.line(Common::String::format("Story setup: in room 2 of the palace, phase %#x", _state.b(GameState::kPhase)));
}

void GameScreen::epidemicSetup() {
	// The Fremen epidemic (FINDINGS.md "The Fremen epidemic";
	// scripts/check_epidemic.sh): phase 0x5c with three hired troops at two
	// sietches (two at the first), Paul at the palace. The days pass until
	// the picker strikes the first sietch, then the second; the chief's
	// message, the contact lines and greyed rows, Stilgar's line; Chani is
	// brought to each sietch and told to stay; the cures, phase 0x60; a
	// cured troop's contact brings phase 0x64: Chani held in a fortress.
	enum { Stilgar = 5, Chani = 7 };
	auto speech = [&](uint id) {
		return READ_LE_UINT16(_state.vars + World::kTroopTable + (id - 1) * World::kTroopSize + 0x12);
	};
	auto state = [&](const char *when, uint a, uint b, uint c) {
		_log.line(Common::String::format("Story setup: %s: day %u slot %u, phase %#x, ds:f8 %u, ds:f9 %#x, ill place %d; troop %u occ %#x speech %#x, troop %u occ %#x speech %#x, troop %u occ %#x speech %#x",
				when, _world.day(), _world.timeSlot(), _state.b(GameState::kPhase), _state.b(0xf8), _state.b(0xf9), _world.illnessPlace(),
				a, _world.troop(a).occupation, speech(a), b, _world.troop(b).occupation, speech(b), c, _world.troop(c).occupation, speech(c)));
	};
	auto clickRow = [&](RowAction action) -> bool {
		for (uint i = 0; i < Panel::kCommandRows; ++i) {
			if (_rowActions[i] != action)
				continue;
			_log.line(Common::String::format("Story setup: row %u (%s)%s", i, _panel.commandText(i) ? _panel.commandText(i) : "",
					_panel.rowDisabled(i) ? " is greyed" : ""));
			if (_panel.rowDisabled(i))
				return false;
			Common::Event event;
			event.type = Common::EVENT_LBUTTONDOWN;
			event.mouse = Common::Point(160, 163 + 8 * (int)i);
			handleEvent(event);
			return true;
		}
		return false;
	};
	auto contact = [&](uint id, const char *when) {
		openTroop(id, true);
		_log.line(Common::String::format("Story setup: %s, troop %u says \"%s\"", when, id, _troopLine.c_str()));
		for (uint g = 0; g < 6 && nextTroopLine(); ++g)
			_log.line(Common::String::format("Story setup: %s, troop %u says \"%s\"", when, id, _troopLine.c_str()));
	};
	auto closeContact = [&]() {
		Common::Event event;
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_ESCAPE;
		handleEvent(event);
		_mode = kRoom;
	};
	auto talkTo = [&](uint who) {
		startConversation(who);
		for (uint guard = 0; talking() && guard < 40; ++guard)
			advanceConversation();
	};
	auto verb = [&](uint who) {
		_talkWho = who;
		_mode = kTalk;
		companionVerb();
		for (uint guard = 0; talking() && guard < 40; ++guard)
			advanceConversation();
		endConversation();
	};
	// Periods pass until: 1 one ill place, 2 two, 3 troop a cured, 4 phase 0x60.
	uint a = 0;
	auto done = [&](uint until) -> bool {
		switch (until) {
		case 1: return _state.b(0xf8) >= 1;
		case 2: return _state.b(0xf8) >= 2;
		case 3: return (speech(a) & 0x800) != 0;
		default: return _state.b(GameState::kPhase) >= 0x60;
		}
	};
	auto passUntil = [&](uint maxPeriods, uint until) -> uint {
		uint periods = 0;
		for (; periods < maxPeriods && !done(until); ++periods)
			_world.advanceTime(1);
		applyStory();
		return periods;
	};

	_world.firstVision();
	setGamePhase(0x5c);
	while (_world.visionCount())
		_world.dequeueVision();
	_state.setB(World::kShipmentFlags, 0);
	_world.setPosition(0, 10);
	// Three Fremen troops at two sietches away from the palace, not Tuono-Timin
	// (place 16, which the CD's picker leaves out).
	uint ids[3] = { 0, 0, 0 };
	uint places[2] = { 0, 0 };
	uint found = 0;
	for (uint id = 1; id <= World::kTroops && found < 2; ++id) {
		const Troop t = _world.troop(id);
		const int at = _world.troopPlace(id);
		if (!t.id || t.harkonnen() || at <= 0 || at == 16 || !_world.location((uint)at).isSietch())
			continue;
		if (found == 1 && (uint)at == places[0])
			continue;
		ids[found] = id;
		places[found++] = (uint)at;
	}
	for (uint id = 1; id <= World::kTroops && found == 2 && !ids[2]; ++id) {
		const Troop t = _world.troop(id);
		if (t.id && !t.harkonnen() && id != ids[0] && id != ids[1] && _world.troopPlace(id) >= 0)
			ids[2] = id;
	}
	if (!ids[2]) {
		_log.line("Story setup: no troops for the epidemic");
		return;
	}
	a = ids[0];
	const uint b = ids[2], c = ids[1];
	const uint p = places[0], q = places[1];
	for (uint k = 0; k < 3; ++k)
		if (!_world.troop(ids[k]).hired())
			_world.rallyTroop(ids[k]);
	_world.placeTroopForTest(a, p, Troop::kSpiceMining, 0);
	_world.placeTroopForTest(b, p, Troop::kMilitaryTraining, 0);
	_world.placeTroopForTest(c, q, Troop::kSpiceMining, 0);
	_world.markDiscovered(p);
	_world.markDiscovered(q);
	_world.setLocationStatus(p, _world.location(p).status & 0x7f);
	_world.setLocationStatus(q, _world.location(q).status & 0x7f);
	_log.line(Common::String::format("Story setup: troops %u and %u at place %u, troop %u at place %u; illness from day %u",
			a, b, p, c, q, (uint)_state.w(0x1156) + 1));
	state("start", a, b, c);

	// 1. The first sietch falls ill (1e43): the one with the most working troops.
	uint periods = passUntil(6 * World::kSlotsPerDay, 1);
	state(Common::String::format("first illness after %u period(s)", periods).c_str(), a, b, c);
	_log.line(Common::String::format("Story setup: motivation modifier of troop %u at phase %#x: %u (motivation %u)", a,
			_state.b(GameState::kPhase), _world.motivationModifier(a), _world.troop(a).motivation));
	if (_world.visionCount())
		presentVision(true); // "There is a strange disease here in ..."
	endConversation();
	// 2. A day later it spreads to the next sietch (the phase is still 0x5c).
	periods = passUntil(2 * World::kSlotsPerDay, 2);
	state(Common::String::format("second illness after %u period(s)", periods).c_str(), a, b, c);
	while (_world.visionCount())
		_world.dequeueVision();
	// 3. Over the map: the ill troop's lines, its greyed rows.
	openMap(MapScreen::kFlat, false);
	contact(a, "ill");
	clickRow(kRowTroopOccupation);
	clickRow(kRowEquipment);
	clickRow(kRowMoveTroop);
	closeContact();
	leaveMap();
	// 4. Stilgar names the latest place (94f3 -> 9519): "We have to go to ... to stem the epidemic."
	talkTo(Stilgar);
	endConversation();
	// 5. Chani is brought to the first sietch's room 2 and told to stay.
	_state.setW(GameState::kPersonsWith, _state.w(GameState::kPersonsWith) | (1 << Chani));
	_world.addCompanion(Chani);
	travelTo(p);
	showRoom(2);
	talkTo(Chani);
	verb(Chani); // STAY HERE: "OK Paul! I'm staying here to cure the Fremen..." (action 11: phase 0x5d)
	state("Chani stays", a, b, c);
	showRoom(1); // away from her room, or the cure stands still (ds:12 bit 7)
	updateRoomVars();
	periods = passUntil(3 * World::kSlotsPerDay, 3);
	state(Common::String::format("first cure after %u period(s)", periods).c_str(), a, b, c);
	if (_world.visionCount())
		presentVision(true); // "Paul, I'm so happy! I've managed to cure everybody, here in ..."
	endConversation();
	// 6. The second sietch: COME WITH ME at the first (her first-meeting
	// lines, with their phase action, are left alone: a real game has said
	// them), STAY HERE at the second.
	showRoom(2);
	verb(Chani);
	travelTo(q);
	showRoom(2);
	talkTo(Chani);
	verb(Chani);
	showRoom(1);
	updateRoomVars();
	periods = passUntil(3 * World::kSlotsPerDay, 4);
	state(Common::String::format("second cure after %u period(s)", periods).c_str(), a, b, c);
	while (_world.visionCount())
		_world.dequeueVision();
	const byte *r = _state.vars + World::kCharacterTable + Chani * World::kCharacterSize;
	_log.line(Common::String::format("Story setup: Chani's record %02x %02x %02x %02x", r[0], r[1], r[2], r[3]));
	// 7. A cured troop's contact: phase 0x64, Chani held in a fortress, Feyd-Rautha's message.
	travelTo(0);
	showRoom(1);
	openMap(MapScreen::kFlat, false);
	contact(c, "cured");
	closeContact();
	leaveMap();
	_log.line(Common::String::format("Story setup: after the contact, phase %#x, Chani's record %02x %02x %02x %02x, ds:f2 %#x, troop %u speech %#x",
			_state.b(GameState::kPhase), r[0], r[1], r[2], r[3], _state.w(0xf2), c, speech(c)));
	_log.line(Common::String::format("Story setup: motivation modifier of troop %u at phase %#x: %u (motivation %u)", c,
			_state.b(GameState::kPhase), _world.motivationModifier(c), _world.troop(c).motivation));
	talkTo(Stilgar); // "We all like Chani a lot..."
	endConversation();
}

void GameScreen::chaniSetup(bool gurney) {
	// Chani's story (queue item 7; FINDINGS.md "Chani's kidnapping";
	// scripts/check_chani.sh). The phase callbacks' charisma with its
	// motivation spill (Stilgar 0x2c, Chani 0x48, the worm 0x50: 6f78);
	// phase 0x60 then 0x64 (1f13): Chani held in room 3 of a fortress,
	// Feyd-Rautha's message, every troop's motivation - 40 (at least 10,
	// 6efd); Thufir's and Stilgar's lines; a spy at her fortress ("they had a
	// prisoner", condition 588); the fortress taken (7443): Paul lands, the
	// phase stays; Chani met in room 3: "Oh Paul! I was so scared!"
	// (condition 360, action 12: phase 0x68), COME WITH ME, Stilgar's line.
	// chani-gurney: Gurney's STAY HERE at a sietch with an army troop
	// (condition 288, action 12, once) ends the kidnapping without her; and
	// a sietch lost with Stilgar in it (74b6) holds him in room 3.
	enum { Thufir = 2, Gurney = 4, Stilgar = 5, Chani = 7 };
	const byte *cr = _state.vars + World::kCharacterTable + Chani * World::kCharacterSize;
	auto troopWord = [&](uint id, uint offset) {
		return READ_LE_UINT16(_state.vars + World::kTroopTable + (id - 1) * World::kTroopSize + offset);
	};
	auto motivation = [&](const char *when, uint a, uint b) {
		_log.line(Common::String::format("Story setup: %s: phase %#x, charisma %u; troop %u motivation %u modifier %u; troop %u motivation %u modifier %u",
				when, _state.b(GameState::kPhase), _state.b(World::kCharisma), a, _world.troop(a).motivation,
				_world.motivationModifier(a), b, _world.troop(b).motivation, _world.motivationModifier(b)));
	};
	auto chani = [&](const char *when) {
		_log.line(Common::String::format("Story setup: %s: phase %#x, Chani's record %02x %02x %02x %02x, ds:f2 %#x, with Paul %d",
				when, _state.b(GameState::kPhase), cr[0], cr[1], cr[2], cr[3], _state.w(0xf2),
				(_state.w(GameState::kPersonsWith) >> Chani) & 1));
	};
	auto talkTo = [&](uint who) {
		startConversation(who);
		for (uint guard = 0; talking() && guard < 40; ++guard)
			advanceConversation();
		endConversation();
		applyStory();
	};
	auto verb = [&](uint who) {
		_talkWho = who;
		_mode = kTalk;
		companionVerb();
		for (uint guard = 0; talking() && guard < 40; ++guard)
			advanceConversation();
		endConversation();
		applyStory();
	};
	auto people = [&](const char *when) {
		Common::Array<byte> in;
		_world.peopleInRoom(in);
		Common::String list;
		for (uint i = 0; i < in.size(); ++i)
			list += Common::String::format(" %u", in[i]);
		_log.line(Common::String::format("Story setup: %s: place %u room %u, people:%s", when, _world.currentLocation(),
				_world.room(), list.c_str()));
	};

	firstVisionForSetup();
	_state.setB(World::kShipmentFlags, 0);
	_world.setPosition(0, 10);
	// Hired troops at sietches away from the palace: a (motivation 80), b
	// (motivation 30, the floor of 10), c the spy or the army troop.
	uint ids[3] = { 0, 0, 0 };
	uint found = 0;
	for (uint id = 1; id <= World::kTroops && found < 3; ++id) {
		const Troop t = _world.troop(id);
		const int at = _world.troopPlace(id);
		if (t.id && !t.harkonnen() && id != World::kProspectorTroop && at > 0 && _world.location((uint)at).isSietch())
			ids[found++] = id;
	}
	if (found < 3) {
		_log.line("Story setup: no troops for Chani's story");
		return;
	}
	const uint a = ids[0], b = ids[1], c = ids[2];
	for (uint k = 0; k < 3; ++k) {
		if (!_world.troop(ids[k]).hired())
			_world.rallyTroop(ids[k]);
		_world.markDiscovered((uint)_world.troopPlace(ids[k]));
	}
	_world.placeTroopForTest(a, (uint)_world.troopPlace(a), Troop::kSpiceMining, 0);
	_world.placeTroopForTest(b, (uint)_world.troopPlace(b), Troop::kSpiceMining, 0);
	_world.setTroopByteForTest(a, 0x15, 80);
	_world.setTroopByteForTest(b, 0x15, 30);
	// 1. The phase callbacks' charisma (110be, 11139, 1117b through 6f78).
	_state.setB(World::kCharisma, 0);
	setGamePhase(0x28);
	motivation("before Stilgar", a, b);
	setGamePhase(0x2c);
	motivation("after Stilgar (phase 0x2c)", a, b);
	setGamePhase(0x44);
	setGamePhase(0x48); // Chani met: the love scene's phase
	motivation("after Chani (phase 0x48)", a, b);
	_log.line(Common::String::format("Story setup: Chani's flags after phase 0x48: %#x (0x10: she follows Paul now and always), ds:1178 %u",
			cr[15], _state.b(0x1178)));
	setGamePhase(0x50); // the first worm ride
	motivation("after the worm (phase 0x50)", a, b);
	_pendingScene = 0;
	_state.setB(World::kCharisma, 60);
	_world.setTroopByteForTest(a, 0x15, 80);
	_world.setTroopByteForTest(b, 0x15, 30);

	// 2. The kidnapping (phase 0x60, the Harkonnen palace; 0x64, 1f13).
	setGamePhase(0x5c);
	setGamePhase(0x60);
	chani("phase 0x60");
	motivation("before the kidnapping", a, b);
	setGamePhase(0x64);
	while (_world.visionCount())
		_world.dequeueVision();
	chani("kidnapped");
	motivation("while Chani is held", a, b);
	const int fort = (int)cr[3] - 1;
	if (cr[0] != 3 || fort < 2 || !_world.location((uint)fort).isFortress()) {
		_log.line("Story setup: Chani is not in a fortress");
		return;
	}
	const uint f = (uint)fort;
	_state.setB(World::kUnread, 0); // Feyd-Rautha's message read (Thufir: "Look at the new message before we continue.")
	talkTo(Thufir);  // "Chani kidnapped by Feyd-Rautha Harkonnen... use espionage troops"
	talkTo(Stilgar); // "We all like Chani a lot..."

	if (gurney) {
		// 3g. Gurney told to stay at a sietch with an army troop (condition
		// 288, ds:66 > 0, action 12 once): the next chapter, 0x68, while
		// Chani is still held.
		const uint p = (uint)_world.troopPlace(c);
		_world.placeTroopForTest(c, p, Troop::kMilitaryTraining, 0);
		_state.setW(GameState::kPersonsWith, _state.w(GameState::kPersonsWith) | (1 << Gurney));
		_world.addCompanion(Gurney);
		travelTo(p);
		showRoom(1);
		verb(Gurney); // STAY HERE: "Good! I'm going to try to teach these Fremen the handling of arms."
		chani("after Gurney's STAY HERE");
		motivation("after Gurney's STAY HERE", a, b);
		talkTo(Stilgar); // "Good to see you with Chani again..."
		// Her line still comes when she is met (360: phase 0x64-0x68): one more chapter.
		_world.winFortForTest(f);
		forceRules(true);
		travelTo(f);
		forceRules(false);
		showRoom(3);
		people("Chani's room");
		talkTo(Chani);
		chani("Chani met after Gurney's skip");
		// 4g. A sietch lost with Stilgar in it (74b6): room 3 of the new fort.
		const uint q = (uint)_world.troopPlace(a);
		byte *sr = _state.vars + World::kCharacterTable + Stilgar * World::kCharacterSize;
		_state.setW(GameState::kPersonsWith, _state.w(GameState::kPersonsWith) & ~(1 << Stilgar));
		sr[0] = 2;
		sr[1] = _world.location(q).type;
		sr[2] = 0x80;
		sr[3] = (byte)(q + 1);
		_world.loseSietchForTest(q);
		_log.line(Common::String::format("Story setup: sietch %u lost: type %#x, Stilgar's record %02x %02x %02x %02x", q,
				_world.location(q).type, sr[0], sr[1], sr[2], sr[3]));
		return;
	}

	// 3. A spy at her fortress (6a45: the nearest hidden fortress within 30
	// cells): "One of my men told me that he was sure they had a prisoner."
	_world.setLocationStatus(f, _world.location(f).status | 0x80);
	int from = -1;
	for (uint i = 2; i < _world.locationCount() && from < 0; ++i) {
		uint dist;
		const Location l = _world.location(i);
		if (l.isSietch() && _world.nearestHiddenHarkonnen(i, dist) == (int)f && dist < 0x1e)
			from = (int)i;
	}
	if (from < 0) {
		_log.line(Common::String::format("Story setup: no sietch sends spies to place %u", f));
		return;
	}
	_world.placeTroopForTest(c, (uint)from, Troop::kMilitaryTraining, 0);
	_world.setTroopByteForTest(c, 0x17, 80); // an army skill that the Harkonnens never catch (72b0)
	_world.markDiscovered((uint)from);
	const bool sent = _world.startEspionage(c);
	_log.line(Common::String::format("Story setup: troop %u at place %d on espionage: %s", c, from, sent ? "marching" : "refused"));
	// It marches, then counts the Harkonnens (72b0: word 0x0c, then bit
	// 0x40 of word 0x10); the prisoner line (588) follows its count (587).
	uint periods = 0;
	for (; periods < 3 * World::kSlotsPerDay && (_world.troopPlace(c) != (int)f || (_world.troop(c).occupation & 0x40) ||
				!(troopWord(c, 0x10) & 0x40)); ++periods)
		_world.advanceTime(1);
	applyStory();
	while (_world.visionCount())
		_world.dequeueVision();
	_log.line(Common::String::format("Story setup: after %u period(s) troop %u is at place %d, occupation %#x, speech %#x", periods, c,
			_world.troopPlace(c), _world.troop(c).occupation, troopWord(c, 0x12)));
	openMap(MapScreen::kFlat, false);
	openTroop(c, true);
	_log.line(Common::String::format("Story setup: the spy says \"%s\"", _troopLine.c_str()));
	for (uint g = 0; g < 8 && nextTroopLine(); ++g)
		_log.line(Common::String::format("Story setup: the spy says \"%s\"", _troopLine.c_str()));
	{
		Common::Event event;
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_ESCAPE;
		handleEvent(event);
		_mode = kRoom;
	}
	leaveMap();

	// 4. Paul cannot land there yet (503c: Harkonnens at the place).
	uint harkonnen = 0, attacking = 0;
	_world.countHostiles(f, harkonnen, attacking);
	_log.line(Common::String::format("Story setup: place %u before the battle: %u Harkonnen troop(s), friendly %d: landing is fatal",
			f, harkonnen, _world.friendlyPlace(f) ? 1 : 0));
	// 5. The fortress taken (7443): landing is safe, Chani is still held.
	_world.winFortForTest(f);
	_world.countHostiles(f, harkonnen, attacking);
	_log.line(Common::String::format("Story setup: place %u taken: status %#x, %u Harkonnen troop(s), friendly %d", f,
			_world.location(f).status, harkonnen, _world.friendlyPlace(f) ? 1 : 0));
	chani("the fortress taken");
	motivation("the fortress taken", a, b);
	// 6. Paul flies there and walks to room 3: she is there; her line frees her.
	forceRules(true);
	travelTo(f);
	forceRules(false);
	_log.line(Common::String::format("Story setup: Paul lands at place %u (room %u)", _world.currentLocation(), _world.room()));
	showRoom(2);
	people("room 2");
	showRoom(3);
	people("room 3");
	talkTo(Chani); // "Oh Paul! I was so scared! ..." (360, action 12: phase 0x68)
	chani("Chani met");
	motivation("Chani met", a, b);
	verb(Chani); // COME WITH ME: "Yes Paul, I want to follow you, now and always."
	chani("COME WITH ME");
	talkTo(Stilgar); // "Good to see you with Chani again. The Fremen have recovered their motivation!"
}

void GameScreen::endlessPlaySetup(bool withPaul) {
	// The endless play (queue item 3c). Stilgar's troops attack the palace
	// (ds:c2 = 6). Without Paul the next period's attack callback (CD
	// seg000:739e -> 73a9, floppy 8102 -> 810d) makes the shield fall: ds:c2
	// 7, the world stops (1b5e), and the Baron's hall ends the game. With
	// Paul there, MASSIVE ATTACK (CD 7317, floppy 807b) goes through the
	// fortress-won routine instead (7419 -> 7429 -> 7443): the palace is
	// held, ds:c2 goes back to 1, two days later it becomes a sietch
	// (6e20), room 2 is only a room, and the Baron, Feyd-Rautha and the
	// Emperor stand there as prisoners.
	firstVisionForSetup();
	setGamePhase(0x58);
	_pendingScene = 0;
	const uint left = _world.takeFortsForTest(-1);
	Common::Array<uint> ids;
	_world.finalBattleForTest(4, ids);
	_log.line(Common::String::format("Story setup: %u Harkonnen place(s) left, %u troop(s) attack the palace, stage %u",
			left, ids.size(), _state.b(0xc2)));
	forceRules(true);
	if (!withPaul) {
		_world.advanceTime(1);
		_log.line(Common::String::format("Story setup: a period later, stage %u, %s", _state.b(0xc2), placeLine(_world, 1).c_str()));
		const byte d5 = _state.b(0xd5);
		_world.advanceTime(World::kSlotsPerDay * 2);
		_log.line(Common::String::format("Story setup: two days later, stage %u, %s, troop %u occupation %#x",
				_state.b(0xc2), placeLine(_world, 1).c_str(), ids.empty() ? 0 : ids[0], ids.empty() ? 0 : _world.troop(ids[0]).occupation));
		(void)d5;
		_world.setPosition(1, 1);
		if (arrivalIsFatal(1)) {
			_log.line("Story setup: Paul is shot at the Harkonnen palace");
			return;
		}
		showRoom(1);
		showRoom(2);
		_log.line(Common::String::format("Story setup: in room 2 of the palace, phase %#x", _state.b(GameState::kPhase)));
		return;
	}
	// Paul rides in: the palace is in battle, he joins it (503c: ds:2b).
	_world.setPosition(1, 1);
	if (arrivalIsFatal(1)) {
		_log.line("Story setup: Paul is shot at the Harkonnen palace");
		return;
	}
	_log.line(Common::String::format("Story setup: Paul at the palace, battle %d", _battle ? 1 : 0));
	if (ConfMan.hasKey("dune_setup_save")) {
		// For the original: this state as Log 1 (an engine-written save).
		showRoom(1);
		saveSlot(0);
		_log.line("Story setup: saved to Log 1 before the battle");
		return;
	}
	bool won = false;
	for (uint attempt = 0; attempt < 6 && !won && !_ending; ++attempt) {
		won = _world.massiveAttack(1);
		_log.line(Common::String::format("Story setup: MASSIVE ATTACK %u: %s, stage %u", attempt + 1, won ? "won" : "not won", _state.b(0xc2)));
		if (battleCheck())
			return;
	}
	if (!won)
		return;
	_battle = false;
	_log.line(Common::String::format("Story setup: after the battle: %s, stage %u, friendly %d", placeLine(_world, 1).c_str(),
			_state.b(0xc2), _world.friendlyPlace(1) ? 1 : 0));
	{
		Common::Array<uint> here;
		_world.troopsAt(1, here);
		Common::String occ;
		for (uint i = 0; i < here.size(); ++i)
			occ += Common::String::format(" %u:%#x", here[i], _world.troop(here[i]).occupation);
		_log.line(Common::String::format("Story setup: troops at the palace:%s", occ.c_str()));
	}
	showRoom(1);
	// Two days, periods one by one (seg000:6e20 on a new day, day >= won + 2).
	for (uint p = 0; p < 2 * World::kSlotsPerDay + 1 && _world.location(1).type >= Location::kFortressMin; ++p)
		_world.advanceTime(1);
	_log.line(Common::String::format("Story setup: day %u: %s, stage %u", _world.day(), placeLine(_world, 1).c_str(), _state.b(0xc2)));
	for (uint c = 9; c <= 11; ++c) {
		const byte *r = _state.vars + World::kCharacterTable + c * World::kCharacterSize;
		_log.line(Common::String::format("Story setup: character %u record %02x %02x %02x %02x", c, r[0], r[1], r[2], r[3]));
	}
	// Liet Kynes there too, to ask him along (his topic-5 line: ds:c2 != 0).
	byte *kynes = _state.vars + World::kCharacterTable + 6 * World::kCharacterSize;
	kynes[0] = 2;
	kynes[1] = _world.location(1).type;
	kynes[2] = 0x80;
	kynes[3] = 2;
	_world.setPosition(1, 1);
	showRoom(1);
	showRoom(2);
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	Common::String list;
	for (uint i = 0; i < people.size(); ++i)
		list += Common::String::format(" %u", people[i]);
	_log.line(Common::String::format("Story setup: in room 2 of the palace, phase %#x, people:%s", _state.b(GameState::kPhase), list.c_str()));
	Common::String logged;
	auto talk = [&](uint who, bool come) {
		startConversation(who);
		for (uint g = 0; g < 30 && _mode == kTalk && !_talkEnded; ++g) {
			if (_talkLastPage != logged) {
				logged = _talkLastPage;
				_log.line(Common::String::format("Story setup: %u says \"%s\"", who, logged.c_str()));
			}
			advanceConversation();
		}
		if (come && _mode == kTalk) {
			companionVerb();
			if (_talkLastPage != logged) {
				logged = _talkLastPage;
				_log.line(Common::String::format("Story setup: %u answers \"%s\"", who, logged.c_str()));
			}
			_log.line(Common::String::format("Story setup: COME WITH ME to %u: %s", who,
					((_state.w(GameState::kPersonsWith) >> who) & 1) ? "comes" : "stays"));
		}
		if (inConversation())
			endConversation();
	};
	talk(9, false);  // the Baron
	talk(10, true);  // Feyd-Rautha
	talk(11, true);  // the Emperor
	talk(6, true);   // Liet Kynes
	const uint16 shipments = _state.b(World::kShipments);
	_world.advanceTime(World::kSlotsPerDay);
	_log.line(Common::String::format("Story setup: a day later, demands %u -> %u, stage %u, phase %#x", shipments,
			_state.b(World::kShipments), _state.b(0xc2), _state.b(GameState::kPhase)));
}

void GameScreen::firstVisionForSetup() {
	_world.firstVision();
	while (_world.visionCount())
		_world.dequeueVision();
}

// ---- MODIFY EQUIPMENT ------------------------------------------------------------

namespace {
// troop_icon_equipment_sprites (ds:192f): harvester, orni, krys, laser guns,
// weirding modules, atomics, bulbs.
const uint16 kEquipmentSprites[7] = { 0x63, 0x68, 0x71, 0x72, 0x73, 0x74, 0x77 };
} // namespace

void GameScreen::drawEquipmentPanel(const Common::Rect &area) {
	// seg000:7cbb: the troop's equipment ("Equipment:"), then the place's
	// unused stock (COMMAND "  Sietch:\r(unused eqp.)", seg000:7d56); an item
	// clicked in one row goes to the other (the equipment spinners,
	// seg000:7e97 / 7eb8). A troop carries at most one of each kind.
	for (uint r = 0; r < 2; ++r)
		for (uint k = 0; k < 7; ++k)
			_equipRects[r][k] = Common::Rect();
	const Troop t = _world.troop(_troopId);
	if (!t.id || !_map || t.location < Location::kTableOffset)
		return;
	const uint index = (t.location - Location::kTableOffset) / Location::kRecordSize;
	byte free[7];
	_world.placeFreeEquipment(index, free);
	auto text = [&](const char *prefix, int y) {
		const uint16 id = _panel.findCommand(prefix, true);
		if (id == 0xffff)
			return;
		Common::String line = _panel.commandString(id);
		for (uint i = 0; i < line.size(); ++i)
			if (line[i] == '\r')
				line.setChar(' ', i);
		_panel.drawText(_surface, line.c_str(), area.left + 2, y, Panel::kDarkColour, false);
	};
	text("Equipment:", area.top + 1);
	int x = area.left + 2;
	for (uint k = 0; k < 7; ++k) {
		if (!(t.equipment & (0x80 >> k)))
			continue;
		const int w = (int)_map->onmapWidth(kEquipmentSprites[k]);
		_map->drawOnmap(_surface, kEquipmentSprites[k], x, area.top + 11);
		_equipRects[0][k] = Common::Rect(x, area.top + 11, x + w, area.top + 27);
		x += w + 3;
	}
	text("  Sietch:", area.top + 31);
	x = area.left + 2;
	for (uint k = 0; k < 7; ++k) {
		if (!free[k])
			continue;
		const int w = (int)_map->onmapWidth(kEquipmentSprites[k]);
		_map->drawOnmap(_surface, kEquipmentSprites[k], x, area.top + 41);
		const Common::String n = Common::String::format("%u", free[k]);
		_panel.drawText(_surface, n.c_str(), x, area.top + 56, Panel::kDarkColour, false);
		_equipRects[1][k] = Common::Rect(x, area.top + 41, x + w, area.top + 63);
		x += w + 3;
	}
}

bool GameScreen::equipmentTap(int x, int y) {
	for (uint k = 0; k < 7; ++k) {
		if (_equipRects[0][k].contains(x, y) && _world.giveEquipment(_troopId, k)) {
			_log.line(Common::String::format("Equipment: troop %u leaves item %u", _troopId, k));
			return true;
		}
		if (_equipRects[1][k].contains(x, y) && _world.takeEquipment(_troopId, k)) {
			_log.line(Common::String::format("Equipment: troop %u takes item %u", _troopId, k));
			return true;
		}
	}
	return false;
}

// ---- The ecology test (dump tour and regression) -------------------------------------

void GameScreen::prepareEcologyTest(bool showMap) {
	// Kynes met (ds:0a bit 5); place 12's troop is rallied, the place gets a
	// wind trap, 200 water and 16 bulbs; the troop takes bulbs (MODIFY
	// EQUIPMENT), specializes in ecology, and 120 days pass.
	_state.setB(World::kPaulEvents, (byte)(_state.b(World::kPaulEvents) | 0x21));
	_state.setB(GameState::kPhase, 0x58);
	const uint place = 12;
	byte *l = _state.vars + Location::kTableOffset + place * Location::kRecordSize;
	l[10] |= 0x20;
	l[27] = 200;
	l[26] = 16;
	Common::Array<uint> ids;
	_world.troopsAt(place, ids);
	uint id = 0;
	for (uint i = 0; i < ids.size() && !id; ++i)
		if (!_world.troop(ids[i]).harkonnen())
			id = ids[i];
	if (!id) {
		_log.line("Ecology test: no Fremen troop at place 12");
		return;
	}
	if (!_world.troop(id).hired())
		_world.rallyTroop(id);
	_world.takeEquipment(id, 6);
	_world.setTroopOccupation(id, Troop::kIrrigation);
	_log.line(Common::String::format("Ecology test: troop %u irrigates place %u (equipment %#x)", id, place,
			_world.troop(id).equipment));
	for (uint day = 0; day < 120; ++day)
		_world.advanceTime(World::kSlotsPerDay);
	_world.computeAreas();
	uint sprouting = 0, atreides = 0;
	for (uint i = 0; i < _world.map().size(); ++i) {
		const byte st = _world.map()[i] & 0x30;
		sprouting += st == 0x10;
		atreides += st == 0x20;
	}
	_log.line(Common::String::format("Ecology test: %u sprouting cells, %u Atreides cells, place cell %d vs map %d",
			sprouting, atreides, (int)_world.location(place).mapOffset,
			_world.mapCell(_world.location(place).longitude, _world.location(place).latitude)));
	const Location after = _world.location(place);
	_log.line(Common::String::format("Ecology test: water %u, disc radius %u, areas Atreides %u%% Harkonnen %u%%",
			after.water, _state.vars[Location::kTableOffset + place * Location::kRecordSize + 11],
			_state.w(0xa2), _state.w(0xa4)));
	if (showMap) {
		_world.setPosition(place, 1);
		openMap(MapScreen::kFlat, false);
		_map->centreOn(place);
		_map->setCaption(false);
		drawMapScreen();
		dumpScreen(_system, "ecology-map");
	}
}

// ---- The stillsuit chain (dump tour) ---------------------------------------------------

void GameScreen::dumpStillsuit() {
	// Phase 1 (after Gurney): with two troops rallied Leto asks for stillsuits
	// (phase 2); Carthag-Tuek's chief (place 12, names 0x205) sends Paul
	// east (phase 3); at Tuono-Tuek (place 15) troop 5's Fremen talk about
	// their stillsuits and their event 12 opens chapter 4.
	auto phase = [&](const char *what) {
		_log.line(Common::String::format("Stillsuit walk: %s, phase %#x, rallied %u", what,
				_state.b(GameState::kPhase), _state.b(GameState::kFremenTroops)));
	};
	phase("start");
	if (_state.b(GameState::kFremenTroops) < 2)
		_world.rallyTroop(3);
	phase("two troops");
	travelTo(0);
	showRoom(kPalaceFirstRoom);
	talkThrough(0);
	endConversation();
	phase("after Leto");
	travelTo(12);
	for (uint room = 1; room <= 6; ++room) {
		showRoom(room);
		Common::Array<byte> people;
		_world.peopleInRoom(people);
		bool chief = false;
		for (uint i = 0; i < people.size(); ++i)
			chief |= people[i] == World::kFremenChief;
		if (chief) {
			talkThrough(World::kFremenChief);
			dumpScreen(_system, "stillsuit-chief");
			endConversation();
			break;
		}
	}
	phase("after the chief");
	_log.line(Common::String::format("Stillsuit walk: Tuono-Tuek findable %d", _world.discoverable(15)));
	travelTo(15);
	for (uint room = 1; room <= 6; ++room) {
		showRoom(room);
		Common::Array<byte> people;
		_world.peopleInRoom(people);
		bool fremen = false;
		for (uint i = 0; i < people.size(); ++i)
			fremen |= people[i] == World::kFremen;
		if (fremen) {
			startConversation(World::kFremen);
			dumpScreen(_system, "stillsuit-fremen");
			for (uint g = 0; talking() && g < 20; ++g)
				advanceConversation();
			endConversation();
			break;
		}
	}
	applyStory();
	phase("after Tuono-Tuek");
	travelTo(0);
	showRoom(kPalaceFirstRoom);
	talkThrough(0);
	dumpScreen(_system, "stillsuit-leto");
	endConversation();
	phase("end");
}

// ---- The dump tour's story walk ---------------------------------------------------

void GameScreen::dumpStory() {
	if (!loadDialogue())
		return;
	auto runScene = [&](uint16 script, byte phase) {
		_state.setB(GameState::kPhase, phase);
		_world.setPosition(0, kPalaceFirstRoom);
		showRoom(kPalaceFirstRoom);
		startScene(script);
		for (uint guard = 0; _sceneActive && guard < 60; ++guard) {
			dumpScreen(_system, Common::String::format("scene-%x-%u", script, guard).c_str());
			if (_mode == kTalk)
				advanceConversation();
			else
				sceneStep();
		}
	};
	// The COMM room gathering (phase 0x0c), the map lesson, the scenes of
	// dialogue event 3 by phase, and Chani's.
	runScene(0x1321, 0x0c);
	runScene(0x12f8, 0x10);
	runScene(0x134f, 0x14);
	runScene(0x1370, 0x18);
	runScene(0x12db, 0x30);
	runScene(0x1313, 0x48);
	_state.setB(GameState::kPhase, 0x14);

	// Phase 0x14 in the desert: the first vision, the shipments begin.
	landInDesert();
	_world.firstVision();
	runPhaseTriggers();
	presentVision(true);
	dumpScreen(_system, "vision-first");
	for (uint guard = 0; _mode == kTalk && guard < 8; ++guard)
		advanceConversation();

	// The COMM room: the Emperor's demand.
	_world.setPosition(0, 8);
	showRoom(8);
	dumpScreen(_system, "comm-room");
	if (_world.sightingCount()) {
		openComm(false);
		byte variant = 0;
		const uint index = _world.sightingCount() - 1;
		const byte person = _world.viewSighting(index, variant);
		_state.setB(0x24, variant);
		presentLine(person, person, 4, 0, kTalkComm);
		dumpScreen(_system, "comm-message");
		for (uint guard = 0; _mode == kTalk && !_talkEnded && guard < 8; ++guard)
			advanceConversation();
		endConversation();
	}

	// Duncan in the COMM room: the bargaining, ACCEPT, the shipment.
	byte *duncan = _state.vars + World::kCharacterTable + 3 * World::kCharacterSize;
	duncan[0] = 8;
	duncan[1] = Location::kPalace;
	duncan[3] = 1;
	_state.setW(World::kSpiceStock, 200);
	showRoom(8);
	startConversation(3);
	for (uint guard = 0; _mode == kTalk && guard < 20; ++guard) {
		if (_talkBargain) {
			dumpScreen(_system, "duncan-bargain");
			_world.bargainChoice(1);
			_talkBargain = false;
			_talkEnded = false;
			_conversation->resume();
		}
		if (_talkEnded)
			break;
		advanceConversation();
	}
	endConversation(); // back to room 8: the agreed spice goes
	_log.line(Common::String::format("Dump: stock %u, agreed %u, fulfilment %u, days to shipment %u",
			_state.w(World::kSpiceStock), _state.w(World::kAgreed), _state.b(World::kFulfilment),
			_state.b(GameState::kDaysToShipment)));

	// A place's popup on the map (seg000:5ee4): Carthag-Tuek.
	openMap(MapScreen::kFlat, false);
	_map->setCaption(false);
	_map->centreOn(12);
	_map->select(12);
	drawMapScreen();
	dumpScreen(_system, "map-popup");
	_map->select(-1);

	// The ecology route: vegetation round place 12, then the final scene.
	prepareEcologyTest(true);
	_world.setPosition(0, kPalaceFirstRoom);
	showRoom(kPalaceFirstRoom);
	startScene(0x128f);
	for (uint guard = 0; _sceneActive && guard < 80; ++guard) {
		if (_mode == kTalk)
			advanceConversation();
		else
			sceneStep();
		if (guard % 8 == 0 || _finalPicture)
			dumpScreen(_system, Common::String::format("scene-128f-%u", guard).c_str());
	}
	dumpScreen(_system, "final-credits");
	_ending = false;
}

} // namespace Dune

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
#include "common/events.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "audio/mixer.h"

#include "engines/advancedDetector.h"
#include "engines/util.h"

#include "dune/cursor.h"
#include "dune/debug.h"
#include "dune/dune.h"
#include "dune/intro.h"
#include "dune/harness.h"
#include "dune/music.h"
#include "dune/palace.h"
#include "dune/resource.h"
#include "dune/scene.h"

namespace Dune {

DuneEngine::DuneEngine(OSystem *syst, const ADGameDescription *gameDescription) :
		Engine(syst), _gameDescription(gameDescription) {
}

DuneEngine::~DuneEngine() {
}

// Start a song and note the outcome in the log. The two songs used here are
// placeholders for the original's song selection, which is not decoded yet.
static void startSong(Music &music, Resource &resources, StartupLog &log, const char *name, bool enabled) {
	Common::Array<byte> song;
	if (enabled && resources.load(name, song) && music.play(song)) {
		debugSetAudioStream(Common::String::format("music:%s playing", name));
		log.line(Common::String::format("Music: %s started (%u bytes)", name, song.size()));
	} else {
		debugSetAudioStream(Common::String::format("music:%s not started", name));
		log.line(Common::String::format("Music: %s not started", name));
	}
}

Common::Error DuneEngine::run() {
	StartupLog log;
	const bool isCD = (_gameDescription->flags & ADGF_CD) != 0;
	log.line(Common::String::format("Dune startup: version=%s", isCD ? "CD" : "floppy"));

	// The CD release keeps its files in DUNE.DAT; the floppy release has them
	// loose. The choice is explicit so that a shared search path can never
	// make the floppy target read the CD archive.
	Resource resources(isCD);

	// Fail early, with a clear message, when the data is not where we look.
	const char *probeName = isCD ? "INTDS.HSQ" : "DUNES.HSQ";
	Common::Array<byte> probe;
	if (!resources.load(probeName, probe)) {
		warning("Dune: unable to load %s from the game data", probeName);
		log.line(Common::String::format("ERROR: unable to load %s", probeName));
		return Common::kNoGameDataFoundError;
	}
	probe.clear();

	preferDirectTouch(); // Must precede initGraphics(), where the backend reads it.
	initGraphics(320, 200);
	_system->fillScreen(0);
	_system->updateScreen();
	debugBegin(_system);
	debugSetRoom(-1);
	debugSetScene("startup");
	log.line("Graphics initialized: 320x200");

	// A silent run must explain itself in the log: on iOS the backend can fail
	// to start its audio queue (mixer not ready), and ScummVM's own volume and
	// mute settings apply on top of the phone's.
	log.line(Common::String::format("Audio: mixer %s, %u Hz, volumes music=%d sfx=%d speech=%d, mute=%s",
			_mixer->isReady() ? "ready" : "NOT READY (no sound this session)", _mixer->getOutputRate(),
			ConfMan.getInt("music_volume"), ConfMan.getInt("sfx_volume"), ConfMan.getInt("speech_volume"),
			ConfMan.getBool("mute") ? "yes" : "no"));
	if (!_mixer->isReady())
		showStatus("Dune: the audio device did not start - no sound");

	Music music;
	const bool musicEnabled = !isDumpRun() && !ConfMan.hasKey("dune_no_music");

	startSong(music, resources, log, "WORMINTR.HSQ", musicEnabled);
	bool keepRunning;
	if (isCD) {
		keepRunning = playCdIntro(_system, resources, log);
	} else {
		// The floppy sequence owns its own music changes (WORMINTR for the
		// intro, WORMSUIT for credits and prologue).
		FloppyIntro intro(_system, resources, log, musicEnabled ? &music : nullptr);
		keepRunning = intro.play();
	}
	if (!keepRunning) {
		debugEnd();
		return Common::kNoError;
	}

	startSong(music, resources, log, "ARRAKIS.HSQ", musicEnabled);
	showGameCursor(); // The intro runs without a pointer, as in the original.
	GameScreen screen(_system, resources, log);
	// As in the original, the game lands in the throne room; there is no
	// start menu. Saving, loading and options belong to the book.
	screen.setMusic(&music, musicEnabled);
	screen.startNewGame();
	log.line("Startup complete: throne room displayed");
	if (ConfMan.hasKey("dune_test_flight")) {
		// Developer key: fly to place <n> in real time and stop (with
		// dune_dump_every the flight view is sampled into the dump folder).
		screen.travelTo((uint)ConfMan.getInt("dune_test_flight"));
		debugEnd();
		return Common::kNoError;
	}
	if (ConfMan.hasKey("dune_speedrun")) {
		// Developer key: the speedrun check (notes/speedrun/route.md). A
		// watched run (dune_speedrun_watch) leaves the game playable after it.
		screen.speedrun(ConfMan.get("dune_speedrun"));
		if (!ConfMan.hasKey("dune_speedrun_watch") || screen.quitRequested() || isRecording()) {
			debugEnd();
			return Common::kNoError;
		}
		ConfMan.removeKey("dune_speedrun", Common::ConfigManager::kApplicationDomain);
	}
	if (ConfMan.hasKey("dune_story_setup"))
		screen.storySetup(ConfMan.get("dune_story_setup")); // developer / regression key
	if (ConfMan.hasKey("dune_test_cockpit"))
		screen.testCockpit(ConfMan.getInt("dune_test_cockpit")); // developer key: the orni cockpit, for a scripted real-time test

	if (isDumpRun() && !ConfMan.hasKey("dune_test_cockpit")) {
		// Screenshot every palace room, then leave.
		for (uint room = 1; room <= kPalaceRoomCount; ++room)
			if (!(palaceRoom(room).code & 0x80))
				screen.showPalaceRoom(room);
		// Then Duke Leto's opening conversation, page by page (talk-<n>).
		screen.showPalaceRoom(kPalaceFirstRoom);
		screen.startConversation(0);
		while (screen.talking())
			screen.advanceConversation();
		screen.stopTalking();
		// And the book: cover, then the first page of all topics.
		screen.openBook();
		screen.bookAction(GameScreen::kBookNext);
		// The map and the globe, then a sietch, a village and a fortress
		// (place-<n>-room-<m>): Carthag-Oxtyn, Arrakeen-Cielago, Arrakeen-Tuono.
		screen.showRoom(9); // the bedroom: LOOK AT MIRROR
		screen.lookInMirror();
		screen.showRoom(kPalaceFirstRoom);
		screen.openMap(MapScreen::kFlat, false);
		screen.openMap(MapScreen::kGlobe, false);
		screen.travelTo(12);
		screen.showRoom(2);
		screen.dumpGameplay();
		screen.travelTo(9);
		screen.travelTo(2);
		screen.showRoom(2);
		screen.showRoom(3);
		// The Harkonnen palace, then the game menu: save to the third log,
		// the load and options lists, the exit question, and the log back.
		screen.travelTo(1);
		screen.openMenu(GameScreen::kMenuSave);
		screen.saveSlot(2);
		screen.openMenu(GameScreen::kMenuLoad);
		screen.openMenu(GameScreen::kMenuOptions);
		screen.openMenu(GameScreen::kMenuQuit);
		screen.loadSlot(2);
		// Last, so it cannot disturb the pictures above: the story systems.
		screen.dumpStillsuit();
		screen.dumpStory();
		screen.dumpDesertWalk();
		screen.dumpCockpit();
		debugEnd();
		return Common::kNoError;
	}

	Common::Event event;
	while (!shouldQuit()) {
		const Common::Point cursor = _eventMan->getMousePos();
		debugSetCursor(cursor.x, cursor.y);
		while (pollDuneEvent(_system, event)) {
			if (event.type == Common::EVENT_MOUSEMOVE || event.type == Common::EVENT_LBUTTONDOWN ||
					event.type == Common::EVENT_LBUTTONUP || event.type == Common::EVENT_RBUTTONDOWN ||
					event.type == Common::EVENT_RBUTTONUP)
				debugSetCursor(event.mouse.x, event.mouse.y);
			if (screen.handleEvent(event) || screen.quitRequested()) {
				debugEnd();
				return Common::kNoError;
			}
		}
		screen.update();
		// The backend repaints the mouse pointer only inside updateScreen(),
		// so it has to be called every frame even when nothing of ours has
		// changed; otherwise the pointer freezes between room changes.
		_system->updateScreen();
		_system->delayMillis(10);
	}

	debugEnd();
	return Common::kNoError;
}

bool DuneEngine::hasFeature(EngineFeature feature) const {
	return feature == kSupportsReturnToLauncher;
}

} // namespace Dune

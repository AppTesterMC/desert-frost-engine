/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
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
	if (enabled && resources.load(name, song) && music.play(song))
		log.line(Common::String::format("Music: %s started (%u bytes)", name, song.size()));
	else
		log.line(Common::String::format("Music: %s not started", name));
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
		FloppyIntro intro(_system, resources, log);
		keepRunning = intro.play();
	}
	if (!keepRunning)
		return Common::kNoError;

	startSong(music, resources, log, "ARRAKIS.HSQ", musicEnabled);
	showGameCursor(); // The intro runs without a pointer, as in the original.
	GameScreen screen(_system, resources, log);
	screen.showMenu();
	log.line("Startup complete: main menu displayed");

	if (isDumpRun()) {
		// Screenshot every palace room, then leave.
		for (uint room = 1; room <= kPalaceRoomCount; ++room)
			if (!(palaceRoom(room).code & 0x80))
				screen.showPalaceRoom(room);
		return Common::kNoError;
	}

	Common::Event event;
	while (!shouldQuit()) {
		while (_eventMan->pollEvent(event)) {
			if (screen.handleEvent(event))
				return Common::kNoError;
		}
		// The backend repaints the mouse pointer only inside updateScreen(),
		// so it has to be called every frame even when nothing of ours has
		// changed; otherwise the pointer freezes between room changes.
		_system->updateScreen();
		_system->delayMillis(10);
	}

	return Common::kNoError;
}

bool DuneEngine::hasFeature(EngineFeature feature) const {
	return feature == kSupportsReturnToLauncher;
}

} // namespace Dune

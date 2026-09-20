/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#include "common/array.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/str.h"
#include "common/system.h"

#include "graphics/paletteman.h"

#include "dune/debug.h"
#include "dune/hnm.h"
#include "dune/intro.h"
#include "dune/resource.h"
#include "dune/sky.h"
#include "dune/sprite.h"

namespace Dune {

bool playCdIntro(OSystem *system, Resource &resources, StartupLog &log) {
	static const char *const videos[] = {
		"VIRGIN.HNM", "CRYO.HNM", "CRYO2.HNM", "PRESENT.HNM", "TITLE.HNM", "IRULAN.HNM"
	};

	HnmPlayer player(system);
	Common::Array<byte> video;
	const uint first = ConfMan.hasKey("dune_intro_start") ? (uint)ConfMan.getInt("dune_intro_start") : 0;
	for (uint i = first; i < ARRAYSIZE(videos); ++i) {
		if (!resources.load(videos[i], video)) {
			log.line(Common::String::format("Intro: %s missing", videos[i]));
			continue;
		}
		// Dump runs stop each video at frame 40 and screenshot it.
		const HnmPlayer::Result played = player.play(video, videos[i], isDumpRun() ? 40 : -1);
		log.line(Common::String::format("Intro: %s (%u bytes) result %d", videos[i], video.size(), (int)played));
		if (played == HnmPlayer::kQuit)
			return false;
	}
	return true;
}

FloppyIntro::FloppyIntro(OSystem *system, Resource &resources, StartupLog &log) :
		_system(system), _resources(resources), _log(log), _aborted(false), _quit(false) {
	_surface.create(320, 200, Graphics::PixelFormat::createFormatCLUT8());
	memset(_palette, 0, sizeof(_palette));
}

FloppyIntro::~FloppyIntro() {
	_surface.free();
}

// Sprite numbers, positions, durations and transitions below follow the intro
// script of codingstyle's swift-dune (Game/Scenes/Intro.swift, Presents.swift,
// Stars.swift, DuneTitle.swift), which agrees with the recording of the
// original. Its coordinates are relative to the 320x152 game view, which the
// intro shows 24 rows down the screen (kViewTop).
bool FloppyIntro::play() {
	Common::Array<byte> video;
	if (_resources.load("LOGO.HNM", video)) {
		HnmPlayer player(_system);
		const HnmPlayer::Result result = player.play(video, "LOGO.HNM", isDumpRun() ? 12 : -1);
		_log.line(Common::String::format("Intro: LOGO.HNM result %d", (int)result));
		if (result == HnmPlayer::kQuit)
			return false;
		_aborted = result == HnmPlayer::kSkipped;
	}

	static const Placement virgin[] = { { 6, 62, 58 }, { 7, 114, 90 } };
	static const Placement cryo[] = { { 8, 76, 43 }, { 9, 132, 71 }, { 10, 14, 96 } };

	card("INTDS.HSQ", virgin, ARRAYSIZE(virgin), 3800 - 2 * 900, "intro-virgin");
	card("INTDS.HSQ", cryo, ARRAYSIZE(cryo), 5510 - 2 * 900, "intro-cryo");
	starfield();
	title();
	wormCall();
	characterScene(false, "PAUL.HSQ", "intro-paul", 8000);
	characterScene(true, "CHAN.HSQ", "intro-chani", 6500);
	_log.line(_aborted ? "Intro: floppy sequence skipped" : "Intro: floppy sequence finished");
	return !_quit;
}

bool FloppyIntro::loadSheet(const char *name, Common::Array<byte> &data) {
	if (_aborted || _quit || !_resources.load(name, data))
		return false;
	// Read the sheet's palette without showing it yet: fades own the screen palette.
	memset(_palette, 0, sizeof(_palette));
	Sprite(_system, data).setPalette();
	_system->getPaletteManager()->grabPalette(_palette, 0, 256);
	_palette[0] = _palette[1] = _palette[2] = 0;
	applyPalette(0);
	return true;
}

void FloppyIntro::applyPalette(uint level) { // 0..256
	byte scaled[256 * 3];
	for (uint i = 0; i < sizeof(scaled); ++i)
		scaled[i] = (_palette[i] * level) >> 8;
	_system->getPaletteManager()->setPalette(scaled, 0, 256);
}

void FloppyIntro::present() {
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

// Waits while keeping the app responsive; a tap or key skips the intro.
bool FloppyIntro::wait(uint32 millis) {
	const uint32 end = _system->getMillis() + (isDumpRun() ? 0 : millis);
	Common::Event event;
	do {
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				_quit = true;
			else if ((event.type == Common::EVENT_LBUTTONDOWN || event.type == Common::EVENT_KEYDOWN) && !isDumpRun())
				_aborted = true; // Dump runs ignore input: their window may steal a keystroke.
		}
		if (_aborted || _quit)
			return false;
		if (_system->getMillis() >= end)
			return true;
		_system->delayMillis(10);
	} while (true);
}

void FloppyIntro::fade(bool in) {
	for (uint step = 0; step <= 16 && !isDumpRun(); ++step) {
		applyPalette(in ? step * 16 : 256 - step * 16);
		_system->updateScreen();
		if (!wait(25))
			return;
	}
	applyPalette(in ? 256 : 0);
	_system->updateScreen();
}

void FloppyIntro::card(const char *sheetName, const Placement *placements, uint count, uint32 hold, const char *dumpName) {
	Common::Array<byte> data;
	if (!loadSheet(sheetName, data))
		return;
	Sprite sheet(_system, data);
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	for (uint i = 0; i < count; ++i)
		sheet.drawFrame(placements[i].sprite, _surface.surfacePtr(), placements[i].x, placements[i].y + kViewTop);
	present();
	fade(true);
	dumpScreen(_system, dumpName);
	if (wait(hold))
		fade(false);
}

// STARS.HSQ: sprites 0-2 are a star panorama (placed 302 pixels apart), 3-35
// Arrakis at growing sizes, 36 the sun, 37-44 a red planet passing by. The
// view pans 604 pixels while Arrakis grows towards the centre, then the
// picture shrinks into the planet ("zoom" transition; we fade instead).
void FloppyIntro::starfield() {
	Common::Array<byte> data;
	if (!loadSheet("STARS.HSQ", data))
		return;
	Sprite sheet(_system, data);

	const uint32 duration = 9840, fadeIn = 7340;
	const uint32 start = _system->getMillis();
	while (true) {
		const uint32 elapsed = isDumpRun() ? duration * 3 / 4 : MIN<uint32>(_system->getMillis() - start, duration);
		const int scroll = (int)((uint64)604 * elapsed / duration);

		_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
		Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, kViewTop, 320, kViewTop + 152));
		sheet.drawFrame(0, &view, -scroll, 0);
		sheet.drawFrame(1, &view, 302 - scroll, 0);
		sheet.drawFrame(2, &view, 604 - scroll, 0);
		sheet.drawFrame(36, &view, 45 - scroll * 2, 60);

		uint32 offset;
		uint16 width, height;
		bool compressed;
		int8 paletteOffset;

		// Arrakis starts at x = 124 and drifts to the centre as it grows.
		const uint arrakis = 3 + MIN<uint>(32 * elapsed / duration, 32);
		if (sheet.getFrameInfo(3, offset, width, height, compressed, paletteOffset)) {
			const int firstDelta = (320 - width) / 2 - 124;
			if (sheet.getFrameInfo(arrakis, offset, width, height, compressed, paletteOffset)) {
				const int x = (320 - width) / 2 - (int)((int64)firstDelta * (duration - elapsed) / duration);
				sheet.drawFrame(arrakis, &view, x, (152 - height) / 2);
			}
		}

		const uint redPlanet = 37 + MIN<uint>(7 * elapsed / duration, 7);
		if (sheet.getFrameInfo(redPlanet, offset, width, height, compressed, paletteOffset)) {
			const int x = elapsed * 2 < duration ? 235 + (int)(160 * elapsed / duration)
					: 315 - (int)(945 * (elapsed - duration / 2) / duration);
			sheet.drawFrame(redPlanet, &view, x, 76 + (76 - height) / 2);
		}

		applyPalette(isDumpRun() ? 256 : MIN<uint32>(256, 256 * elapsed / fadeIn));
		present();
		if (elapsed >= duration || isDumpRun())
			break;
		if (!wait(25))
			return;
	}
	dumpScreen(_system, "intro-stars");
	fade(false);
}

// The dunes under a midday sky, which then scroll down out of view while the
// title backdrop follows from above; the red DUNE letters (colours 224-239)
// fade in on their own.
void FloppyIntro::title() {
	Common::Array<byte> data;
	if (_aborted || _quit || !_resources.load("INTDS.HSQ", data))
		return;
	Sprite sheet(_system, data);

	// The sky brings its own palette for 128-222; the sheet's must come last
	// wherever they overlap, as in the original's drawing order.
	Graphics::ManagedSurface dunes;
	dunes.create(320, 152, Graphics::PixelFormat::createFormatCLUT8());
	drawSky(_system, _resources, *dunes.surfacePtr(), kSkyNarrow, 320, kSkyDay);
	sheet.setPalette();
	sheet.drawFrame(1, dunes.surfacePtr(), 0, 44);
	_system->getPaletteManager()->grabPalette(_palette, 0, 256);
	_palette[0] = _palette[1] = _palette[2] = 0;

	const uint32 scrollStart = 500, scrollEnd = 4500, lettersStart = 5500, lettersFade = 2000, duration = 16500;
	const uint32 start = _system->getMillis();
	bool fadedIn = false, dumped = false;
	while (true) {
		const uint32 elapsed = isDumpRun() ? (dumped ? duration : 300) : _system->getMillis() - start;
		const int scroll = elapsed <= scrollStart ? 0 :
				(int)(152 * MIN<uint32>(elapsed - scrollStart, scrollEnd - scrollStart) / (scrollEnd - scrollStart));

		_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
		Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, kViewTop, 320, kViewTop + 152));
		for (int y = scroll; y < 152; ++y)
			memcpy(view.getBasePtr(0, y), dunes.getBasePtr(0, y - scroll), 320);
		if (scroll > 0) {
			const int top = scroll - 152;
			sheet.drawFrame(2, &view, 0, top);
			sheet.drawFrame(3, &view, 0, top + 95);
			sheet.drawFrame(4, &view, 0, top + 123);
		}
		if (elapsed >= lettersStart)
			sheet.drawFrame(5, &view, 0, 48);

		if (!fadedIn && !isDumpRun()) {
			// "Pixelate" in the original; a fade stands in for it.
			present();
			fade(true);
			fadedIn = true;
			continue;
		}

		// Everything at full brightness except the letters' colours.
		byte palette[256 * 3];
		memcpy(palette, _palette, sizeof(palette));
		const uint level = elapsed < lettersStart ? 0 : MIN<uint32>(256, 256 * (elapsed - lettersStart) / lettersFade);
		for (uint i = 224 * 3; i < 240 * 3; ++i)
			palette[i] = (palette[i] * level) >> 8;
		_system->getPaletteManager()->setPalette(palette, 0, 256);
		present();

		if (isDumpRun()) {
			dumpScreen(_system, dumped ? "intro-title" : "intro-dunes");
			if (dumped)
				break;
			dumped = true;
			continue;
		}
		if (elapsed >= duration)
			break;
		if (!wait(25)) {
			dunes.free();
			return;
		}
	}
	dunes.free();
	fade(false);
}

// The first scene after the title calls the worm from a blue Arrakis sky.
// SHAI frame 44 is the fixed desert strip; frames 0-2 are the first three
// recovered worm poses. The original has a longer animation table, but these
// data-backed poses establish the scene without inventing sprite geometry.
void FloppyIntro::wormCall() {
	Common::Array<byte> skyData, wormData;
	if (_aborted || _quit || !_resources.load("SKY.HSQ", skyData) || !_resources.load("SHAI.HSQ", wormData))
		return;

	Sprite sky(_system, skyData), worm(_system, wormData);
	if (!sky.setPaletteRecord(9) || !worm.setPalette())
		return;
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, kViewTop, 320, kViewTop + 152));
	drawSky(_system, _resources, view, kSkyNarrow, 320, kSkyDay);
	worm.drawFrame(44, &view, 0, 74);
	present();
	fade(true);

	const uint32 duration = 9410;
	const uint32 start = _system->getMillis();
	while (!_aborted && !_quit) {
		const uint32 elapsed = isDumpRun() ? duration / 2 : MIN<uint32>(_system->getMillis() - start, duration);
		const uint frame = MIN<uint32>(2, elapsed / 1800);
		_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
		Graphics::Surface current = _surface.surfacePtr()->getSubArea(Common::Rect(0, kViewTop, 320, kViewTop + 152));
		drawSky(_system, _resources, current, kSkyNarrow, 320, kSkyDay);
		worm.drawFrame(44, &current, 0, 74);
		uint32 offset;
		uint16 width, height;
		bool compressed;
		int8 paletteOffset;
		if (worm.getFrameInfo(frame, offset, width, height, compressed, paletteOffset))
			worm.drawFrame(frame, &current, (320 - width) / 2, 54 + (70 - height) / 2);
		present();
		if (isDumpRun()) {
			dumpScreen(_system, "intro-worm");
			break;
		}
		if (elapsed >= duration)
			break;
		if (!wait(25))
			return;
	}
	fade(false);
}

// Port the next two script entries as data-backed cards. BACK frame 0 is the
// red Paul backdrop; SUNRS frames 2-6 are the sunrise desert. Character frame
// 0 is the first standing pose for Paul/Chani; later animation tables can
// replace this still once their original scheduler is decoded.
void FloppyIntro::characterScene(bool sunrise, const char *characterName, const char *dumpName, uint32 duration) {
	Common::Array<byte> backgroundData, characterData;
	const char *backgroundName = sunrise ? "SUNRS.HSQ" : "BACK.HSQ";
	if (_aborted || _quit || !_resources.load(backgroundName, backgroundData) || !_resources.load(characterName, characterData))
		return;

	Sprite background(_system, backgroundData), character(_system, characterData);
	if (!background.setPalette() || !character.setPalette())
		return;

	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
	Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, kViewTop, 320, kViewTop + 152));
	if (sunrise) {
		background.drawFrame(2, &view, 0, 0);
		background.drawFrame(3, &view, 0, 25);
		background.drawFrame(4, &view, 0, 50);
		background.drawFrame(5, &view, 0, 74);
		background.drawFrame(6, &view, 134, 92);
		background.drawFrame(0, &view, 0, 102);
	} else {
		background.drawFrame(0, &view, 0, 0);
		background.drawFrame(1, &view, 52, 25);
		background.drawFrame(2, &view, 108, 51);
	}

	uint32 offset;
	uint16 width, height;
	bool compressed;
	int8 paletteOffset;
	if (character.getFrameInfo(0, offset, width, height, compressed, paletteOffset))
		character.drawFrame(0, &view, (320 - width) / 2, 44);
	present();
	fade(true);
	dumpScreen(_system, dumpName);
	if (wait(duration))
		fade(false);
}

} // namespace Dune

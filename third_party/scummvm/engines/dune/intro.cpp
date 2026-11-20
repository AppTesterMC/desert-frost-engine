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

#include "common/array.h"
#include "common/config-manager.h"
#include "common/endian.h"
#include "common/events.h"
#include "common/file.h"
#include "common/str.h"
#include "common/tokenizer.h"
#include "common/system.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/hnm.h"
#include "dune/intro.h"
#include "dune/music.h"
#include "dune/resource.h"
#include "dune/scene.h"
#include "dune/sky.h"
#include "dune/sound.h"
#include "dune/sprite.h"

namespace Dune {

namespace {

// Regression captures take one deterministic representative frame from each
// authored scene while normal device runs retain their real timing.
bool isFastIntroCapture() {
	return (isDumpRun() && !dumpEveryMillis()) || isDuneFastHarness();
}

} // namespace

namespace {

// play_IRULx_HSQ (seg000:cf1b): the subtitles of Irulan's narration are the
// frames of IRULn.HSQ (n the language, 1 English), each shown from the first
// to the second video frame of its pair in the table at ds:35a8 and drawn at
// row 190 (seg000:cf4b); the strip is cleared between them.
struct IrulanSubtitles {
	Sprite *sheet = nullptr;
	Common::Array<uint16> frames; ///< start, end, start, end...
};

void drawIrulanSubtitle(void *context, uint frame, byte *screen) {
	IrulanSubtitles *s = (IrulanSubtitles *)context;
	if (!s->sheet)
		return;
	// The video only repaints its own window: the subtitle strip (rows 190
	// on) is cleared every frame, as seg000:cf61 clears it between phrases.
	memset(screen + 190 * 320, 0, 10 * 320);
	for (uint k = 0; k + 1 < s->frames.size(); k += 2) {
		if (frame < s->frames[k] || frame >= s->frames[k + 1])
			continue;
		// Drawn once off screen to find the ink's extent, then centred.
		byte strip[320 * 10];
		memset(strip, 0, sizeof(strip));
		Graphics::Surface off;
		off.init(320, 10, 320, strip, Graphics::PixelFormat::createFormatCLUT8());
		s->sheet->drawFrame((uint16)(k / 2), &off, 0, 0);
		int left = 320, right = -1;
		for (int y = 0; y < 10; ++y)
			for (int x = 0; x < 320; ++x)
				if (strip[y * 320 + x]) {
					left = MIN(left, x);
					right = MAX(right, x);
				}
		if (right < left)
			return;
		const int shift = (320 - (right - left + 1)) / 2 - left;
		for (int y = 0; y < 10; ++y)
			for (int x = left; x <= right; ++x)
				if (strip[y * 320 + x])
					screen[(190 + y) * 320 + x + shift] = strip[y * 320 + x];
		return;
	}
}

// The subtitle frame table: 0x77, 0x89, 0x8a, 0xad... in the CD
// executable's data (ds:35a8); found by its first pairs so other CD builds
// with the same data work too.
bool findIrulanTable(Resource &resources, Common::Array<uint16> &frames) {
	(void)resources;
	Common::Array<byte> exe;
	Common::File file;
	if (!file.open(Common::Path("DNCDPRG.EXE")))
		return false;
	exe.resize(file.size());
	if (file.read(exe.data(), exe.size()) != exe.size())
		return false;
	static const byte kHead[] = { 0x77, 0x00, 0x89, 0x00, 0x8a, 0x00, 0xad, 0x00, 0xba, 0x00 };
	for (uint p = 0; p + sizeof(kHead) <= exe.size(); ++p) {
		if (memcmp(exe.data() + p, kHead, sizeof(kHead)))
			continue;
		uint16 last = 0;
		for (uint q = p; q + 2 <= exe.size() && frames.size() < 80; q += 2) {
			const uint16 v = READ_LE_UINT16(exe.data() + q);
			if (v < last || v > 0x1000)
				break;
			frames.push_back(v);
			last = v;
		}
		return frames.size() >= 2;
	}
	return false;
}

} // namespace

bool playCdIntro(OSystem *system, Resource &resources, StartupLog &log) {
	// intro_script (seg000:0337), the CD's order: VIRGIN; CRYO; CRYO2, held
	// on the logo until the music reaches its cue (wait 0x6f, then 0xa8);
	// PRESENT; Irulan's narration (IRULAN.HNM with the IRULn.HSQ subtitles,
	// seg000:cefc); TITLE; the desert flyover to the palace (MTG1.HNM,
	// seg000:06ce). The rest of the script, the story scenes and MTG2/MTG3,
	// is not played yet (see FINDINGS.md).
	struct Step {
		const char *video;
		uint32 holdMillis; ///< the last picture stays this long (the music cues)
	};
	static const Step steps[] = {
		{ "VIRGIN.HNM", 0 }, { "CRYO.HNM", 0 }, { "CRYO2.HNM", 3500 }, { "PRESENT.HNM", 0 },
		{ "IRULAN.HNM", 0 }, { "TITLE.HNM", 1500 }
	};
	const bool fast = isDumpRun() || isDuneFastHarness();
	HnmPlayer player(system);
	Common::Array<byte> video;
	const uint first = ConfMan.hasKey("dune_intro_start") ? (uint)ConfMan.getInt("dune_intro_start") : 0;
	for (uint i = first; i < ARRAYSIZE(steps); ++i) {
		const char *name = steps[i].video;
		// The regression harness keeps its five checkpoints (VIRGIN .. TITLE).
		if (!strcmp(name, "IRULAN.HNM") && isDuneHarnessRun())
			continue;
		if (!resources.load(name, video)) {
			log.line(Common::String::format("Intro: %s missing", name));
			continue;
		}
		IrulanSubtitles subtitles;
		Common::Array<byte> sheetData;
		if (!strcmp(name, "IRULAN.HNM") && resources.load("IRUL1.HSQ", sheetData)) {
			subtitles.sheet = new Sprite(system, sheetData);
			if (!findIrulanTable(resources, subtitles.frames))
				log.line("Intro: the Irulan subtitle table was not found in DNCDPRG.EXE");
			player.setFrameHook(&drawIrulanSubtitle, &subtitles);
		}
		// Dump runs stop each video at frame 40 and screenshot it.
		player.setTop(!strncmp(name, "MTG", 3) ? 24 : 0);
		const int dumpAt = ConfMan.hasKey("dune_hnm_dump_frame") ? ConfMan.getInt("dune_hnm_dump_frame") : 40;
		const HnmPlayer::Result played = player.play(video, name, fast ? dumpAt : -1);
		player.setFrameHook(nullptr, nullptr);
		delete subtitles.sheet;
		log.line(Common::String::format("Intro: %s (%u bytes) result %d", name, video.size(), (int)played));
		if (played == HnmPlayer::kQuit)
			return false;
		// ESC ends the whole intro, the story after TITLE too: the original
		// goes straight to the throne room (DNCDPRG on Spice86, captures/
		// cd-flight: ESC at 8.0 s, the throne room at 8.2 s). A click or
		// Return skips only the current video.
		if (played == HnmPlayer::kSkipped && player.skippedWithEscape()) {
			log.line("Intro: ESC ends the intro");
			return true;
		}
		if (played == HnmPlayer::kSkipped)
			continue;
		if (!fast && steps[i].holdMillis) {
			const uint32 until = system->getMillis() + steps[i].holdMillis;
			bool skip = false;
			while (!skip && system->getMillis() < until) {
				Common::Event event;
				while (pollDuneEvent(system, event)) {
					if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
						return false;
					if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
						log.line("Intro: ESC ends the intro");
						return true;
					}
					if (event.type == Common::EVENT_LBUTTONDOWN || event.type == Common::EVENT_KEYDOWN)
						skip = true;
				}
				system->delayMillis(10);
			}
		}
	}
	// The story after TITLE (the regression harness keeps its short intro).
	if (!isDuneHarnessRun() && !ConfMan.hasKey("dune_skip_cd_story")) {
		FloppyIntro story(system, resources, log, nullptr);
		if (!story.playCdStory())
			return false;
	}
	return true;
}

FloppyIntro::FloppyIntro(OSystem *system, Resource &resources, StartupLog &log, Music *music) :
		_system(system), _resources(resources), _log(log), _music(music), _viewTop(kViewTop), _prologueBase(0),
		_aborted(false), _quit(false), _skipPrologue(false), _manualPrologue(false), _sceneNumber(0), _introStart(0), _lastTimedDump(0) {
	_surface.create(320, 200, Graphics::PixelFormat::createFormatCLUT8());
	memset(_palette, 0, sizeof(_palette));
	loadSubtitles();
}

void FloppyIntro::startSong(const char *name) {
	Common::Array<byte> song;
	if (!_music || isFastIntroCapture() || (ConfMan.hasKey("dune_no_music") && ConfMan.getBool("dune_no_music")))
		return;
	if (_resources.load(name, song) && _music->play(song))
		_log.line(Common::String::format("Music: %s started (%u bytes)", name, song.size()));
	else
		_log.line(Common::String::format("Music: %s not started", name));
}

FloppyIntro::~FloppyIntro() {
	_surface.free();
}

void FloppyIntro::loadSubtitles() {
	// Resolve the release's record index in its English bank, then use that
	// index in the active language. It is 267 on floppy and 279 on CD.
	Common::Array<byte> data, english;
	if (!_resources.load("COMMAND1.HSQ", data))
		_resources.load("COMMAND2.HSQ", data);
	if (_resources.language() != 1)
		_resources.loadUntranslated("COMMAND1.HSQ", english);
	if (data.size() >= 2) {
		const uint count = READ_LE_UINT16(data.data()) / 2;
		const uint keysCount = english.size() >= 2 ? READ_LE_UINT16(english.data()) / 2 : 0;
		if (count <= 1024 && count * 2 <= data.size()) {
			_subtitles.resize(count);
			for (uint i = 0; i < count; ++i) {
				const uint32 start = READ_LE_UINT16(data.data() + i * 2);
				if (start >= data.size())
					continue;
				uint32 end = start;
				while (end < data.size() && data[end] != 0xff)
					++end;
				_subtitles[i] = Common::String((const char *)data.data() + start, end - start);
				if (_prologueBase)
					continue;
				const Common::Array<byte> &keys = i < keysCount && keysCount * 2 <= english.size() ? english : data;
				const uint32 at = READ_LE_UINT16(keys.data() + i * 2);
				static const char first[] = "In these times";
				if ((at + sizeof(first) - 1 <= keys.size() &&
						!memcmp(keys.data() + at, first, sizeof(first) - 1)) ||
						_subtitles[i].hasPrefix("En ces temps"))
					_prologueBase = i;
			}
		}
	}
	if (!_resources.load("DNCHAR.BIN", _font))
		_resources.load("DUNECHAR.HSQ", _font);
	_log.line(Common::String::format("Intro subtitles: %u records, prologue at %u, font=%u bytes",
			_subtitles.size(), _prologueBase, _font.size()));
}

uint16 FloppyIntro::prologueSentence(uint step) const {
	return _prologueBase ? _prologueBase + step : 0;
}

int FloppyIntro::textWidth(const Common::String &text) const {
	int width = 0;
	if (_font.size() >= 256)
		for (uint i = 0; i < text.size(); ++i)
			width += _font[(byte)text[i]];
	return width;
}

// DOS uses palette entry 6 for narration, separate from the STARS picture.
static const byte kNarrationInk = 6;
static const int kNarrationSpace = 6;

void FloppyIntro::drawSubtitleLine(const Common::Array<Common::String> &words, uint first, uint last, int y,
		int space, int extra) {
	int x = 0;
	for (uint i = first; i < last; ++i) {
		const Common::String &word = words[i];
		for (uint c = 0; c < word.size(); ++c) {
			const byte ch = (byte)word[c];
			const uint32 glyph = 256 + (uint32)ch * 9;
			if (glyph + 9 <= _font.size()) {
				for (int row = 0; row < 9 && y + row < _surface.h; ++row) {
					const byte bits = _font[glyph + row];
					for (int column = 0; column < 8 && x + column < _surface.w; ++column)
						if (bits & (0x80 >> column))
							*(byte *)_surface.getBasePtr(x + column, y + row) = kNarrationInk;
				}
			}
			x += _font[ch];
		}
		x += space;
		if (extra > 0) {
			++x;
			--extra;
		}
	}
}

void FloppyIntro::drawSubtitle(uint16 sentenceNumber) {
	if (!sentenceNumber || sentenceNumber >= _subtitles.size() || _font.size() < 256)
		return;
	const Common::Rect panel(0, 152, 320, 200);
	_surface.fillRect(panel, 0);
	static const byte brown[] = { 170, 85, 0 }; // VGA DAC 42, 21, 0
	_system->getPaletteManager()->setPalette(brown, kNarrationInk, 1);

	struct Line {
		uint first, last;
		int space, extra;
	};
	Common::Array<Line> lines;
	Common::Array<Common::String> words;
	bool justify = true;
	Common::StringTokenizer paragraphs(_subtitles[sentenceNumber], "\r\n");
	while (!paragraphs.empty()) {
		Common::StringTokenizer tokens(paragraphs.nextToken(), " \t");
		uint first = words.size();
		int width = 0;
		while (!tokens.empty()) {
			const Common::String word = tokens.nextToken();
			const int wordWidth = textWidth(word);
			if (first < words.size() && width + kNarrationSpace + wordWidth > panel.width()) {
				const int gaps = words.size() - first - 1;
				const int slack = panel.width() - width;
				const int space = gaps > 0 ? kNarrationSpace + slack / gaps : kNarrationSpace;
				lines.push_back({ first, words.size(), space, gaps > 0 ? slack % gaps : 0 });
				// DOS falls back to normal spacing for the caption if a gap is too wide.
				if (space >= 30)
					justify = false;
				first = words.size();
				width = 0;
			}
			if (first < words.size())
				width += kNarrationSpace;
			words.push_back(word);
			width += wordWidth;
		}
		// A hard break in COMMAND ends the paragraph without justification.
		if (first < words.size())
			lines.push_back({ first, words.size(), kNarrationSpace, 0 });
	}
	// The text window begins below the picture, at row 153. The nine-row
	// font advances ten pixels per line, and the group is centred vertically.
	const int lineHeight = 10;
	int y = panel.top + 1 + MAX(0, (panel.height() - 1 - (int)lines.size() * lineHeight) / 2);
	for (const Line &line : lines) {
		drawSubtitleLine(words, line.first, line.last, y, justify ? line.space : kNarrationSpace,
				justify ? line.extra : 0);
		y += lineHeight;
	}
}

// Sprite numbers, positions, durations and transitions below follow the intro
// script of codingstyle's swift-dune (Game/Scenes/Intro.swift, Presents.swift,
// Stars.swift, DuneTitle.swift), which agrees with the recording of the
// original. Its coordinates are relative to the 320x152 game view, which the
// intro shows 24 rows down the screen (kViewTop).
bool FloppyIntro::play() {
	debugSetRoom(-1);
	debugSetScene("floppy/intro");
	// Development aid: "dune_floppy_start=<n>" skips the first n scenes (the
	// log numbers them), so a late scene can be checked without replaying
	// four minutes of intro.
	int startScene = ConfMan.hasKey("dune_floppy_start") ? ConfMan.getInt("dune_floppy_start") : 0;
	_introStart = _lastTimedDump = _system->getMillis();
	startSong("WORMINTR.HSQ");
	Common::Array<byte> video;
	if (_resources.load("LOGO.HNM", video)) {
		HnmPlayer player(_system);
		const HnmPlayer::Result result = player.play(video, "LOGO.HNM", (isDumpRun() || isDuneHarnessRun()) ? 12 : -1);
		_log.line(Common::String::format("Intro: LOGO.HNM result %d", (int)result));
		if (result == HnmPlayer::kQuit)
			return false;
		_aborted = result == HnmPlayer::kSkipped;
		_skipPrologue = _aborted && player.skippedWithEscape();
	}

	const bool manual = !ConfMan.hasKey("dune_speedrun") && !isRecording() && !isDumpRun() && !isDuneHarnessRun();
	for (;;) {
		playMovie(startScene);
		// A passive floppy movie returns to the Cryo/Virgin logo sequence.
		// Capture and campaign playback continue into the prologue after one pass.
		if (_aborted || _quit || !manual || startScene >= _sceneNumber)
			break;
		startScene = 0;
		startSong("WORMINTR.HSQ");
	}
	if (_aborted && !_quit && !_skipPrologue) {
		_log.line("Intro: skipped to the prologue");
		_aborted = false;
	}
	if (++_sceneNumber > startScene && !_skipPrologue && !_quit) {
		_log.line(Common::String::format("Intro scene %d", _sceneNumber));
		prologue();
	}
	_log.line(_aborted ? "Intro: prologue skipped" : "Intro: floppy sequence finished");
	return !_quit;
}

void FloppyIntro::playMovie(int startScene) {
	_sceneNumber = 0;
	auto due = [&]() {
		++_sceneNumber;
		if (_sceneNumber <= startScene)
			return false;
		_log.line(Common::String::format("Intro scene %d", _sceneNumber));
		return true;
	};
	// Scene order: swift-dune Intro.swift; durations and transitions from the
	// recording where it differs (see intro_first.cpp and intro.h).
	if (due())
		logoSwap();
	if (due())
		presentsScene("intro-virgin", false);
	if (due())
		presentsScene("intro-cryo", true);
	if (due())
		starsScene("intro-stars");
	if (due())
		titleScene("intro-title");
	if (due())
		wormScene("intro-worm");
	if (due())
		backdropCharacterScene("intro-paul", "red", "PAUL.HSQ", nullptr, 0, false, 5000, kFade, 500, kFade, 1000);
	if (due())
		sunriseScene("intro-sunrise", 8000);
	if (due())
		chaniCloseUpScene("intro-chani-zoom");
	if (due())
		chaniZoomOutScene("intro-chani-animation", 6000);
	if (due())
		sunriseCharacterScene("intro-liet", "KYNE.HSQ", true, 3500);
	if (due())
		sunriseCharacterScene("intro-chani-reprise", "CHAN.HSQ", false, 4000);
	if (due())
		desertPaulScene("intro-paul-desert", 4000);

	// PERS.HSQ frames: Leto 0, Jessica 2, Thufir 4, Duncan 6, Gurney 8,
	// Stilgar 10, Liet 12, Chani 14, Harah 16 (odd frames are their tiny
	// map markers). Marker numbers are the room's own, in file order.
	static const MarkerCharacter sietchFamily[] = { { 6, 16 }, { 9, 10 } };
	static const MarkerCharacter balconyFamily[] = { { 4, 8 }, { 5, 6 }, { 6, 4 }, { 7, 2 }, { 8, 0 } };
	static const Common::Rect letoZoom(75, 45, 75 + 84, 45 + 40);
	static const Common::Rect jessicaZoom(170, 63, 170 + 84, 63 + 40);
	static const uint16 feydAnimations[] = { 1, 4 };

	if (due())
		sietchScene("intro-sietch-1", 8, sietchFamily, ARRAYSIZE(sietchFamily), nullptr, 4000, kFade, 1000, kNoTransition, 0);
	if (due())
		sietchScene("intro-sietch-water", 12, nullptr, 0, nullptr, 4000, kNoTransition, 0, kNoTransition, 0);
	if (due())
		sietchScene("intro-sietch-2", 8, sietchFamily, ARRAYSIZE(sietchFamily), nullptr, 4000, kNoTransition, 0, kNoTransition, 0);
	if (due())
		sietchScene("intro-stilgar", 12, nullptr, 0, "STIL.HSQ", 4000, kNoTransition, 0, kDissolve, 300);
	if (due())
		desertWalkScene("intro-desert-walk", kSkyDay, 2000, kDissolve, 300, kNoTransition, 0);
	if (due())
		palaceScene("intro-palace-stairs", 11, nullptr, 0, nullptr, nullptr, 2000, kNoTransition, 0);
	if (due())
		palaceScene("intro-palace-balcony", 10, balconyFamily, ARRAYSIZE(balconyFamily), nullptr, nullptr, 2000, kNoTransition, 0);
	if (due())
		palaceScene("intro-leto", 10, nullptr, 0, &letoZoom, "LETO.HSQ", 4000, kNoTransition, 0);
	if (due())
		palaceScene("intro-jessica", 10, nullptr, 0, &jessicaZoom, "JESS.HSQ", 4000, kDissolve, 300);
	if (due())
		backdropCharacterScene("intro-paul-2", "red", "PAUL.HSQ", nullptr, 0, false, 4000, kDissolve, 300, kDissolve, 300);
	if (due())
		sunsetFortScene("intro-sunset-fort", 5000);
	if (due())
		backdropCharacterScene("intro-baron", "baron", "BARO.HSQ", nullptr, 0, false, 2000, kDissolve, 300, kNoTransition, 0);
	if (due())
		backdropCharacterScene("intro-feyd", "feyd", "FEYD.HSQ", feydAnimations, ARRAYSIZE(feydAnimations), false, 4000, kNoTransition, 0, kNoTransition, 0);
	if (due())
		backdropCharacterScene("intro-baron-sardaukar", "baron", "BARO.HSQ", nullptr, 0, true, 3000, kNoTransition, 0, kDissolve, 300);
	if (due())
		attackScene("intro-attack");
	if (due())
		backdropCharacterScene("intro-paul-3", "red", "PAUL.HSQ", nullptr, 0, false, 4000, kDissolve, 300, kDissolve, 300);
	if (due())
		kissScene("intro-kiss");
	if (due())
		ornithopterScene("intro-ornithopter");
	// swift-dune flies for 60 s; the original's recording lands after 22 s.
	if (due())
		flightScene("intro-flight", 22000);
	if (due())
		creditsScene("intro-credits");
}

bool FloppyIntro::playVideo(const char *name, uint top) {
	if (_aborted || _quit)
		return false;
	Common::Array<byte> video;
	if (!_resources.load(name, video)) {
		_log.line(Common::String::format("Intro: %s missing", name));
		return true;
	}
	HnmPlayer player(_system);
	player.setTop(top);
	const HnmPlayer::Result r = player.play(video, name, isFastIntroCapture() ? 40 : -1);
	_log.line(Common::String::format("Intro: %s result %d", name, (int)r));
	if (r == HnmPlayer::kQuit)
		_quit = true;
	else if (r == HnmPlayer::kSkipped)
		_aborted = true;
	return !_aborted && !_quit;
}

bool FloppyIntro::playCdStory() {
	// intro_script entries 11-47 (seg000:0337): 11 the desert sky, 12 MTG1,
	// 13-18 the palace (equipment room, Jessica, Leto, Jessica, Paul, the
	// outside), 19 MTG2, 20-23 the sietch (inside, Chani, Kynes, somebody),
	// 24 midnight, 25-28 the Harkonnens and the night attack, 29 MTG3, 30-47
	// the INT stills of the prologue with 38 the water ripples, 39 PLANT and
	// 41 VER.HNM. The scenes are the floppy port's closest ones.
	debugSetRoom(-1);
	debugSetScene("cd/story");
	_sceneNumber = 100;
	_introStart = _lastTimedDump = _system->getMillis();
	static const MarkerCharacter sietchFamily[] = { { 6, 16 }, { 9, 10 } };
	static const MarkerCharacter balconyFamily[] = { { 4, 8 }, { 5, 6 }, { 6, 4 }, { 7, 2 }, { 8, 0 } };
	// Left out on the CD until their CD rooms are drawn: the palace stairs,
	// Jessica's and Leto's talks, Chani, Stilgar and the water cave (the
	// floppy's sheets for them garble on the CD data).
	auto go = [&]() { return !_aborted && !_quit; };
	if (go())
		sunriseScene("cd-desert-sky", 6000);
	if (go())
		playVideo("MTG1.HNM", 24);
	if (go())
		palaceScene("cd-balcony", 10, balconyFamily, ARRAYSIZE(balconyFamily), nullptr, nullptr, 2000, kNoTransition, 0);
	if (go())
		backdropCharacterScene("cd-paul", "red", "PAUL.HSQ", nullptr, 0, false, 4000, kDissolve, 300, kDissolve, 300);
	if (go())
		playVideo("MTG2.HNM", 24);
	if (go())
		sietchScene("cd-sietch", 8, sietchFamily, ARRAYSIZE(sietchFamily), nullptr, 4000, kFade, 1000, kNoTransition, 0);
	if (go())
		sunriseCharacterScene("cd-kynes", "KYNE.HSQ", true, 3500);
	if (go())
		desertWalkScene("cd-midnight", kSkyNight, 2000, kDissolve, 300, kNoTransition, 0);
	if (go())
		sunsetFortScene("cd-sunset-fort", 4000);
	if (go())
		backdropCharacterScene("cd-baron", "baron", "BARO.HSQ", nullptr, 0, false, 3000, kDissolve, 300, kNoTransition, 0);
	if (go())
		backdropCharacterScene("cd-baron-guards", "baron", "BARO.HSQ", nullptr, 0, true, 3000, kNoTransition, 0, kDissolve, 300);
	if (go())
		attackScene("cd-attack");
	if (go())
		playVideo("MTG3.HNM", 24);
	if (go())
		playVideo("PLANT.HNM", 24);
	_log.line(_quit ? "Intro: CD story quit" : _aborted ? "Intro: CD story skipped" : "Intro: CD story finished");
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
	debugOverlay(*_surface.surfacePtr());
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
	// Timed dumps (see dumpEveryMillis) name frames by scene and intro time.
	const uint every = dumpEveryMillis();
	if (every && isDumpRun() && !isDuneHarnessRun()) {
		const uint32 now = _system->getMillis();
		if (now - _lastTimedDump >= every) {
			_lastTimedDump = now;
			dumpScreen(_system, Common::String::format("s%02d-%06u", _sceneNumber, now - _introStart).c_str());
		}
	}
}

// Pump events during the cinematic, its transitions and each prologue page.
bool FloppyIntro::wait(uint32 millis) {
	const uint32 end = _system->getMillis() + (isFastIntroCapture() ? 0 : millis);
	Common::Event event;
	do {
		while (pollDuneEvent(_system, event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				_quit = true;
			else if (!isFastIntroCapture() && (event.type == Common::EVENT_LBUTTONDOWN ||
			         (event.type == Common::EVENT_KEYDOWN && !event.kbdRepeat))) {
				if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE)
					_skipPrologue = true;
				else if (_manualPrologue)
					return false; // End this page's render loop. The next page receives its own input.
				_aborted = true;
			}
		}
		if (_aborted || _quit)
			return false;
		if (_system->getMillis() >= end)
			return true;
		_system->delayMillis(10);
	} while (true);
}

void FloppyIntro::fade(bool in) {
	for (uint step = 0; step <= 16 && !isFastIntroCapture(); ++step) {
		applyPalette(in ? step * 16 : 256 - step * 16);
		_system->updateScreen();
		if (!wait(25))
			return;
	}
	applyPalette(in ? 256 : 0);
	_system->updateScreen();
}

} // namespace Dune

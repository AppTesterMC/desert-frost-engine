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

// First half of the floppy intro: from the Cryo logo to Paul in the desert.
// Each scene follows the matching swift-dune node (LogoSwap.swift,
// Presents.swift, Stars.swift, DuneTitle.swift, WormCall.swift,
// Sunrise.swift, Character.swift; reuse permitted, see CREDITS.md). Where the
// recording of the original (RPReplay_Final1789852776.mov) shows other
// timings, the recording wins and the comment says so.

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/intro.h"
#include "dune/resource.h"
#include "dune/sky.h"
#include "dune/sound.h"
#include "dune/sprite.h"

namespace Dune {

namespace {

bool fastCapture() {
	return (isDumpRun() && !dumpEveryMillis()) || isDuneFastHarness();
}

uint32 ramp256(uint32 elapsed, uint32 start, uint32 length) {
	if (!length || elapsed <= start)
		return 0;
	return MIN<uint32>(256, (elapsed - start) * 256 / length);
}

// Effects.swift zoom: the view shows the rectangle of the picture, magnified.
void magnify(const Graphics::Surface &picture, Graphics::Surface &view, const Common::Rect &rect) {
	for (int y = 0; y < view.h; ++y) {
		const int sourceY = CLIP<int>(rect.top + y * rect.height() / view.h, 0, picture.h - 1);
		for (int x = 0; x < view.w; ++x) {
			const int sourceX = CLIP<int>(rect.left + x * rect.width() / view.w, 0, picture.w - 1);
			*(byte *)view.getBasePtr(x, y) = *(const byte *)picture.getBasePtr(sourceX, sourceY);
		}
	}
}

// Effects.swift flip: the picture squeezed vertically about its centre line.
void squeeze(const Graphics::Surface &picture, Graphics::Surface &view, uint32 height256) {
	const int height = (int)(picture.h * height256 / 256);
	if (height <= 0)
		return;
	const int top = (picture.h - height) / 2;
	for (int y = 0; y < height && top + y < view.h; ++y) {
		const int sourceY = CLIP<int>(y * picture.h / height, 0, picture.h - 1);
		memcpy(view.getBasePtr(0, top + y), picture.getBasePtr(0, sourceY), MIN<int>(picture.w, view.w));
	}
}

} // namespace

// Sunrise.swift drawDesertBackground: SUNRS frames 2-6 and 0 stacked, with
// the village (1) or the Harkonnen fort (7) when asked.
bool FloppyIntro::drawSunrise(Sprite &background, Graphics::Surface &view, bool village, bool fort) {
	background.drawFrame(2, &view, 0, 0);
	background.drawFrame(3, &view, 0, 25);
	background.drawFrame(4, &view, 0, 50);
	background.drawFrame(5, &view, 0, 74);
	background.drawFrame(6, &view, 134, 92);
	background.drawFrame(0, &view, 0, 102);
	if (village)
		background.drawFrame(1, &view, 84, 11);
	if (fort)
		background.drawFrame(7, &view, 19, 74);
	return true;
}

// LogoSwap.swift: after LOGO.HNM the Cryo logo box (CRYO.HSQ 0-4 at 113,15)
// holds, flips out, the Virgin Games logo (5 at 98,13 and 6 at 98,118)
// flips in, holds and fades. Recording: Cryo 2.5-5.0 s, Virgin 5.5-7.5 s,
// dark at 8 s (swift-dune keeps the Cryo box only 1.28 s).
void FloppyIntro::logoSwap() {
	Common::Array<byte> data;
	if (_aborted || _quit || !_resources.load("CRYO.HSQ", data))
		return;
	Sprite sheet(_system, data);
	Graphics::ManagedSurface picture;
	picture.create(320, kViewHeight, Graphics::PixelFormat::createFormatCLUT8());
	const uint32 cryoEnd = 2400, flip = 300, virginStart = 2500, fadeStart = 4800, duration = 5500;
	int drawn = -1;
	runScene("intro-logo-swap", duration, kNoTransition, 0, kFade, duration - fadeStart, 3500,
			[&](uint32 elapsed, Graphics::Surface &view) {
		const int wanted = elapsed <= cryoEnd ? 0 : elapsed >= virginStart ? 1 : 2;
		if (wanted != drawn) {
			picture.fillRect(Common::Rect(0, 0, 320, kViewHeight), 0);
			sheet.setPalette();
			if (wanted == 0)
				for (uint frame = 0; frame < 5; ++frame)
					sheet.drawFrame(frame, picture.surfacePtr(), 113, 15);
			else if (wanted == 1) {
				sheet.drawFrame(5, picture.surfacePtr(), 98, 13);
				sheet.drawFrame(6, picture.surfacePtr(), 98, 118);
			}
			drawn = wanted;
		}
		uint32 height = 256;
		if (wanted == 0 && elapsed + flip > cryoEnd)
			height = 256 * (cryoEnd - elapsed) / flip;
		else if (wanted == 1 && elapsed < virginStart + flip)
			height = 256 * (elapsed - virginStart) / flip;
		if (wanted == 2)
			height = 0;
		squeeze(*picture.surfacePtr(), view, fastCapture() ? 256 : height);
	});
	picture.free();
}

// Presents.swift: "Virgin Games presents" (INTDS 6 at 62,58 and 7 at 114,90)
// for 3.8 s, then "A production from Cryo" (8 at 76,43; 9 at 132,71;
// 10 at 14,96) for 5.51 s, each fading 0.9 s in and out.
void FloppyIntro::presentsScene(const char *dumpName, bool cryo) {
	Common::Array<byte> data;
	if (_aborted || _quit || !_resources.load("INTDS.HSQ", data))
		return;
	Sprite sheet(_system, data);
	const uint32 duration = cryo ? 5510 : 3800;
	runScene(dumpName, duration, kFade, 900, kFade, 900, duration / 2, [&](uint32, Graphics::Surface &view) {
		sheet.setPalette();
		if (cryo) {
			sheet.drawFrame(8, &view, 76, 43);
			sheet.drawFrame(9, &view, 132, 71);
			sheet.drawFrame(10, &view, 14, 96);
		} else {
			sheet.drawFrame(6, &view, 62, 58);
			sheet.drawFrame(7, &view, 114, 90);
		}
	});
}

// Stars.swift planetsPan: the star panorama (frames 0-2 placed 302 apart)
// with the sun (36), then a pan of 604 pixels while Arrakis (3-35) grows
// towards the centre and a red planet (37-44) passes. Recording: the stars
// fade in over 2 s and stand still until 29.5 s (6.5 s in), the pan and the
// growth take 2 s, then the picture zooms into the planet (0.5 s) and the
// title arrives pixelated. swift-dune fades 7.34 s and pans the rest.
void FloppyIntro::starsScene(const char *dumpName) {
	Common::Array<byte> data;
	if (_aborted || _quit || !_resources.load("STARS.HSQ", data))
		return;
	Sprite sheet(_system, data);
	const uint32 duration = 9840, panStart = 6500, panLength = 2840, zoom = 500;
	_zoomTarget = Common::Rect(140, 66, 180, 85);
	runScene(dumpName, duration, kFade, 2000, kZoom, zoom, 8500, [&](uint32 elapsed, Graphics::Surface &view) {
		const uint32 ratio = ramp256(MIN(elapsed, duration - zoom), panStart, panLength); // 0..256
		// The panorama is three 302-pixel frames: a pan of one frame keeps
		// stars on the right, as the recording shows (swift-dune's 604 leaves
		// its right half black once the fade no longer holds the pan back).
		const int scroll = (int)(302 * ratio / 256);
		sheet.setPalette();
		sheet.drawFrame(0, &view, -scroll, 0);
		sheet.drawFrame(1, &view, 302 - scroll, 0);
		sheet.drawFrame(2, &view, 604 - scroll, 0);
		sheet.drawFrame(36, &view, 45 - scroll * 2, 60);

		uint32 offset;
		uint16 width, height;
		bool compressed;
		int8 paletteOffset;
		const uint16 arrakis = (uint16)(3 + MIN<uint32>(32 * ratio / 256, 32));
		if (sheet.getFrameInfo(3, offset, width, height, compressed, paletteOffset)) {
			const int firstDelta = (320 - width) / 2 - 124;
			if (sheet.getFrameInfo(arrakis, offset, width, height, compressed, paletteOffset))
				sheet.drawFrame(arrakis, &view, (320 - width) / 2 - firstDelta * (int)(256 - ratio) / 256,
						(152 - height) / 2);
		}
		const uint16 redPlanet = (uint16)(37 + MIN<uint32>(7 * ratio / 256, 7));
		if (sheet.getFrameInfo(redPlanet, offset, width, height, compressed, paletteOffset)) {
			const int x = ratio < 128 ? 235 + (int)(80 * ratio / 128) : 315 - (int)(315 * (ratio - 128) * 3 / 256);
			sheet.drawFrame(redPlanet, &view, x, 76 + (76 - height) / 2);
		}
	});
}

// DuneTitle.swift: the dunes (INTDS 1 at y=44 under the midday sky) come in
// pixelated, scroll down while the title backdrop (2, 3, 4) follows from
// above, then the red DUNE letters (5 at 0,48; colours 224-239) fade in.
// Recording: pixelate 1 s, dunes still until 4 s, scroll 4-7 s, letters
// 7.5-8.5 s, hold, fade 0.5 s at 14.5 s.
void FloppyIntro::titleScene(const char *dumpName) {
	Common::Array<byte> data;
	if (_aborted || _quit || !_resources.load("INTDS.HSQ", data))
		return;
	Sprite sheet(_system, data);
	Graphics::ManagedSurface dunes;
	dunes.create(320, kViewHeight, Graphics::PixelFormat::createFormatCLUT8());
	drawSky(_system, _resources, *dunes.surfacePtr(), kSkyNarrow, 320, kSkyDay);
	sheet.setPalette();
	sheet.drawFrame(1, dunes.surfacePtr(), 0, 44);

	const uint32 duration = 15000, scrollStart = 4000, scrollEnd = 7000, lettersStart = 7500, lettersFade = 1000;
	runScene(dumpName, duration, kPixelate, 1000, kFade, 500, 10000, [&](uint32 elapsed, Graphics::Surface &view) {
		const int scroll = (int)(152 * ramp256(elapsed, scrollStart, scrollEnd - scrollStart) / 256);
		drawSky(_system, _resources, view, kSkyNarrow, 320, kSkyDay);
		sheet.setPalette();
		for (int y = scroll; y < kViewHeight; ++y)
			memcpy(view.getBasePtr(0, y), dunes.getBasePtr(0, y - scroll), 320);
		if (scroll > 0) {
			const int top = scroll - 152;
			sheet.drawFrame(2, &view, 0, top);
			sheet.drawFrame(3, &view, 0, top + 95);
			sheet.drawFrame(4, &view, 0, top + 123);
		}
		if (elapsed >= lettersStart)
			sheet.drawFrame(5, &view, 0, 48);
		// The letters' colours fade in on their own (Effects.fade 224-239).
		byte palette[16 * 3];
		_system->getPaletteManager()->grabPalette(palette, 224, 16);
		const uint32 level = fastCapture() ? 256 : ramp256(elapsed, lettersStart, lettersFade);
		for (uint i = 0; i < sizeof(palette); ++i)
			palette[i] = (byte)((palette[i] * level) >> 8);
		_system->getPaletteManager()->setPalette(palette, 224, 16);
	});
	dunes.free();
}

// WormCall.swift: the desert (SHAI 44 at 0,74 under the midday sky) and the
// authored SHAI/SHAI2 animation. Recording: the sand puffs (groups 0-21)
// take 3.5 s, the worm surfaces and dives in the next 1.8 s (12 fps), the
// empty desert holds and the scene fades out at 7 s. SD8 is the call.
void FloppyIntro::wormScene(const char *dumpName) {
	Common::Array<byte> wormData, wormExtension, soundData;
	if (_aborted || _quit || !_resources.load("SHAI.HSQ", wormData) || !_resources.load("SHAI2.HSQ", wormExtension))
		return;
	Sprite worm(_system, wormData), wormFrames(_system, wormExtension);
	worm.setShaiAnimationFormat();
	worm.mergeFrames(wormFrames);
	// Groups 0-21 are the sand puffs, 22-41 the worm and its trail; the
	// last two groups only restore the ground and are not drawn.
	const uint frames = MIN<uint>(42, worm.animationFrameCount(0));
	const uint puffFrames = MIN<uint>(22, frames);
	Sound call(_system);
	if (!fastCapture() && _resources.load("SD8.HSQ", soundData))
		call.playVOC(soundData);
	const uint32 duration = 8000, puffs = 3500;
	runScene(dumpName, duration, kFade, 500, kFade, 1000, 4700, [&](uint32 elapsed, Graphics::Surface &view) {
		drawSky(_system, _resources, view, kSkyNarrow, 320, kSkyDay);
		worm.setPalette();
		worm.drawFrame(44, &view, 0, 74);
		if (!frames)
			return;
		uint frame;
		if (elapsed < puffs)
			frame = (uint)(elapsed * puffFrames / puffs);
		else
			frame = puffFrames + (uint)((elapsed - puffs) * 12 / 1000);
		if (frame < frames)
			worm.drawAnimationFrame(0, (uint16)frame, &view, 0, 0);
	});
}

// Sunrise.swift in sunrise mode: SUNRS palettes 0, 1 and 2 blend one into
// the next. Recording: dark red to full daylight over 5 s, then the day
// palette holds; no fade in (the previous scene faded to black).
void FloppyIntro::sunriseScene(const char *dumpName, uint32 duration) {
	Common::Array<byte> data;
	if (_aborted || _quit || !_resources.load("SUNRS.HSQ", data))
		return;
	Sprite background(_system, data);
	background.setPalette(); // the sheet's own colours; the records only recolour the sky
	const uint32 stepLength = 2500;
	runScene(dumpName, duration, kNoTransition, 0, kNoTransition, 0, 6000, [&](uint32 elapsed, Graphics::Surface &view) {
		const uint step = MIN<uint32>(2, elapsed / stepLength);
		if (step < 2)
			background.setPaletteRecordBlend(8 + step + 1, 8 + step, (elapsed % stepLength) * 256 / stepLength);
		else
			background.setPaletteRecord(8 + 2);
		drawSunrise(background, view, false, false);
	});
}

// Intro.swift: Chani under the day palette, seen through the rectangle
// (48,48,80,38) magnified to the full view; one frozen frame, a hard cut in
// and out. Recording: 1.75 s.
void FloppyIntro::chaniCloseUpScene(const char *dumpName) {
	Common::Array<byte> backgroundData, characterData;
	if (_aborted || _quit || !_resources.load("SUNRS.HSQ", backgroundData) || !_resources.load("CHAN.HSQ", characterData))
		return;
	Sprite background(_system, backgroundData), character(_system, characterData);
	background.setPalette();
	Graphics::ManagedSurface picture;
	picture.create(320, kViewHeight, Graphics::PixelFormat::createFormatCLUT8());
	runScene(dumpName, 1750, kNoTransition, 0, kNoTransition, 0, 900, [&](uint32, Graphics::Surface &view) {
		background.setPaletteRecord(8 + 2);
		drawSunrise(background, *picture.surfacePtr(), false, false);
		character.setPalette();
		character.drawAnimationFrame(0, 0, picture.surfacePtr(), 0, 0);
		magnify(*picture.surfacePtr(), view, Common::Rect(48, 48, 128, 86));
	});
	picture.free();
}

// Intro.swift: the same picture zooms out to the full view in 0.25 s, then
// Chani's animation 0 plays. Recording: 6 s in all (swift-dune 2.5 s).
void FloppyIntro::chaniZoomOutScene(const char *dumpName, uint32 duration) {
	Common::Array<byte> backgroundData, characterData;
	if (_aborted || _quit || !_resources.load("SUNRS.HSQ", backgroundData) || !_resources.load("CHAN.HSQ", characterData))
		return;
	Sprite background(_system, backgroundData), character(_system, characterData);
	background.setPalette();
	Graphics::ManagedSurface picture;
	picture.create(320, kViewHeight, Graphics::PixelFormat::createFormatCLUT8());
	const uint32 zoom = 250;
	runScene(dumpName, duration, kNoTransition, 0, kNoTransition, 0, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		background.setPaletteRecord(8 + 2);
		drawSunrise(background, *picture.surfacePtr(), false, false);
		character.setPalette();
		if (elapsed < zoom) {
			character.drawAnimationFrame(0, 0, picture.surfacePtr(), 0, 0);
			const uint32 p = ramp256(elapsed, 0, zoom);
			magnify(*picture.surfacePtr(), view, Common::Rect((int16)(48 - 48 * p / 256), (int16)(48 - 48 * p / 256),
					(int16)(128 + 192 * p / 256), (int16)(86 + 66 * p / 256)));
		} else {
			character.drawAnimation(0, elapsed - zoom, picture.surfacePtr(), 0, 0);
			memcpy(view.getPixels(), picture.getPixels(), 320 * kViewHeight);
		}
	});
	picture.free();
}

// Intro.swift: a character under the day palette playing animation 1 then
// idling in 2 (Character.swift), Liet with the village behind him. Hard cuts.
// Recording: Liet 3.5 s, Chani again 4 s (swift-dune 2 s each).
void FloppyIntro::sunriseCharacterScene(const char *dumpName, const char *characterName, bool village, uint32 duration) {
	Common::Array<byte> backgroundData, characterData;
	if (_aborted || _quit || !_resources.load("SUNRS.HSQ", backgroundData) || !_resources.load(characterName, characterData))
		return;
	Sprite background(_system, backgroundData), character(_system, characterData);
	background.setPalette();
	CharacterPlayer player;
	player.sprite = &character;
	player.queue.push_back(1);
	player.queue.push_back(2);
	player.idle = 2;
	player.current = -1;
	player.animationStart = player.animationDuration = 0;
	player.offsetX = player.offsetY = 0;
	runScene(dumpName, duration, kNoTransition, 0, kNoTransition, 0, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		background.setPaletteRecord(8 + 2);
		drawSunrise(background, view, village, false);
		drawCharacter(player, view, elapsed);
	});
}

// Background.swift desert + Character.swift: Paul (animation 0) before the
// dunes (INTDS 0 at y=60) under the midday sky, fading out over the last
// second (recording; swift-dune 2 s).
void FloppyIntro::desertPaulScene(const char *dumpName, uint32 duration) {
	Common::Array<byte> backgroundData, characterData;
	if (_aborted || _quit || !_resources.load("INTDS.HSQ", backgroundData) || !_resources.load("PAUL.HSQ", characterData))
		return;
	Sprite background(_system, backgroundData), character(_system, characterData);
	runScene(dumpName, duration, kNoTransition, 0, kFade, 1000, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		drawSky(_system, _resources, view, kSkyNarrow, 320, kSkyDay);
		background.setPalette();
		background.drawFrame(0, &view, 0, 60);
		character.setPalette();
		character.drawAnimation(0, elapsed, &view, 0, 0);
	});
}

} // namespace Dune

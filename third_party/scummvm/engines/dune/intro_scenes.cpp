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

// Second half of the floppy intro, the credits and the narrated prologue.
// Every scene here is a transcription of the matching swift-dune node
// (codingstyle, reuse permitted; see CREDITS.md), checked against the
// recording of the original. Comments name the Swift file a scene follows.

#include "common/array.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/random.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/attack.h"
#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/intro.h"
#include "dune/music.h"
#include "dune/resource.h"
#include "dune/room.h"
#include "dune/scene.h"
#include "dune/sky.h"
#include "dune/sound.h"
#include "dune/sprite.h"

namespace Dune {

namespace {

bool isFastCapture() {
	return (isDumpRun() && !dumpEveryMillis()) || isDuneHarnessRun();
}

// The original's 4x4 dissolve order (Effects.swift pixelTransitionOffsets).
const byte kDissolveOrder[16][2] = {
	{ 1, 1 }, { 0, 3 }, { 3, 2 }, { 2, 0 }, { 0, 1 }, { 2, 3 }, { 0, 0 }, { 1, 2 },
	{ 3, 0 }, { 1, 3 }, { 2, 1 }, { 3, 3 }, { 2, 2 }, { 1, 0 }, { 3, 1 }, { 0, 2 }
};

// Colour 63 with the original's 127 offset: the desert floor under a sky.
const byte kDesertFloor = 190;

uint32 progress256(uint32 elapsed, uint32 start, uint32 length) {
	if (!length || elapsed <= start)
		return 0;
	return MIN<uint32>(256, (elapsed - start) * 256 / length);
}

void copyView(const Graphics::Surface &source, Graphics::Surface &target) {
	for (int y = 0; y < target.h && y < source.h; ++y)
		memcpy(target.getBasePtr(0, y), source.getBasePtr(0, y), MIN<int>(source.w, target.w));
}

void zoomView(const Graphics::Surface &source, Graphics::Surface &target, const Common::Rect &rect) {
	for (int destY = 0; destY < target.h; ++destY) {
		const int sourceY = CLIP<int>(rect.top + destY * rect.height() / target.h, 0, source.h - 1);
		for (int destX = 0; destX < target.w; ++destX) {
			const int sourceX = CLIP<int>(rect.left + destX * rect.width() / target.w, 0, source.w - 1);
			*(byte *)target.getBasePtr(destX, destY) = *(const byte *)source.getBasePtr(sourceX, sourceY);
		}
	}
}

} // namespace

// ---- scene runner -----------------------------------------------------------

void FloppyIntro::dissolve(Graphics::Surface &view, uint clearedPerBlock) {
	clearedPerBlock = MIN<uint>(clearedPerBlock, 16);
	for (int y = 0; y < view.h; y += 4)
		for (int x = 0; x < view.w; x += 4)
			for (uint n = 0; n < clearedPerBlock; ++n) {
				const int px = x + kDissolveOrder[n][0], py = y + kDissolveOrder[n][1];
				if (px < view.w && py < view.h)
					*(byte *)view.getBasePtr(px, py) = 0;
			}
}

// Effects.swift pixelate: every pixel takes the colour of its block's corner.
void FloppyIntro::pixelate(Graphics::Surface &view, uint blockSize) {
	if (blockSize < 2)
		return;
	for (int y = view.h - 1; y >= 0; --y)
		for (int x = view.w - 1; x >= 0; --x)
			*(byte *)view.getBasePtr(x, y) = *(const byte *)view.getBasePtr(x - x % blockSize, y - y % blockSize);
}

bool FloppyIntro::runSceneImpl(const char *dumpName, uint32 duration, Transition in, uint32 inMillis, Transition out,
		uint32 outMillis, uint32 captureAt, SceneRenderer &renderer) {
	if (_aborted || _quit)
		return false;
	debugSetScene(Common::String::format("floppy/%s", dumpName));

	Graphics::ManagedSurface composed;
	composed.create(320, kViewHeight, Graphics::PixelFormat::createFormatCLUT8());
	const uint32 start = _system->getMillis();
	while (!_aborted && !_quit) {
		const uint32 elapsed = isFastCapture() ? captureAt : MIN<uint32>(_system->getMillis() - start, duration);
		// As in swift-dune's Intro node, the picture stands still during a
		// transition: the first frame while coming in, the last while going out.
		uint32 sceneTime = elapsed;
		if (in != kNoTransition && elapsed < inMillis)
			sceneTime = inMillis;
		else if (out != kNoTransition && outMillis && elapsed > duration - outMillis)
			sceneTime = duration - outMillis;

		_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);
		composed.fillRect(Common::Rect(0, 0, 320, kViewHeight), 0);
		renderer.render(sceneTime, *composed.surfacePtr());
		Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, _viewTop, 320, _viewTop + kViewHeight));
		if (out == kZoom && outMillis && elapsed > duration - outMillis && !isFastCapture()) {
			const uint32 p = progress256(elapsed, duration - outMillis, outMillis);
			const Common::Rect rect((int16)(_zoomTarget.left * p / 256), (int16)(_zoomTarget.top * p / 256),
					(int16)(320 - (320 - _zoomTarget.right) * p / 256), (int16)(kViewHeight - (kViewHeight - _zoomTarget.bottom) * p / 256));
			zoomView(*composed.surfacePtr(), view, rect);
		} else {
			copyView(*composed.surfacePtr(), view);
		}

		uint32 level = 256;
		if (!isFastCapture()) {
			if (in == kFade && elapsed < inMillis)
				level = progress256(elapsed, 0, inMillis);
			else if (out == kFade && outMillis && elapsed > duration - outMillis)
				level = 256 - progress256(elapsed, duration - outMillis, outMillis);
			if (in == kPixelate && elapsed < inMillis)
				pixelate(view, 16 - 15 * elapsed / inMillis);
			if (in == kDissolve && elapsed < inMillis)
				dissolve(view, (16 * (inMillis - elapsed) + inMillis / 2) / inMillis);
			else if (out == kDissolve && outMillis && elapsed > duration - outMillis)
				dissolve(view, (16 * (elapsed - (duration - outMillis)) + outMillis / 2) / outMillis);
		}
		// Scenes set their palettes while rendering; fades scale that result.
		_system->getPaletteManager()->grabPalette(_palette, 0, 256);
		if (level < 256)
			applyPalette(level);
		present();

		if (isFastCapture()) {
			dumpScreen(_system, dumpName);
			break;
		}
		if (elapsed >= duration)
			break;
		if (!wait(25))
			break;
	}
	composed.free();
	return !_aborted && !_quit;
}

// ---- shared drawing ---------------------------------------------------------

bool FloppyIntro::drawSkyView(Graphics::Surface &view, SkyType type, int width, uint palette) {
	return drawSky(_system, _resources, view, type, width, palette);
}

void FloppyIntro::fillView(Graphics::Surface &view, const Common::Rect &rect, byte colour) {
	view.fillRect(rect, colour);
}

void FloppyIntro::drawScaled(Sprite &sheet, uint16 frame, Graphics::Surface &view, int x, int y, uint percent) {
	if (!percent)
		return;
	sheet.drawFrameScaled(frame, &view, x, y, 25600 / percent);
}

// Background.swift: the red curtain behind Paul.
bool FloppyIntro::drawRedBackdrop(Sprite &back, Graphics::Surface &view) {
	back.setPalette();
	back.drawFrame(0, &view, 0, 0);
	back.drawFrame(1, &view, 52, 25);
	back.drawFrame(2, &view, 108, 51);
	return true;
}

// Background.swift: the Harkonnen hall, one pillar mirrored.
bool FloppyIntro::drawBaronBackdrop(Sprite &back, Graphics::Surface &view) {
	back.setPalette();
	back.drawFrame(4, &view, 0, 0, true);
	back.drawFrame(4, &view, 236, 0);
	back.drawFrame(3, &view, 84, 0);
	return true;
}

// Background.swift: Feyd between two rows of Sardaukar.
bool FloppyIntro::drawFeydBackdrop(Sprite &back, Graphics::Surface &view) {
	back.setPalette();
	view.fillRect(Common::Rect(84, 0, 236, 152), 48);
	back.drawFrame(4, &view, 0, 0, true);
	back.drawFrame(4, &view, 236, 0);
	back.drawFrame(5, &view, -53, 8);
	back.drawFrame(6, &view, -53, 8);
	back.drawFrame(5, &view, 0, 13);
	back.drawFrame(6, &view, 0, 13);
	back.drawFrame(7, &view, 200, 13);
	back.drawFrame(8, &view, 200, 13);
	return true;
}

// Character.swift: queued animations each run frames x 160 ms; the last one
// is the idle loop.
void FloppyIntro::drawCharacter(CharacterPlayer &player, Graphics::Surface &view, uint32 elapsed) {
	if (player.current < 0 || elapsed - player.animationStart >= player.animationDuration) {
		player.animationStart = elapsed;
		if (!player.queue.empty()) {
			player.current = player.queue[0];
			player.queue.remove_at(0);
			player.animationDuration = (uint32)player.sprite->animationFrameCount(player.current) * 160;
			if (!player.animationDuration)
				player.animationDuration = 160;
		} else {
			player.current = player.idle;
			player.animationDuration = 0xffffffffu;
		}
	}
	player.sprite->setPalette();
	if (player.sprite->animationFrameCount(player.current))
		player.sprite->drawAnimation(player.current, elapsed - player.animationStart, &view, player.offsetX, player.offsetY);
	else
		player.sprite->drawFrame(0, &view, player.offsetX, player.offsetY);
}

void FloppyIntro::markerTable(const MarkerCharacter *markers, uint markerCount, Common::Array<uint16> &table) {
	uint size = 0;
	for (uint i = 0; i < markerCount; ++i)
		size = MAX(size, markers[i].marker + 1);
	table.clear();
	table.resize(size, 0xffff);
	for (uint i = 0; i < markerCount; ++i)
		table[markers[i].marker] = markers[i].frame;
}

// Primitives.swift drawEllipse: the sietch water's ripple copies the pixel on
// the ellipse into a 4x4 block every fourth column, an approximation of the
// original's palette-shifting reflection.
void FloppyIntro::drawWaterRipple(Graphics::Surface &view, int cx, int cy, int rx, int ry) {
	auto plot = [&](int x, int y) {
		if (x < 0 || x >= view.w || y < 0 || y >= view.h)
			return;
		const byte colour = *(const byte *)view.getBasePtr(x, y);
		for (int j = 0; j < 4 && y + j < view.h; ++j)
			for (int i = 0; i < 4 && x + i < view.w; ++i)
				*(byte *)view.getBasePtr(x + i, y + j) = colour;
	};
	auto plotFour = [&](int x, int y) {
		if ((cx + x) % 4 != 0)
			return;
		plot(cx + x, cy + y);
		plot(cx - x, cy + y);
		plot(cx + x, cy - y);
		plot(cx - x, cy - y);
	};

	const int twoASquare = 2 * rx * rx, twoBSquare = 2 * ry * ry;
	int x = rx, y = 0;
	int xChange = ry * ry * (1 - 2 * rx), yChange = rx * rx, error = 0;
	int stoppingX = twoBSquare * rx, stoppingY = 0;
	while (stoppingX >= stoppingY) {
		plotFour(x, y);
		++y;
		stoppingY += twoASquare;
		error += yChange;
		yChange += twoASquare;
		if (2 * error + xChange > 0) {
			--x;
			stoppingX -= twoBSquare;
			error += xChange;
			xChange += twoBSquare;
		}
	}
	x = 0;
	y = ry;
	xChange = ry * ry;
	yChange = rx * rx * (1 - 2 * ry);
	error = 0;
	stoppingX = 0;
	stoppingY = twoASquare * ry;
	while (stoppingX <= stoppingY) {
		plotFour(x, y);
		++x;
		stoppingX += twoBSquare;
		error += xChange;
		xChange += twoBSquare;
		if (2 * error + yChange > 0) {
			--y;
			stoppingY -= twoASquare;
			error += yChange;
			yChange += twoASquare;
		}
	}
}

// DesertWalk.swift and Kiss.swift: scattered dunes at fractional scale with
// Arrakeen on the horizon. DUNES.HSQ carries no palette; the sky's colours
// (128-222) light it.
void FloppyIntro::drawDuneField(Sprite &dunes, Sprite &dunes2, Graphics::Surface &view, bool kissLayout) {
	if (!kissLayout) {
		drawScaled(dunes, 4, view, 0, 78, 20);
		drawScaled(dunes, 1, view, 34, 77, 10);
		drawScaled(dunes, 6, view, 143, 77, 20);
		drawScaled(dunes, 0, view, 260, 78, 15);
		drawScaled(dunes, 4, view, 243, 77, 20);
		drawScaled(dunes, 4, view, 83, 77, 10);
		drawScaled(dunes, 2, view, 62, 78, 15);
		dunes2.drawFrame(16, &view, 160, 12);
		dunes.drawFrame(0, &view, 210, 72);
		dunes.drawFrame(4, &view, 10, 76);
	} else {
		drawScaled(dunes, 0, view, 62, 77, 10);
		drawScaled(dunes, 6, view, 174, 77, 20);
		drawScaled(dunes, 7, view, 120, 77, 20);
		drawScaled(dunes, 3, view, 19, 77, 15);
		drawScaled(dunes, 1, view, 240, 77, 15);
		drawScaled(dunes, 2, view, 27, 78, 35);
		drawScaled(dunes, 16, view, 280, 56, 40);
		drawScaled(dunes, 1, view, 108, 74, 50);
		dunes.drawFrame(4, &view, 108, 77);
	}
}

// ---- scenes -----------------------------------------------------------------

// Sietch.swift. SIET.SAL rooms use SIET0 (0), SIET1 (1-12) and BOTA (13);
// the entrance (0) stands under the sky. Room 12 is the water cave.
void FloppyIntro::sietchScene(const char *dumpName, uint room, const MarkerCharacter *markers, uint markerCount,
		const char *foreground, uint32 duration, Transition in, uint32 inMillis, Transition out, uint32 outMillis) {
	Common::Array<byte> salData, sheetData, personData, foregroundData, dropData;
	const char *sheetName = room == 0 ? "SIET0.HSQ" : room == 13 ? "BOTA.HSQ" : "SIET1.HSQ";
	if (_aborted || _quit || !_resources.load("SIET.SAL", salData) || !_resources.load(sheetName, sheetData)
			|| !_resources.load("PERS.HSQ", personData))
		return;
	if (foreground && !_resources.load(foreground, foregroundData))
		foreground = nullptr;

	Sprite sheet(_system, sheetData), persons(_system, personData);
	Sprite *fg = foreground ? new Sprite(_system, foregroundData) : nullptr;
	Room rooms(salData);
	Common::Array<uint16> table;
	markerTable(markers, markerCount, table);

	Sound drop(_system);
	if (room == 12 && !isFastCapture() && _resources.load("SD4.HSQ", dropData))
		drop.playVOC(dropData);

	runScene(dumpName, duration, in, inMillis, out, outMillis, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		if (room == 0) {
			drawSkyView(view, kSkyNarrow, 320, kSkyDay);
			fillView(view, Common::Rect(0, 78, 320, 152), kDesertFloor);
		}
		sheet.setPalette();
		persons.setPalette();
		rooms.draw(room, sheet, view, table.empty() ? nullptr : &persons, table.empty() ? nullptr : &table);
		if (room == 12) {
			const uint32 p = MIN<uint32>(elapsed, 3000);
			drawWaterRipple(view, 175, 95, 15 + (int)(320 * p / 3000), 3 + (int)(22 * p / 3000));
		}
		if (fg) {
			fg->setPalette();
			if (fg->animationFrameCount(0))
				fg->drawAnimation(0, elapsed, &view);
		}
	});
	delete fg;
}

// Palace.swift. The exterior (BALCON frame 2) is the backdrop of the stairs
// (SAL room 11, markers only) and of the balcony (room 10); the balcony rail
// and the family come from the room record. A zoom shows part of it at full
// size with a character portrait in front.
void FloppyIntro::palaceScene(const char *dumpName, uint room, const MarkerCharacter *markers, uint markerCount,
		const Common::Rect *zoom, const char *foreground, uint32 duration, Transition out, uint32 outMillis) {
	Common::Array<byte> salData, sheetData, personData, foregroundData;
	if (_aborted || _quit || !_resources.load("PALACE.SAL", salData) || !_resources.load("BALCON.HSQ", sheetData)
			|| !_resources.load("PERS.HSQ", personData))
		return;
	if (foreground && !_resources.load(foreground, foregroundData))
		foreground = nullptr;

	Sprite sheet(_system, sheetData), persons(_system, personData);
	Sprite *fg = foreground ? new Sprite(_system, foregroundData) : nullptr;
	Room rooms(salData);
	Common::Array<uint16> table;
	markerTable(markers, markerCount, table);

	Graphics::ManagedSurface full;
	full.create(320, kViewHeight, Graphics::PixelFormat::createFormatCLUT8());

	runScene(dumpName, duration, kNoTransition, 0, out, outMillis, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		Graphics::Surface &scene = *full.surfacePtr();
		scene.fillRect(Common::Rect(0, 0, 320, kViewHeight), 0);
		if (room == 11)
			drawSkyView(scene, kSkyLarge, 200, kSkyDay);
		else
			drawSkyView(scene, kSkyNarrow, 320, kSkyDay);
		// BALCON's palette records touch the sky range; keep the sky's.
		byte skyPalette[256 * 3];
		_system->getPaletteManager()->grabPalette(skyPalette, 0, 256);
		sheet.setPalette();
		_system->getPaletteManager()->setPalette(skyPalette + 128 * 3, 128, 95);
		sheet.drawFrame(2, &scene, 0, 0);
		persons.setPalette();
		rooms.draw(room, sheet, scene, table.empty() ? nullptr : &persons, table.empty() ? nullptr : &table);

		if (zoom)
			zoomView(scene, view, *zoom);
		else
			copyView(scene, view);
		if (fg) {
			fg->setPalette();
			if (fg->animationFrameCount(0))
				fg->drawAnimation(0, elapsed, &view);
		}
	});
	full.free();
	delete fg;
}

// Background.swift + Character.swift: a character in front of one of the
// BACK.HSQ backdrops, with the Sardaukar sliding in from the left when asked.
void FloppyIntro::backdropCharacterScene(const char *dumpName, const char *backdrop, const char *characterName,
		const uint16 *animations, uint animationCount, bool sardaukar, uint32 duration, Transition in,
		uint32 inMillis, Transition out, uint32 outMillis, uint16 subtitle) {
	Common::Array<byte> backData, characterData;
	if (_aborted || _quit || !_resources.load("BACK.HSQ", backData) || !_resources.load(characterName, characterData))
		return;
	Sprite back(_system, backData), character(_system, characterData);

	CharacterPlayer player;
	player.sprite = &character;
	for (uint i = 0; i < animationCount; ++i)
		player.queue.push_back(animations[i]);
	player.idle = animationCount ? animations[animationCount - 1] : 0;
	player.current = -1;
	player.animationStart = player.animationDuration = 0;
	player.offsetX = player.offsetY = 0;
	if (!scumm_stricmp(characterName, "BARO.HSQ"))
		player.offsetX = 83;
	else if (!scumm_stricmp(characterName, "FEYD.HSQ"))
		player.offsetX = 66;

	const Common::String kind(backdrop);
	runScene(dumpName, duration, in, inMillis, out, outMillis, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		if (kind == "baron")
			drawBaronBackdrop(back, view);
		else if (kind == "feyd")
			drawFeydBackdrop(back, view);
		else
			drawRedBackdrop(back, view);
		if (sardaukar) {
			const int x = -150 + (int)(150 * progress256(elapsed, 2000, 200) / 256);
			back.drawFrame(5, &view, x, 13);
			back.drawFrame(6, &view, x, 13);
		}
		drawCharacter(player, view, elapsed);
		if (subtitle)
			drawSubtitle(subtitle);
	});
}

// Sunrise.swift in sunset mode: SUNRS palettes 3, 4 and 5 blend one per
// second while the Harkonnen fort (frame 7) stands in the desert.
void FloppyIntro::sunsetFortScene(const char *dumpName, uint32 duration) {
	Common::Array<byte> data;
	if (_aborted || _quit || !_resources.load("SUNRS.HSQ", data))
		return;
	Sprite background(_system, data);
	runScene(dumpName, duration, kDissolve, 300, kDissolve, 300, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		const uint step = MIN<uint32>(2, elapsed / 1000);
		if (step < 2) {
			const uint level = (elapsed % 1000) * 256 / 1000;
			background.setPaletteRecordBlend(8 + 3 + step + 1, 8 + 3 + step, level);
		} else {
			background.setPaletteRecord(8 + 5);
		}
		background.drawFrame(2, &view, 0, 0);
		background.drawFrame(3, &view, 0, 25);
		background.drawFrame(4, &view, 0, 50);
		background.drawFrame(5, &view, 0, 74);
		background.drawFrame(6, &view, 134, 92);
		background.drawFrame(0, &view, 0, 102);
		background.drawFrame(7, &view, 19, 74);
	});
}

// DesertWalk.swift: dunes under the sky with Arrakeen far away.
void FloppyIntro::desertWalkScene(const char *dumpName, uint skyPalette, uint32 duration, Transition in,
		uint32 inMillis, Transition out, uint32 outMillis, uint16 subtitle) {
	Common::Array<byte> dunesData, dunes2Data;
	if (_aborted || _quit || !_resources.load("DUNES.HSQ", dunesData) || !_resources.load("DUNES2.HSQ", dunes2Data))
		return;
	Sprite dunes(_system, dunesData), dunes2(_system, dunes2Data);
	runScene(dumpName, duration, in, inMillis, out, outMillis, duration / 2, [&](uint32 elapsed, Graphics::Surface &view) {
		(void)elapsed;
		drawSkyView(view, kSkyNarrow, 320, skyPalette);
		fillView(view, Common::Rect(0, 76, 320, 152), kDesertFloor);
		drawDuneField(dunes, dunes2, view, false);
		if (subtitle)
			drawSubtitle(subtitle);
	});
}

// Kiss.swift: Paul and Chani at night; after five seconds the picture zooms
// on them and the close-up sprite replaces the silhouettes.
void FloppyIntro::kissScene(const char *dumpName) {
	Common::Array<byte> kissData, dunesData, dunes2Data;
	if (_aborted || _quit || !_resources.load("CHANKISS.HSQ", kissData) || !_resources.load("DUNES.HSQ", dunesData)
			|| !_resources.load("DUNES2.HSQ", dunes2Data))
		return;
	Sprite kiss(_system, kissData), dunes(_system, dunesData), dunes2(_system, dunes2Data);
	Graphics::ManagedSurface scene;
	scene.create(320, kViewHeight, Graphics::PixelFormat::createFormatCLUT8());
	const Common::Rect zoom(0, 52, 210, 152);

	runScene(dumpName, 10000, kDissolve, 300, kFade, 2000, 3000, [&](uint32 elapsed, Graphics::Surface &view) {
		Graphics::Surface &composed = *scene.surfacePtr();
		composed.fillRect(Common::Rect(0, 0, 320, kViewHeight), 0);
		drawSkyView(composed, kSkyNarrow, 320, kSkyNight);
		fillView(composed, Common::Rect(0, 78, 320, 152), kDesertFloor);
		drawDuneField(dunes, dunes2, composed, true);
		kiss.setPalette();
		if (elapsed < 5000) {
			copyView(composed, view);
			kiss.drawFrame(0, &view, 78, 32);
		} else {
			zoomView(composed, view, zoom);
			kiss.drawFrame(1, &view, 25, 4);
		}
	});
	scene.free();
}

// Ornithopter.swift: at the sietch entrance by night the ornithopter beats
// its wings (frames 8-22), folds its feet (2-7) and lifts off up and left.
void FloppyIntro::ornithopterScene(const char *dumpName) {
	Common::Array<byte> salData, sheetData, ornyData;
	if (_aborted || _quit || !_resources.load("SIET.SAL", salData) || !_resources.load("SIET0.HSQ", sheetData)
			|| !_resources.load("ORNYTK.HSQ", ornyData))
		return;
	Sprite sheet(_system, sheetData), orny(_system, ornyData);
	Room rooms(salData);

	runScene(dumpName, 5000, kFade, 2000, kNoTransition, 0, 1500, [&](uint32 elapsed, Graphics::Surface &view) {
		drawSkyView(view, kSkyNarrow, 320, kSkyNight);
		fillView(view, Common::Rect(0, 78, 320, 152), kDesertFloor);
		sheet.setPalette();
		rooms.draw(0, sheet, view);

		// Take-off eases in over 2.0-3.0 s: slow at first, then fast.
		const uint32 t = progress256(elapsed, 2000, 1000);
		const uint32 eased = t * t / 256;
		const int x = 68 - (int)(100 * eased / 256), y = 56 - (int)(152 * eased / 256);
		const uint16 feet = 2 + (uint16)(5 * progress256(elapsed, 2000, 600) / 256);
		const uint16 wing = 8 + (uint16)(14 * progress256(elapsed, 200, 1800) / 256);
		orny.setPalette();
		orny.drawFrame(1, &view, x + 87, y + 33);
		orny.drawFrame(0, &view, x + 81, y + 3);
		orny.drawFrame(feet, &view, x + 85, y + 53);
		orny.drawFrame(wing, &view, x, y);
	});
}

// Flight.swift: terrain sprites stream from the vanishing point towards the
// viewer along thirteen rays, growing as they come.
void FloppyIntro::flightScene(const char *dumpName, uint32 duration) {
	Common::Array<byte> dunesData;
	if (_aborted || _quit || !_resources.load("DUNES.HSQ", dunesData))
		return;
	Sprite dunes(_system, dunesData);

	struct Terrain {
		uint16 sprite;
		int16 endX, endY;
		uint32 born;
	};
	const int originX = 160, originY = 70, radius = 300;
	Common::Array<Terrain> terrain;
	Common::RandomSource rng("dune-flight");
	// A fixed seed: the pattern is pseudo-random anyway, and the harness
	// checkpoint must not depend on the clock.
	rng.setSeed(0x44554e45);
	uint32 nextSpawn = 0;
	bool evenSet = true;

	runScene(dumpName, duration, kNoTransition, 0, kFade, 2000, 3500, [&](uint32 elapsed, Graphics::Surface &view) {
		drawSkyView(view, kSkyNarrow, 320, kSkyNight);
		fillView(view, Common::Rect(0, 78, 320, 152), kDesertFloor);

		while (elapsed >= nextSpawn) {
			// Five new pieces per second on alternating rays.
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
				Terrain piece;
				piece.sprite = (uint16)rng.getRandomNumber(7);
				piece.endX = (int16)(originX + radius * cos(angle));
				piece.endY = (int16)(originY + radius * sin(angle));
				piece.born = nextSpawn;
				terrain.push_back(piece);
			}
			nextSpawn += 1000;
		}

		for (uint i = 0; i < terrain.size();) {
			const Terrain &piece = terrain[i];
			// Cubic timing: progress = (t^3) / 2 over a two-second life.
			const double t = (elapsed - piece.born) / 1000.0;
			const double progress = MIN(1.0, t * t * t / 2.0);
			const int x = originX + (int)((piece.endX - originX) * progress);
			const int y = originY + (int)((piece.endY - originY) * progress);
			if (y >= 152 || progress >= 1.0) {
				terrain.remove_at(i);
				continue;
			}
			++i;
			if (y < 78)
				continue;
			const int scalePercent = (int)(120.0 * (y - 80) / 72.0);
			uint16 width, height;
			if (scalePercent <= 0 || !dunes.frameSize(piece.sprite, width, height))
				continue;
			drawScaled(dunes, piece.sprite, view, x - width * scalePercent / 100, y, (uint)scalePercent);
		}
	});
}

// Attack.swift / dune-rust attack: the night battle simulation.
void FloppyIntro::attackScene(const char *dumpName) {
	Common::Array<byte> attackData, soundData;
	if (_aborted || _quit || !_resources.load("ATTACK.HSQ", attackData))
		return;
	Sprite sheet(_system, attackData);
	sheet.setPalette();
	NightAttack attack(_system, sheet);
	Sound gunfire(_system);
	if (!isFastCapture() && _resources.load("SD3.HSQ", soundData))
		gunfire.playVOC(soundData);

	uint32 lastElapsed = 0;
	runScene(dumpName, 10000, kDissolve, 300, kDissolve, 300, 5000, [&](uint32 elapsed, Graphics::Surface &view) {
		if (isFastCapture()) {
			// One frame stands for the whole scene: simulate it in 15 ms steps.
			for (uint32 t = 0; t < elapsed; t += 15)
				attack.update(15);
		} else {
			attack.update(elapsed - lastElapsed);
		}
		lastElapsed = elapsed;
		sheet.setPalette();
		attack.applySkyPalette();
		attack.draw(view);
	});
}

// Credits.swift: the credit lines scroll up over a sunset while WORMSUIT
// plays. Sprite numbers and positions are the sheet's own order.
void FloppyIntro::creditsScene(const char *dumpName) {
	Common::Array<byte> creditsData, intdsData;
	if (_aborted || _quit || !_resources.load("CREDITS.HSQ", creditsData) || !_resources.load("INTDS.HSQ", intdsData))
		return;
	static const int16 lines[][3] = {
		{ 0, 134, 0 }, { 1, 77, 30 }, { 2, 126, 90 }, { 3, 56, 120 }, { 4, 86, 210 }, { 5, 68, 270 },
		{ 6, 104, 340 }, { 7, 166, 360 }, { 8, 166, 380 }, { 9, 75, 440 }, { 10, 166, 460 }, { 11, 48, 540 },
		{ 12, 64, 570 }, { 13, 124, 585 }, { 14, 86, 615 }, { 15, 88, 625 }, { 16, 96, 715 }, { 17, 166, 735 },
		{ 18, 166, 755 }, { 19, 67, 815 }, { 20, 166, 835 }, { 21, 65, 915 }, { 22, 58, 995 }, { 23, 75, 1035 },
		{ 24, 92, 1075 }, { 25, 166, 1095 }, { 26, 67, 1135 }, { 27, 55, 1175 }, { 28, 113, 1255 }, { 29, 166, 1275 },
		{ 30, 166, 1295 }, { 31, 166, 1315 }, { 32, 166, 1335 }, { 33, 66, 1375 }, { 34, 60, 1415 }, { 35, 108, 1505 },
		{ 36, 64, 1520 }, { 37, 120, 1535 }, { 38, 126, 1565 }, { 39, 59, 1580 }, { 40, 96, 1595 }, { 41, 114, 1685 },
		{ 42, 74, 1700 }, { 43, 6, 1790 }
	};
	Sprite credits(_system, creditsData), intds(_system, intdsData);
	startSong("WORMSUIT.HSQ");

	runScene(dumpName, 54000, kFade, 2000, kNoTransition, 0, 6000, [&](uint32 elapsed, Graphics::Surface &view) {
		drawSkyView(view, kSkyNarrow, 320, kSkyNight);
		credits.setPalette(); // Turns the sky red: CREDITS owns 128-190.
		intds.drawFrame(0, &view, 0, 60);
		const int scroll = (int)(1936 * progress256(elapsed, 2000, 52000) / 256);
		for (uint i = 0; i < ARRAYSIZE(lines); ++i)
			credits.drawFrame(lines[i][0], &view, lines[i][1], 152 - scroll + lines[i][2]);
	});
}

// ---- prologue ---------------------------------------------------------------

// Stars.swift, planets and globe modes. With the globe the star field shows
// frame 0 only and the planet turns 200 units per second from 5000.
void FloppyIntro::prologueStarsScene(const char *dumpName, bool globe, uint16 subtitle) {
	Common::Array<byte> starsData;
	if (_aborted || _quit || !_resources.load("STARS.HSQ", starsData))
		return;
	Sprite stars(_system, starsData);
	runScene(dumpName, 7000, kFade, 1000, kFade, 1000, 3000, [&](uint32 elapsed, Graphics::Surface &view) {
		stars.setPalette();
		stars.drawFrame(0, &view, 0, 0);
		if (globe) {
			const uint16 rotation = (uint16)(5000 + 200 * ((elapsed + 999) / 1000));
			drawGlobeSphere(_system, view, _resources, rotation, 40);
		} else {
			stars.drawFrame(1, &view, 302, 0);
			stars.drawFrame(2, &view, 604, 0);
			stars.drawFrame(36, &view, 45, 60);
			stars.drawFrame(3, &view, 124, 72);
			stars.drawFrame(37, &view, 235, 106);
		}
		drawSubtitle(subtitle);
	});
}

// Background.swift, desert mode: dunes under a day sky.
void FloppyIntro::prologueDesertScene(const char *dumpName, uint16 subtitle) {
	Common::Array<byte> intdsData;
	if (_aborted || _quit || !_resources.load("INTDS.HSQ", intdsData))
		return;
	Sprite intds(_system, intdsData);
	runScene(dumpName, 7000, kFade, 1000, kFade, 1000, 3000, [&](uint32 elapsed, Graphics::Surface &view) {
		(void)elapsed;
		drawSkyView(view, kSkyNarrow, 320, kSkyDay);
		intds.setPalette();
		intds.drawFrame(0, &view, 0, 60);
		drawSubtitle(subtitle);
	});
}

// Palace.swift, stairs at sunrise: the sky blends from night to sunrise
// between 2 and 5 seconds and the palace lights (colours 112-127) come up.
void FloppyIntro::prologuePalaceScene(const char *dumpName) {
	Common::Array<byte> salData, sheetData, skyData;
	if (_aborted || _quit || !_resources.load("PALACE.SAL", salData) || !_resources.load("BALCON.HSQ", sheetData)
			|| !_resources.load("SKY.HSQ", skyData))
		return;
	Sprite sheet(_system, sheetData), sky(_system, skyData);
	Room rooms(salData);
	runScene(dumpName, 6000, kFade, 1000, kFade, 1000, 4000, [&](uint32 elapsed, Graphics::Surface &view) {
		const uint level = progress256(elapsed, 2000, 3000);
		sky.setPaletteRecordBlend(8 + kSkySunrise, 8 + kSkyNight, level);
		for (int x = 0; x < 200; x += 40)
			for (uint tile = 0; tile < 4; ++tile)
				sky.drawFrame(4 + tile, &view, x, (int)tile * 30);
		byte skyPalette[256 * 3];
		_system->getPaletteManager()->grabPalette(skyPalette, 0, 256);
		sheet.setPalette();
		_system->getPaletteManager()->setPalette(skyPalette + 128 * 3, 128, 95);
		byte lights[16 * 3];
		_system->getPaletteManager()->grabPalette(lights, 112, 16);
		for (uint i = 0; i < sizeof(lights); ++i)
			lights[i] = (byte)(lights[i] * level / 256);
		_system->getPaletteManager()->setPalette(lights, 112, 16);
		sheet.drawFrame(2, &view, 0, 0);
		rooms.draw(11, sheet, view);
	});
}

// Prologue.swift: eight narrated cards, then the palace at dawn. The
// narration is COMMAND1 records prologueSentence(0..7).
void FloppyIntro::prologue() {
	static const uint16 baronAnimation[] = { 4 };
	setViewTop(0);
	startSong("WORMSUIT.HSQ");
	prologueStarsScene("prologue-stars", false, prologueSentence(0));
	prologueStarsScene("prologue-globe-1", true, prologueSentence(1));
	prologueDesertScene("prologue-desert", prologueSentence(2));
	backdropCharacterScene("prologue-paul-1", "red", "PAUL.HSQ", nullptr, 0, false, 7000, kFade, 1000, kFade, 1000,
			prologueSentence(3));
	backdropCharacterScene("prologue-baron", "baron", "BARO.HSQ", baronAnimation, 1, false, 7000, kFade, 1000, kFade, 1000,
			prologueSentence(4));
	prologueStarsScene("prologue-globe-2", true, prologueSentence(5));
	backdropCharacterScene("prologue-paul-2", "red", "PAUL.HSQ", nullptr, 0, false, 7000, kFade, 1000, kFade, 1000,
			prologueSentence(6));
	desertWalkScene("prologue-night", kSkyNight, 7000, kFade, 1000, kFade, 1000, prologueSentence(7));
	prologuePalaceScene("prologue-palace");
	setViewTop(kViewTop);
}

} // namespace Dune

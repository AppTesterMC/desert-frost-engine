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
#include "common/file.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/ustr.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "image/bmp.h"

#include "dune/debug.h"
#include "dune/harness.h"

namespace Dune {

StartupLog::StartupLog() : _file(nullptr) {
	// The null backend used by the native smoke test has no save path. The
	// iOS backend always installs its sandboxed save manager, even when
	// "savepath" is not set.
	if (g_system && g_system->getSavefileManager() &&
#if defined(IPHONE_IOS7)
			true
#else
			ConfMan.hasKey("savepath") && !ConfMan.get("savepath").empty()
#endif
			)
		_file = g_system->getSavefileManager()->openForSaving("dune-ios.log", false);
}

StartupLog::~StartupLog() {
	if (_file) {
		_file->finalize();
		delete _file;
	}
}

void StartupLog::line(const Common::String &message) {
	if (!_file)
		return;
	_file->writeString(message);
	_file->writeByte('\n');
	_file->flush(); // A crash must not lose the lines that explain it.
}

void showStatus(const char *message) {
	if (g_system)
		g_system->displayMessageOnOSD(Common::U32String(message));
}

bool isDumpRun() {
	return ConfMan.hasKey("dune_dump") && !ConfMan.get("dune_dump").empty();
}

uint dumpEveryMillis() {
	return ConfMan.hasKey("dune_dump_every") ? (uint)MAX(0, ConfMan.getInt("dune_dump_every")) : 0;
}

void dumpScreen(OSystem *system, const char *name) {
	if (!isDumpRun() && !isDuneHarnessRun())
		return;
	if (isDuneHarnessRun())
		captureDuneCheckpoint(system, name);
	if (!isDumpRun())
		return;

	byte palette[256 * 3];
	system->getPaletteManager()->grabPalette(palette, 0, 256);
	Graphics::Surface *screen = system->lockScreen();
	if (!screen)
		return;
	Graphics::Surface *rgb = screen->convertTo(Graphics::PixelFormat(3, 8, 8, 8, 0, 16, 8, 0, 0), palette);
	system->unlockScreen();

	Common::DumpFile file;
	if (file.open(Common::Path(ConfMan.get("dune_dump")).appendComponent(Common::String(name) + ".bmp")))
		Image::writeBMP(file, *rgb);
	rgb->free();
	delete rgb;
}

#if defined(DUNE_DEBUG)

namespace {

struct DebugState {
	OSystem *os;
	Common::OutSaveFile *file;
	int room;
	Common::String scene;
	int spriteX, spriteY;
	Common::String audio;
	int cursorX, cursorY;
	uint32 lastLogTime;
	Common::String lastLine;

	DebugState() : os(nullptr), file(nullptr), room(-1), spriteX(0), spriteY(0), cursorX(0), cursorY(0),
			lastLogTime(0) {
	}
};

DebugState g_debug;

Common::String stateLine() {
	return Common::String::format("room=%s scene=%s sprite=(%d,%d) audio=%s cursor=(%d,%d)",
			g_debug.room < 0 ? "INTRO" : Common::String::format("%d", g_debug.room).c_str(),
			g_debug.scene.empty() ? "(none)" : g_debug.scene.c_str(),
			g_debug.spriteX, g_debug.spriteY,
			g_debug.audio.empty() ? "(none)" : g_debug.audio.c_str(),
			g_debug.cursorX, g_debug.cursorY);
}

void writeState(bool force) {
	if (!g_debug.file || !g_debug.os)
		return;
	const Common::String line = stateLine();
	const uint32 now = g_debug.os->getMillis();
	if (!force && line == g_debug.lastLine && now - g_debug.lastLogTime < 250)
		return;
	g_debug.file->writeString(line);
	g_debug.file->writeByte('\n');
	g_debug.file->flush();
	g_debug.lastLine = line;
	g_debug.lastLogTime = now;
}

} // namespace

#endif

void debugBegin(OSystem *system) {
#if defined(DUNE_DEBUG)
	g_debug.os = system;
	g_debug.room = -1;
	g_debug.scene = "startup";
	g_debug.spriteX = g_debug.spriteY = 0;
	g_debug.audio = "mixer starting";
	g_debug.cursorX = g_debug.cursorY = 0;
	g_debug.lastLine.clear();
	g_debug.lastLogTime = 0;
	if (system && system->getSavefileManager())
		g_debug.file = system->getSavefileManager()->openForSaving("dune_debug.log", false);
	if (g_debug.file) {
		g_debug.file->writeString("DUNE_DEBUG overlay enabled\n");
		g_debug.file->flush();
	}
	writeState(true);
#else
	(void)system;
#endif
}

void debugEnd() {
#if defined(DUNE_DEBUG)
	writeState(true);
	if (g_debug.file) {
		g_debug.file->finalize();
		delete g_debug.file;
		g_debug.file = nullptr;
	}
	g_debug.os = nullptr;
#endif
}

void debugSetRoom(int room) {
#if defined(DUNE_DEBUG)
	g_debug.room = room;
#else
	(void)room;
#endif
}

void debugSetScene(const Common::String &scene) {
#if defined(DUNE_DEBUG)
	g_debug.scene = scene;
#else
	(void)scene;
#endif
}

void debugSetSpriteOrigin(int x, int y) {
#if defined(DUNE_DEBUG)
	g_debug.spriteX = x;
	g_debug.spriteY = y;
#else
	(void)x;
	(void)y;
#endif
}

void debugSetAudioStream(const Common::String &state) {
#if defined(DUNE_DEBUG)
	g_debug.audio = state;
#else
	(void)state;
#endif
}

void debugSetCursor(int x, int y) {
#if defined(DUNE_DEBUG)
	g_debug.cursorX = x;
	g_debug.cursorY = y;
#else
	(void)x;
	(void)y;
#endif
}

void debugOverlay(Graphics::Surface &surface) {
#if defined(DUNE_DEBUG)
	if (!g_debug.os || surface.format.bytesPerPixel != 1)
		return;

	const Common::Point cursor = g_debug.os->getEventManager()->getMousePos();
	debugSetCursor(cursor.x, cursor.y);
	const byte colours[6] = { 0, 0, 0, 252, 252, 252 };
	g_debug.os->getPaletteManager()->setPalette(colours, 254, 2);
	surface.fillRect(Common::Rect(0, 0, surface.w, 82), 254);
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (font) {
		// Keep each diagnostic field inside the 320-pixel framebuffer. In
		// particular, sprite origin must never trail the scene name on a clipped
		// first line: it gets the second visible row directly below DUNE_DEBUG.
		const Common::String header = Common::String::format("DUNE_DEBUG room=%s",
				g_debug.room < 0 ? "INTRO" : Common::String::format("%d", g_debug.room).c_str());
		const Common::String scene = Common::String::format("scene=%s",
				g_debug.scene.empty() ? "(none)" : g_debug.scene.c_str());
		const Common::String sprite = Common::String::format("sprite=(%d,%d)",
				g_debug.spriteX, g_debug.spriteY);
		const Common::String audio = Common::String::format("audio=%s",
				g_debug.audio.empty() ? "(none)" : g_debug.audio.c_str());
		const Common::String cursor = Common::String::format("cursor=(%d,%d)",
				g_debug.cursorX, g_debug.cursorY);
		font->drawString(&surface, header, 2, 1, surface.w - 4, 255, Graphics::kTextAlignLeft);
		font->drawString(&surface, sprite, 2, 17, surface.w - 4, 255, Graphics::kTextAlignLeft);
		font->drawString(&surface, scene, 2, 33, surface.w - 4, 255, Graphics::kTextAlignLeft);
		font->drawString(&surface, audio, 2, 49, surface.w - 4, 255, Graphics::kTextAlignLeft);
		font->drawString(&surface, cursor, 2, 65, surface.w - 4, 255, Graphics::kTextAlignLeft);
	}
	writeState(false);
#else
	(void)surface;
#endif
}

} // namespace Dune

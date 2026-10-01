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

// The worm call and ride (FINDINGS.md, "The worm call and ride"). CALL A WORM
// (CD seg000:42d1, floppy 4b08) sets the worm travel mode up (CD 4285, floppy
// 4ae7) and opens the destination map without the cockpit; a pick plays the
// departure (CD 47a0: VER.HNM with SN8.VOC; floppy 4faf: the SHAI/SHAI2 worm
// over the desert with SD8) and the ride shows the worm's back (VER.HSQ,
// drawn by the VER.BIN frame script: CD 4aeb/4d6c/4da0, floppy 5744/577a)
// over the landscape (floppy: the flight landscape seen from 0x1e, moving
// every second frame; CD: the DFL2.HNM clip).

#include "common/config-manager.h"
#include "common/events.h"
#include "common/system.h"
#include "graphics/paletteman.h"

#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/hnm.h"
#include "dune/resource.h"
#include "dune/scene.h"
#include "dune/sky.h"
#include "dune/sound.h"
#include "dune/sprite.h"
#include "dune/world.h"

namespace Dune {

namespace {

bool wormFastCapture() {
	return (isDumpRun() && !dumpEveryMillis()) || isDuneFastHarness() ||
		   (ConfMan.hasKey("dune_speedrun") && !ConfMan.hasKey("dune_speedrun_watch"));
}

} // namespace

bool GameScreen::wormAnimLoad() {
	// The VER.BIN blob (CD resource 0xbe, loaded over cs:015f by 4285; the
	// floppy's VERBIN.HSQ, loaded to cs:44d9 at start-up, 0f20): the view
	// rect, the pointer to the frame script, the table of sprite lists.
	if (_wormAnim.loaded)
		return true;
	Common::Array<byte> data;
	if (!_resources.load("VER.BIN", data) && !_resources.load("VERBIN.HSQ", data))
		return false;
	if (data.size() < 12)
		return false;
	auto w = [&](uint o) -> int { return o + 1 < data.size() ? (int)READ_LE_UINT16(data.data() + o) : -1; };
	_wormAnim.rect = Common::Rect((int16)w(0), (int16)w(2), (int16)w(4), (int16)w(6));
	const int scriptPointer = 8 + w(8);
	const int scriptStart = scriptPointer + w(scriptPointer);
	if (scriptStart < 0 || (uint)scriptStart >= data.size())
		return false;
	_wormAnim.script.clear();
	for (uint i = (uint)scriptStart; i < data.size(); ++i) {
		_wormAnim.script.push_back(data[i]);
		if (data[i] == 0xff)
			break;
	}
	_wormAnim.lists.clear();
	const int first = w(10);
	for (int k = 0; first > 0 && k < first / 2; ++k) {
		uint o = (uint)(10 + w(10 + 2 * k));
		Common::Array<WormPiece> list;
		while (o + 2 < data.size() && data[o]) {
			WormPiece p;
			p.sprite = (uint16)(data[o] - 1);
			p.dx = data[o + 1];
			p.dy = data[o + 2];
			list.push_back(p);
			o += 3;
		}
		_wormAnim.lists.push_back(list);
	}
	_wormAnim.cursor = 0;
	_wormAnim.loaded = true;
	_log.line(Common::String::format("Worm: VER.BIN: view %d,%d-%d,%d, %u lists, a script of %u bytes",
			_wormAnim.rect.left, _wormAnim.rect.top, _wormAnim.rect.right, _wormAnim.rect.bottom,
			_wormAnim.lists.size(), _wormAnim.script.size()));
	return true;
}

void GameScreen::wormRideSetup() {
	// worm_ride_setup (CD 4285, floppy 4ae7): the travel mode (CD ds:11c9 =
	// 8, floppy ds:11d6 = 8; the CD's vehicle 1 = DFL2.HNM), no cockpit, the
	// script from its start, VER.HSQ open with its palette.
	wormAnimLoad();
	_wormAnim.cursor = 0;
	_riding = true;
}

void GameScreen::wormAnimFrame(Graphics::Surface &view, bool advance) {
	// worm_anim_step (CD 4d6c, floppy 5744): 0xff restarts the script; each
	// byte is a sprite list (1: an escape, the next byte + 0x100) until a 0
	// ends the frame. worm_anim_draw_list (CD 4da0, floppy 577a): each
	// (sprite, dx, dy) at the view rect's corner + (dx, dy), clipped to it.
	if (!_wormAnim.loaded || _wormAnim.script.empty())
		return;
	Common::Array<byte> sheetData;
	if (!_resources.load("VER.HSQ", sheetData))
		return;
	Sprite sheet(_system, sheetData);
	sheet.setPalette();
	Common::Rect clip = _wormAnim.rect;
	clip.clip(Common::Rect(0, 0, view.w, view.h));
	if (clip.isEmpty())
		return;
	Graphics::Surface area = view.getSubArea(clip);
	uint cursor = _wormAnim.cursor;
	if (cursor >= _wormAnim.script.size() || _wormAnim.script[cursor] == 0xff)
		cursor = 0;
	while (cursor < _wormAnim.script.size()) {
		const byte b = _wormAnim.script[cursor++];
		if (b == 0 || b == 0xff)
			break;
		uint list = b;
		if (b == 1 && cursor < _wormAnim.script.size())
			list = 0x100 | _wormAnim.script[cursor++];
		if (list < 2 || list - 2 >= _wormAnim.lists.size())
			continue;
		const Common::Array<WormPiece> &pieces = _wormAnim.lists[list - 2];
		for (uint i = 0; i < pieces.size(); ++i)
			sheet.drawFrame(pieces[i].sprite, &area, _wormAnim.rect.left + pieces[i].dx - clip.left,
					_wormAnim.rect.top + pieces[i].dy - clip.top);
	}
	if (advance)
		_wormAnim.cursor = cursor;
}

void GameScreen::wormAnimAdvance() {
	// The script cursor past the current frame (the lists up to its 0).
	if (!_wormAnim.loaded || _wormAnim.script.empty())
		return;
	uint cursor = _wormAnim.cursor;
	if (cursor >= _wormAnim.script.size() || _wormAnim.script[cursor] == 0xff)
		cursor = 0;
	while (cursor < _wormAnim.script.size()) {
		const byte b = _wormAnim.script[cursor++];
		if (b == 0 || b == 0xff)
			break;
		if (b == 1)
			++cursor;
	}
	_wormAnim.cursor = cursor;
}

void GameScreen::dottedColumnsPresent(const byte *pixels, const byte *palette) {
	// transition_dotted_columns (vga effect 0x10, segvga 2dc3): the old
	// picture of the 152-row view goes to black through a 4x4 lattice of
	// dots (16 steps, one per offset), the new palette is set, and the new
	// picture comes in through the same lattice; a step every 3 ticks.
	static const uint16 kOffsets[16] = { 0x0141, 0x03c0, 0x0283, 0x0002, 0x0140, 0x03c2, 0x0000, 0x0281,
										 0x0003, 0x03c1, 0x0142, 0x03c3, 0x0282, 0x0001, 0x0143, 0x0280 };
	Common::Array<byte> screen(320 * 152);
	for (int y = 0; y < 152; ++y)
		memcpy(screen.data() + y * 320, _surface.getBasePtr(0, y), 320);
	const uint32 start = _system->getMillis();
	uint steps = 0;
	for (uint pass = 0; pass < 2 && !_quitRequested; ++pass) {
		if (pass == 1)
			_system->getPaletteManager()->setPalette(palette, 0, 256);
		for (uint k = 0; k < 16; ++k) {
			for (uint group = 0; group < 152 / 4; ++group)
				for (uint column = 0; column < 0x50; ++column) {
					const uint at = kOffsets[k] + group * 0x500 + column * 4;
					if (at < screen.size())
						screen[at] = pass ? pixels[at] : 0;
				}
			_system->copyRectToScreen(screen.data(), 320, 0, 0, 320, 152);
			_system->updateScreen();
			const uint32 due = start + ++steps * kSpiralStepMillis, now = _system->getMillis();
			if ((int32)(due - now) > 0)
				waitPumping(due - now);
		}
	}
	for (int y = 0; y < 152; ++y)
		memcpy(_surface.getBasePtr(0, y), pixels + y * 320, 320);
}

void GameScreen::wormDeparture() {
	// play_travel_departure_transition's worm branch (CD 47a0, floppy 4faf):
	// the subtitle and head go, phase 0x50 (the caller), then the worm comes.
	if (wormFastCapture() || _quitRequested)
		return;
	_log.line("Worm: the call (departure)");
	debugSetScene("travel/worm-call");
	Common::Array<byte> soundData;
	Sound call(_system);
	if (_world.floppy()) {
		// 50c8/5122: SHAI.HSQ (with SHAI2's frames) over the sky of the hour,
		// the desert (sprite 0x2c at 0,0x4a), then 0x2c frames of the list in
		// SHAI's sprite 0x2d, 0x19 ticks each; SD8 (voc 8) from the start.
		Common::Array<byte> wormData, wormExtension;
		if (!_resources.load("SHAI.HSQ", wormData) || !_resources.load("SHAI2.HSQ", wormExtension))
			return;
		Sprite worm(_system, wormData), wormFrames(_system, wormExtension);
		worm.setShaiAnimationFormat();
		worm.mergeFrames(wormFrames);
		const uint frames = MIN<uint>(42, worm.animationFrameCount(0));
		if (_resources.load("SD8.HSQ", soundData))
			call.playVOC(soundData);
		const uint32 kFrameMillis = 125; // 0x19 ticks of the 200 Hz timer
		const uint32 start = _system->getMillis();
		for (uint frame = 0; frame < 0x2c && !_quitRequested; ++frame) {
			Graphics::Surface view = _surface.surfacePtr()->getSubArea(Common::Rect(0, 0, 320, 152));
			_panel.applyPalette();
			drawSky(_system, _resources, view, kSkyNarrow, 320, skyPalette(), true);
			worm.setPalette();
			setSkyPalette(false); // 3b13: the hour's light over it
			worm.drawFrame(44, &view, 0, 74);
			if (frame < frames)
				worm.drawAnimationFrame(0, (uint16)frame, &view, 0, 0);
			const byte black[3] = { 0, 0, 0 };
			_system->getPaletteManager()->setPalette(black, 0, 1);
			_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 152);
			_system->updateScreen();
			if (frame == 20)
				dumpScreen(_system, "worm-call");
			const uint32 due = start + (frame + 1) * kFrameMillis, now = _system->getMillis();
			if ((int32)(due - now) > 0)
				waitPumping(due - now);
		}
		return;
	}
	// CD 47b5-47ca: VER.HNM's first frame wiped in (transition 0x10 through
	// 4913: the frame, then set_sky_palette), then the clip to its end
	// (play_worm_ride_clip 491c) with SN8.VOC looping until frame 0x0b.
	Common::Array<byte> video;
	if (!_resources.load("VER.HNM", video))
		return;
	{
		HnmPlayer first(_system);
		if (first.begin(video) && first.step()) {
			// The clip's colours, then set_sky_palette over them; the old
			// picture keeps the old palette until the lattice is black.
			byte old[256 * 3], palette[256 * 3];
			_system->getPaletteManager()->grabPalette(old, 0, 256);
			memcpy(palette, old, sizeof(palette));
			first.mergePalette(palette);
			_system->getPaletteManager()->setPalette(palette, 0, 256);
			setVideoSkyPalette();
			_panel.applyPalette();
			_system->getPaletteManager()->grabPalette(palette, 0, 256);
			_system->getPaletteManager()->setPalette(old, 0, 256);
			dottedColumnsPresent(first.screen(), palette);
		}
	}
	if (_resources.load("SN8.VOC", soundData))
		call.playVOC(soundData);
	// play_worm_ride_clip (491c): the clip into the 152-row view (the panel
	// stays as it was), a frame each 0x19 ticks (125 ms: the original's 54
	// frames take about 7 s in captures/worm-ride-cd); a click skips it
	// (hnm_do_frame_skippable).
	HnmPlayer player(_system);
	uint frames = 0;
	if (player.begin(video)) {
		const uint32 start = _system->getMillis();
		bool skipped = false;
		while (!_quitRequested && !skipped && player.step()) {
			byte palette[256 * 3];
			_system->getPaletteManager()->grabPalette(palette, 0, 256);
			player.mergePalette(palette);
			_system->getPaletteManager()->setPalette(palette, 0, 256);
			setVideoSkyPalette();
			_panel.applyPalette();
			for (int y = 0; y < 152; ++y)
				memcpy(_surface.getBasePtr(0, y), player.screen() + 320 * y, 320);
			_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 152);
			_system->updateScreen();
			if (++frames == 20)
				dumpScreen(_system, "worm-call");
			const uint32 due = start + frames * 125;
			while (!_quitRequested && (int32)(due - _system->getMillis()) > 0) {
				Common::Event event;
				while (pollDuneEvent(_system, event)) {
					if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
						_quitRequested = true;
					else if (event.type == Common::EVENT_LBUTTONDOWN || event.type == Common::EVENT_KEYDOWN)
						skipped = true;
				}
				_system->delayMillis(5);
			}
		}
	}
	_log.line(Common::String::format("Worm: VER.HNM, %u frames", frames));
	_panel.applyPalette();
}

bool GameScreen::startCdWormView() {
	// The CD's worm vehicle (ds:487e = 1): DFL2.HNM, looped whatever the
	// terrain (travel_select_flight_video 4ec6: a vehicle below 2 plays its
	// own clip).
	if (_dflData.empty() && !_resources.load("DFL2.HNM", _dflData))
		return false;
	delete _flightVideo;
	_flightVideo = new HnmPlayer(_system);
	_mntClip = 0;
	_mntNextFrame = 0;
	_log.line(Common::String::format("Flight view: the worm, DFL2 at %u ms", _system->getMillis()));
	return _flightVideo->begin(_dflData);
}

} // End of namespace Dune

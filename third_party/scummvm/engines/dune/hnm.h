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

#ifndef ENGINES_DUNE_HNM_H
#define ENGINES_DUNE_HNM_H

#include "common/array.h"
#include "common/scummsys.h"

class OSystem;

namespace Dune {

/**
 * Blocking player for the first-generation Cryo HNM videos used by Dune.
 *
 * A file is a sequence of 16-bit-length chunks. The first holds the initial
 * palette; every other chunk holds optional "sd" (sound) and "pl" (palette)
 * blocks followed by one frame. The sound blocks concatenate to a Creative
 * VOC file, which also paces playback.
 *
 * The chunk and tag layout comes from "Cryo HNM 1 Dumper v.0.2 by VAG, ARR.
 * vagsoft@mail.ru" (console version by Rymoah); the frame header bits were
 * worked out against the game data. See CREDITS.md.
 *
 * Frame header: bits 0-8 width, bit 9 HSQ-packed, bit 10 full frame without
 * an x/y prefix, bit 15 per-line RLE; then height and a mode byte (0xFF
 * keeps the previous frame where the new one has colour 0).
 */
class HnmPlayer {
public:
	enum Result {
		kFinished,
		kSkipped,
		kQuit,
		kError
	};

	explicit HnmPlayer(OSystem *system);

	/**
	 * Play a complete video. A click, tap, Return, Space or Escape skips it.
	 * With dumpFrame >= 0 the video runs untimed and silent up to that frame,
	 * which is written through dumpScreen() under the given name.
	 */
	Result play(const Common::Array<byte> &data, const char *name, int dumpFrame = -1);
	/** The last play() was skipped with Escape (the original's ESC ends the whole intro). */
	bool skippedWithEscape() const { return _escape; }

	/**
	 * Decode a whole video offscreen and keep its last picture (320x200
	 * bytes) and palette (768 bytes): the CD leaves the arrival videos'
	 * final view behind the exterior rooms.
	 */
	bool lastFrame(const Common::Array<byte> &data, byte *pixels, byte *palette);

	/**
	 * Called on every frame of play() before it reaches the screen, with the
	 * frame number and the 320x200 picture (the Irulan subtitles are drawn
	 * this way, play_IRULx_HSQ seg000:cf1b).
	 */
	/** Show the picture @p rows lower (the intro's flyovers sit 24 rows down, seg000:06f3). */
	void setTop(uint rows) { _top = rows; }
	typedef void (*FrameHook)(void *context, uint frame, byte *screen);
	void setFrameHook(FrameHook hook, void *context) {
		_hook = hook;
		_hookContext = context;
	}

	/**
	 * Step through a video one frame at a time, silently (the CD's flight
	 * views, MNT1-4, and the approach clips). begin() takes the header;
	 * step() decodes the next frame and answers false at the end.
	 */
	bool begin(const Common::Array<byte> &data);
	bool step();
	const byte *screen() const { return _screen.data(); }
	const byte *palette() const { return _palette; }
	uint frameNumber() const { return _streamFrame; }

private:
	bool _escape = false;
	bool readPalette(const byte *data, uint32 size, uint32 &position);
	bool decodeFrame(const byte *block, uint32 size);
	void present();

	OSystem *_system;
	byte _palette[256 * 3];
	bool _paletteDirty;
	Common::Array<byte> _screen;
	Common::Array<byte> _unpacked;
	uint _scale;
	uint _top = 0;
	FrameHook _hook = nullptr;
	void *_hookContext = nullptr;
	const Common::Array<byte> *_stream = nullptr;
	uint32 _streamOffset = 0;
	uint _streamFrame = 0;
};

} // namespace Dune

#endif // ENGINES_DUNE_HNM_H

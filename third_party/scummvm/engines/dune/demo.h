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

#ifndef ENGINES_DUNE_DEMO_H
#define ENGINES_DUNE_DEMO_H

#include "common/array.h"
#include "common/error.h"
#include "common/events.h"
#include "common/file.h"
#include "common/str.h"

class OSystem;
namespace Dune {
class StartupLog;

// One session owns the file and temporary config overrides. Engine recordings
// are versioned; the original DOS floppy's headerless demo is a different format.
class DemoSession {
public:
	DemoSession(OSystem *system, StartupLog &log);
	~DemoSession();
	Common::Error begin(uint release);
	bool playback() const { return _playing; }
	bool poll(Common::Event &event);
	void record(const Common::Event &event);
	void gameplay();
private:
	struct Frame { uint32 at; uint16 stage; Common::Event event; bool end; };
	struct Setting { bool present; Common::String value; };
	void writeFrame(const Common::Event &event, bool end);
	bool readFrame(Common::File &file, Frame &frame);
	uint32 elapsed();
	OSystem *_system;
	StartupLog &_log;
	Common::DumpFile _output;
	Common::Array<Frame> _frames;
	Common::Array<Setting> _saved;
	uint _next = 0, _count = 0;
	uint16 _stage = 1;
	uint32 _start = 0;
	bool _clockStarted = false, _playing = false, _recording = false, _ended = false;
};

bool duneDemoActive();
void duneDemoGameplay();
bool duneDemoPlayback();
bool pollDuneDemo(Common::Event &event);
void recordDuneDemo(const Common::Event &event);
} // namespace Dune
#endif

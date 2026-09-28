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
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/system.h"
#include "common/tokenizer.h"

#include <stdlib.h>

#include "graphics/cursorman.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "image/png.h"

#include "dune/debug.h"
#include "dune/harness.h"

namespace Dune {

namespace {

enum StepType {
	kStepMove,
	kStepClick,
	kStepKey,
	kStepQuit,
	kStepCheckpoint,
	kStepPeriods
};

struct Step {
	uint32 at;
	StepType type;
	int x, y;
	Common::MouseButton button;
	Common::KeyCode key;
	Common::String name;
	uint line; ///< the script line, for the log
	Common::String text;
};

struct HarnessState {
	bool loaded;
	bool warned;
	uint32 start;
	uint next;
	uint periods;
	Common::Array<Step> steps;
	Common::String output;

	StartupLog *log;

	HarnessState() : loaded(false), warned(false), start(0), next(0), periods(0), log(nullptr) {
	}
};

HarnessState g_harness;

bool checkpointing() {
	if (g_harness.output.empty() && ConfMan.hasKey("dune_checkpoint_dir"))
		g_harness.output = ConfMan.get("dune_checkpoint_dir");
	return !g_harness.output.empty();
}

bool parseInteger(const Common::String &text, int &value) {
	char *end = nullptr;
	const long parsed = strtol(text.c_str(), &end, 10);
	if (!end || *end != '\0')
		return false;
	value = (int)parsed;
	return true;
}

Common::KeyCode parseKey(const Common::String &token) {
	if (token == "ESC" || token == "ESCAPE")
		return Common::KEYCODE_ESCAPE;
	if (token == "SPACE")
		return Common::KEYCODE_SPACE;
	if (token == "RETURN" || token == "ENTER")
		return Common::KEYCODE_RETURN;
	if (token == "UP")
		return Common::KEYCODE_UP;
	if (token == "DOWN")
		return Common::KEYCODE_DOWN;
	if (token == "LEFT")
		return Common::KEYCODE_LEFT;
	if (token == "RIGHT")
		return Common::KEYCODE_RIGHT;
	int numeric;
	if (parseInteger(token, numeric))
		return (Common::KeyCode)numeric;
	if (token.size() == 1) {
		const char c = token[0] >= 'A' && token[0] <= 'Z' ? token[0] + ('a' - 'A') : token[0];
		if (c >= 'a' && c <= 'z')
			return (Common::KeyCode)c;
		if (c >= '0' && c <= '9')
			return (Common::KeyCode)c;
	}
	return Common::KEYCODE_INVALID;
}

bool parseButton(const Common::String &token, Common::MouseButton &button) {
	if (token == "left" || token == "LEFT") {
		button = Common::MOUSE_BUTTON_LEFT;
		return true;
	}
	if (token == "right" || token == "RIGHT") {
		button = Common::MOUSE_BUTTON_RIGHT;
		return true;
	}
	if (token == "middle" || token == "MIDDLE") {
		button = Common::MOUSE_BUTTON_MIDDLE;
		return true;
	}
	return false;
}

void warnBadLine(const Common::String &line) {
	if (!g_harness.warned)
		warning("Dune harness: ignoring malformed input line '%s'", line.c_str());
	g_harness.warned = true;
}

bool loadScript() {
	if (g_harness.loaded)
		return !g_harness.steps.empty();
	g_harness.loaded = true;
	if (!ConfMan.hasKey("dune_input") || ConfMan.get("dune_input").empty())
		return false;

	g_harness.output = ConfMan.hasKey("dune_checkpoint_dir") ? ConfMan.get("dune_checkpoint_dir") : "";
	Common::File input;
	// SearchMan intentionally does not resolve arbitrary host absolute paths.
	// The desktop harness supplies its script outside the game data tree, so
	// open it as an FSNode; this also keeps the same config usable by the SDL
	// and iOS builds without mounting the test directory into SearchMan.
	if (!input.open(Common::FSNode(Common::Path(ConfMan.get("dune_input"))))) {
		warning("Dune harness: unable to open %s", ConfMan.get("dune_input").c_str());
		return false;
	}

	uint32 logicalTime = 0;
	uint lineNumber = 0;
	while (!input.eos()) {
		Common::String line = input.readLine();
		++lineNumber;
		// A '#' after whitespace starts a comment to the end of the line
		// ("click left 160 163   # SEE DUNE MAP"); a line starting with '#'
		// is a comment too.
		for (uint i = 1; i < line.size(); ++i) {
			if (line[i] == '#' && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
				line = Common::String(line.c_str(), i);
				break;
			}
		}
		Common::StringTokenizer tokenizer(line);
		Common::Array<Common::String> tokens = tokenizer.split();
		if (tokens.empty() || tokens[0].empty() || tokens[0][0] == '#')
			continue;

		const Common::String command = tokens[0];
		if (command == "wait") {
			int millis;
			if (tokens.size() != 2 || !parseInteger(tokens[1], millis) || millis < 0)
				warnBadLine(line);
			else
				logicalTime += (uint32)millis;
			continue;
		}

		Step step;
		step.at = logicalTime;
		step.line = lineNumber;
		step.text = line;
		step.text.trim();
		step.x = step.y = 0;
		step.button = Common::MOUSE_BUTTON_LEFT;
		step.key = Common::KEYCODE_INVALID;
		if (command == "move" && tokens.size() == 3 && parseInteger(tokens[1], step.x) && parseInteger(tokens[2], step.y)) {
			step.type = kStepMove;
		} else if (command == "click" && tokens.size() == 4 && parseButton(tokens[1], step.button)
				&& parseInteger(tokens[2], step.x) && parseInteger(tokens[3], step.y)) {
			step.type = kStepClick;
		} else if (command == "key" && tokens.size() == 2 && (step.key = parseKey(tokens[1])) != Common::KEYCODE_INVALID) {
			step.type = kStepKey;
		} else if (command == "quit" && tokens.size() == 1) {
			step.type = kStepQuit;
		} else if (command == "periods" && tokens.size() == 2 && parseInteger(tokens[1], step.x) && step.x > 0) {
			step.type = kStepPeriods;
		} else if (command == "checkpoint" && tokens.size() == 2) {
			step.type = kStepCheckpoint;
			step.name = tokens[1];
		} else {
			warnBadLine(line);
			continue;
		}
		g_harness.steps.push_back(step);
	}
	return !g_harness.steps.empty();
}

void writeCheckpoint(OSystem *system, const Common::String &name) {
	if (!system || !checkpointing())
		return;

	byte palette[256 * 3];
	system->getPaletteManager()->grabPalette(palette, 0, 256);
	Graphics::Surface *screen = system->lockScreen();
	if (!screen)
		return;
	Graphics::Surface *rgb = screen->convertTo(Graphics::PixelFormat(3, 8, 8, 8, 0, 16, 8, 0, 0), palette);
	system->unlockScreen();

	const Common::Path path = Common::Path(g_harness.output).appendComponent(name + ".png");
	Common::DumpFile image;
	if (rgb && image.open(path, true))
		Image::writePNG(image, *rgb);
	if (rgb) {
		rgb->free();
		delete rgb;
	}

	Common::DumpFile metadata;
	if (metadata.open(Common::Path(g_harness.output).appendComponent(name + ".json"), true)) {
		const Common::Point cursor = system->getEventManager()->getMousePos();
		metadata.writeString(Common::String::format(
			"{\"cursor_visible\":%s,\"cursor_x\":%d,\"cursor_y\":%d}\n",
			CursorMan.isVisible() ? "true" : "false", cursor.x, cursor.y));
	}
}

bool makeEvent(const Step &step, Common::Event &event) {
	event = Common::Event();
	switch (step.type) {
	case kStepMove:
		event.type = Common::EVENT_MOUSEMOVE;
		event.mouse = Common::Point(step.x, step.y);
		return true;
	case kStepClick:
		event.type = step.button == Common::MOUSE_BUTTON_RIGHT ? Common::EVENT_RBUTTONDOWN :
			step.button == Common::MOUSE_BUTTON_MIDDLE ? Common::EVENT_MBUTTONDOWN : Common::EVENT_LBUTTONDOWN;
		event.mouse = Common::Point(step.x, step.y);
		return true;
	case kStepKey:
		event.type = Common::EVENT_KEYDOWN;
		event.kbd = Common::KeyState(step.key);
		return true;
	case kStepQuit:
		event.type = Common::EVENT_QUIT;
		return true;
	default:
		return false;
	}
}

} // namespace

bool isDuneHarnessRun() {
	return checkpointing();
}

bool isDuneFastHarness() {
	if (!isDuneHarnessRun())
		return false;
	return !(ConfMan.hasKey("dune_real_time") && ConfMan.getBool("dune_real_time"));
}

void captureDuneCheckpoint(OSystem *system, const Common::String &name) {
	writeCheckpoint(system, name);
}

void setDuneHarnessLog(StartupLog *log) {
	g_harness.log = log;
}

uint takeDuneHarnessPeriods() {
	const uint n = g_harness.periods;
	g_harness.periods = 0;
	return n;
}

bool pollDuneEvent(OSystem *system, Common::Event &event) {
	if (!loadScript())
		return system->getEventManager()->pollEvent(event);
	if (!g_harness.start)
		g_harness.start = system->getMillis();

	while (g_harness.next < g_harness.steps.size()) {
		const Step &step = g_harness.steps[g_harness.next];
		if (system->getMillis() - g_harness.start < step.at)
			break;
		++g_harness.next;
		// Each step in the engine's log with its script line, so what it hit
		// (the lines after it) can be traced back to the script.
		if (g_harness.log)
			g_harness.log->line(Common::String::format("Script line %u: %s", step.line, step.text.c_str()));
		if (step.type == kStepCheckpoint) {
			writeCheckpoint(system, step.name);
			continue;
		}
		if (step.type == kStepPeriods) {
			g_harness.periods += (uint)step.x;
			continue;
		}
		return makeEvent(step, event);
	}
	return system->getEventManager()->pollEvent(event);
}

} // namespace Dune

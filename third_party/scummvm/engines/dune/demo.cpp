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

#include "dune/demo.h"
#include "dune/build_id.h"
#include "dune/debug.h"
#include "dune/harness.h"
#include "common/config-manager.h"
#include "common/system.h"
#include "common/textconsole.h"

namespace Dune {
namespace {
DemoSession *session = nullptr;
const char magic[8] = {'D','U','N','E','R','E','C','1'};
// These keys affect the initial game, intro, clock or input interpretation.
const char *const settings[] = {
	"dune_language", "dune_rng_seed", "dune_floppy_start", "dune_intro_start",
	"dune_skip_cd_story", "dune_fix_leto_loop", "dune_fix_celimyn_tuek",
	"dune_no_music", "dune_no_sound", "music_driver", "dune_real_time"
};
bool mouseEvent(Common::EventType type) {
	return type == Common::EVENT_MOUSEMOVE || type == Common::EVENT_LBUTTONDOWN ||
		type == Common::EVENT_LBUTTONUP || type == Common::EVENT_RBUTTONDOWN ||
		type == Common::EVENT_RBUTTONUP || type == Common::EVENT_MBUTTONDOWN ||
		type == Common::EVENT_MBUTTONUP || type == Common::EVENT_WHEELUP || type == Common::EVENT_WHEELDOWN;
}
bool inputEvent(Common::EventType type) {
	return mouseEvent(type) || type == Common::EVENT_KEYDOWN || type == Common::EVENT_KEYUP;
}
void writeString(Common::DumpFile &file, const Common::String &value) {
	file.writeUint16LE(value.size());
	file.write(value.c_str(), value.size());
}
bool readString(Common::File &file, Common::String &value) {
	uint size = file.readUint16LE();
	if (size > 256 || file.size() - file.pos() < size) return false;
	value.clear();
	while (size--) { byte c = file.readByte(); if (!c) return false; value += (char)c; }
	return !file.err();
}
}

DemoSession::DemoSession(OSystem *system, StartupLog &log) : _system(system), _log(log) {}

DemoSession::~DemoSession() {
	if (_recording) {
		Common::Event event;
		if (!_ended) writeFrame(event, true);
		const bool ok = _output.flush() && !_output.err();
		_output.close();
		_log.line(Common::String::format("REC: %s; %u input events", ok ? "finalized" : "WRITE FAILED", _count));
	}
	for (uint i = 0; i < _saved.size(); ++i) {
		if (_saved[i].present) ConfMan.set(settings[i], _saved[i].value, Common::ConfigManager::kTransientDomain);
		else ConfMan.removeKey(settings[i], Common::ConfigManager::kTransientDomain);
	}
	if (session == this) session = nullptr;
}

Common::Error DemoSession::begin(uint release) {
	const Common::String rec = ConfMan.hasKey("dune_rec") ? ConfMan.get("dune_rec") : "";
	const Common::String dem = ConfMan.hasKey("dune_dem") ? ConfMan.get("dune_dem") : "";
	if (rec.empty() && dem.empty()) return Common::kNoError;
	if (!rec.empty() && !dem.empty()) return Common::Error(Common::kUnknownError, "Choose REC or DEM, not both.");
	const char *const incompatible[] = {"dune_speedrun", "dune_dump", "dune_story_setup", "dune_test_flight", "dune_test_cockpit", "dune_segacd_showcase", "dune_no_raids"};
	for (uint i = 0; i < ARRAYSIZE(incompatible); ++i)
		if (ConfMan.hasKey(incompatible[i]))
			return Common::Error(Common::kUnknownError, Common::String::format("REC/DEM requires ordinary gameplay; remove %s.", incompatible[i]));
	if (!dem.empty() && ConfMan.hasKey("dune_input"))
		return Common::Error(Common::kUnknownError, "DEM cannot run alongside an input script.");

	Common::File file;
	Common::Array<Setting> recorded;
	if (!dem.empty()) {
		if (!file.open(Common::FSNode(Common::Path(dem)))) return Common::Error(Common::kReadingFailed, "Cannot open the engine recording.");
		char signature[8];
		if (file.size() > 64 * 1024 * 1024 || file.read(signature, 8) != 8 || memcmp(signature, magic, 8))
			return Common::Error(Common::kReadingFailed, "Not a DUNEREC1 engine recording. Original DOS demo files must be played by DUNEPRG DEM.");
		if (file.readUint32LE() != release || file.readUint32LE() != ARRAYSIZE(settings))
			return Common::Error(Common::kReadingFailed, "Recording release or format does not match this game.");
		Common::String build;
		if (!readString(file, build)) return Common::Error(Common::kReadingFailed, "Invalid recording header.");
		if (build != DUNE_BUILD_ID) _log.line("DEM: recorded with another build: " + build);
		for (uint i = 0; i < ARRAYSIZE(settings); ++i) {
			Setting setting;
			uint present = file.readByte();
			setting.present = present == 1;
			if (present > 1 || !readString(file, setting.value)) return Common::Error(Common::kReadingFailed, "Invalid recording options.");
			recorded.push_back(setting);
		}
		if ((file.size() - file.pos()) % 20 || file.pos() == file.size())
			return Common::Error(Common::kReadingFailed, "Truncated engine recording.");
		while (file.pos() < file.size()) {
			Frame frame;
			if (!readFrame(file, frame) || (!_frames.empty() &&
				(frame.stage < _frames.back().stage || (frame.stage == _frames.back().stage && frame.at < _frames.back().at) || _frames.back().end)))
				return Common::Error(Common::kReadingFailed, "Invalid event order or damaged recording.");
			_frames.push_back(frame);
		}
		if (!_frames.back().end) return Common::Error(Common::kReadingFailed, "Recording was interrupted before finalization.");
		file.close();
	} else {
		Common::FSNode node{Common::Path(rec)};
		if (node.exists()) return Common::Error(Common::kWritingFailed, "Recording already exists; choose a new filename.");
		if (!_output.open(node)) return Common::Error(Common::kWritingFailed, "Cannot create the recording. Choose an existing writable folder.");
	}

	// Override only the transient domain, restoring every entry on all exits.
	for (uint i = 0; i < ARRAYSIZE(settings); ++i) {
		Setting old;
		old.present = ConfMan.hasKey(settings[i], Common::ConfigManager::kTransientDomain);
		if (old.present) old.value = ConfMan.get(settings[i], Common::ConfigManager::kTransientDomain);
		_saved.push_back(old);
		if (!dem.empty()) {
			// Presence-sensitive keys must also be absent in the selected target.
			if (!recorded[i].present && ConfMan.hasKey(settings[i]))
				return Common::Error(Common::kUnknownError, Common::String::format("DEM requires %s to be absent, as in the recording. Use the dedicated replay launcher/config.", settings[i]));
			if (recorded[i].present) ConfMan.set(settings[i], recorded[i].value, Common::ConfigManager::kTransientDomain);
		}
	}
	if (!ConfMan.hasKey("dune_rng_seed")) ConfMan.setInt("dune_rng_seed", _system->getMillis() & 0x7fffffff, Common::ConfigManager::kTransientDomain);
	ConfMan.setBool("dune_real_time", true, Common::ConfigManager::kTransientDomain);
	if (!rec.empty()) {
		_output.write(magic, 8);
		_output.writeUint32LE(release);
		_output.writeUint32LE(ARRAYSIZE(settings));
		writeString(_output, DUNE_BUILD_ID);
		for (uint i = 0; i < ARRAYSIZE(settings); ++i) {
			bool present = ConfMan.hasKey(settings[i]);
			_output.writeByte(present ? 1 : 0);
			writeString(_output, present ? ConfMan.get(settings[i]) : "");
		}
		if (!_output.flush() || _output.err()) return Common::Error(Common::kWritingFailed, "Could not write the recording header.");
		_recording = true;
		_log.line("REC: recording engine input to " + rec);
	} else {
		_playing = true;
		_log.line(Common::String::format("DEM: replaying %u events from %s; Escape aborts", _frames.size() - 1, dem.c_str()));
	}
	session = this;
	return Common::kNoError;
}

uint32 DemoSession::elapsed() {
	if (!_clockStarted) { _start = _system->getMillis(); _clockStarted = true; }
	return _system->getMillis() - _start;
}
void DemoSession::gameplay() {
	_stage = 2;
	_start = _system->getMillis();
	_clockStarted = true;
}
bool DemoSession::readFrame(Common::File &file, Frame &frame) {
	frame.at = file.readUint32LE();
	frame.stage = file.readUint16LE();
	const uint type = file.readUint16LE();
	frame.end = type == 0xffff;
	frame.event = Common::Event();
	frame.event.type = frame.end ? Common::EVENT_QUIT : (Common::EventType)type;
	frame.event.mouse.x = file.readSint16LE();
	frame.event.mouse.y = file.readSint16LE();
	const uint32 key = file.readUint32LE();
	frame.event.kbd.keycode = (Common::KeyCode)key;
	frame.event.kbd.ascii = file.readUint16LE();
	frame.event.kbd.flags = file.readByte();
	frame.event.kbdRepeat = file.readByte() != 0;
	return !file.err() && frame.stage >= 1 && frame.stage <= 2 && frame.at < 259200000 &&
		(frame.end || inputEvent(frame.event.type)) && key <= 0xffff &&
		(!mouseEvent(frame.event.type) || (frame.event.mouse.x >= 0 && frame.event.mouse.x < 320 && frame.event.mouse.y >= 0 && frame.event.mouse.y < 224));
}
void DemoSession::writeFrame(const Common::Event &event, bool end) {
	_output.writeUint32LE(elapsed());
	_output.writeUint16LE(_stage);
	_output.writeUint16LE(end ? 0xffff : (uint)event.type);
	_output.writeSint16LE(event.mouse.x);
	_output.writeSint16LE(event.mouse.y);
	_output.writeUint32LE(event.kbd.keycode);
	_output.writeUint16LE(event.kbd.ascii);
	_output.writeByte(event.kbd.flags);
	_output.writeByte(event.kbdRepeat ? 1 : 0);
	if (!_output.flush() || _output.err()) {
		_recording = false;
		error("Dune REC: write failed; recording is incomplete (check free disk space)");
	}
}
void DemoSession::record(const Common::Event &event) {
	elapsed(); // Start the intro clock on the first poll, including idle polls.
	if (_recording && !_ended) {
		if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
			writeFrame(event, true);
			_ended = true;
		} else if (inputEvent(event.type)) { writeFrame(event, false); ++_count; }
	}
}
bool DemoSession::poll(Common::Event &event) {
	if (_ended) return false;
	const Frame &frame = _frames[_next];
	if (frame.stage < _stage) error("Dune DEM: game left the intro before its recorded input finished");
	if (frame.stage != _stage || elapsed() < frame.at) return false;
	++_next;
	event = frame.event;
	if (frame.end) {
		_ended = true;
		captureDuneCheckpoint(_system, "demo-end");
		_log.line(Common::String::format("DEM: completed; %u input events", _next - 1));
	} else if (mouseEvent(event.type)) {
		_system->warpMouse(event.mouse.x, event.mouse.y);
		debugSetCursor(event.mouse.x, event.mouse.y);
	}
	return true;
}
bool duneDemoActive() { return session != nullptr; }
void duneDemoGameplay() { if (session) session->gameplay(); }
bool duneDemoPlayback() { return session && session->playback(); }
bool pollDuneDemo(Common::Event &event) { return session && session->poll(event); }
void recordDuneDemo(const Common::Event &event) { if (session) session->record(event); }
} // namespace Dune

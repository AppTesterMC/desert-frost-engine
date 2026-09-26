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

#ifndef ENGINES_DUNE_DEBUG_H
#define ENGINES_DUNE_DEBUG_H

#include "common/str.h"

class OSystem;

namespace Common {
class OutSaveFile;
}

namespace Graphics {
struct Surface;
}

namespace Dune {

/**
 * Bring-up aids shared by the whole engine. None of this is game logic.
 *
 * Why it exists: the target device (an iPhone, the app sideloaded)
 * offers no debugger and no console, and screenshots cannot be taken from the
 * build machine's shell. So the engine writes a plain-text log through the
 * save manager, which the user can copy off the phone, and can dump its own
 * screen to BMP files on desktop runs.
 *
 * Config keys (in the [scummvm] domain or the target's domain):
 *   dune_dump=<dir>       run untimed, write <name>.bmp screenshots to <dir>
 *                         and exit after the last one
	 *   dune_intro_start=<n>  CD only: start the intro at the n-th video
	 *   dune_speech_probe=1   CD only: play later IRULAN speech for preflight
	 *   dune_no_music=1       do not start the AdLib music
	 *   dune_floppy_start=<n> floppy only: skip the first n intro scenes
 *
 * The diagnostic overlay is compile-time only. Build the iOS target with
 * DUNE_DEBUG=1 to draw it and write dune_debug.log through the platform save
 * manager (Documents on iOS); ordinary builds contain the no-op hooks.
 */

/** Plain-text progress log, written as "dune-ios.log" in the save directory. */
class StartupLog {
public:
	StartupLog();
	~StartupLog();

	void line(const Common::String &message);

private:
	Common::OutSaveFile *_file;
};

/** Show a short message on ScummVM's on-screen display (lags on iOS). */
void showStatus(const char *message);

/** True when the "dune_dump" key is set, i.e. this is an automated dump run. */
bool isDumpRun();

/** A recorded run (dune_record=<folder>): the screen as a frame shown for @p millis. */
bool isRecording();
void recordFrame(OSystem *system, uint millis);
/** On a dump run, write the current screen as <name>.bmp; otherwise nothing. */
void dumpScreen(OSystem *system, const char *name);

/**
 * "dune_dump_every=<ms>": on a dump run the intro plays in real time and
 * writes a frame every so many milliseconds (s<scene>-<ms>.bmp) instead of
 * one frame per scene, so an animation can be checked against the recording.
 */
uint dumpEveryMillis();

/** Begin/end the compile-time DUNE_DEBUG diagnostic session. */
void debugBegin(OSystem *system);
void debugEnd();

/** Update the live values shown by the DUNE_DEBUG overlay. */
void debugSetRoom(int room);
void debugSetScene(const Common::String &scene);
void debugSetSpriteOrigin(int x, int y);
void debugSetAudioStream(const Common::String &state);
void debugSetCursor(int x, int y);

/** Draw the current diagnostic state into a Dune 8-bit framebuffer. */
void debugOverlay(Graphics::Surface &surface);

} // namespace Dune

#endif // ENGINES_DUNE_DEBUG_H

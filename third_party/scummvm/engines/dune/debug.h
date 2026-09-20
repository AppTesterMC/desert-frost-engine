/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_DEBUG_H
#define ENGINES_DUNE_DEBUG_H

#include "common/str.h"

class OSystem;

namespace Common {
class OutSaveFile;
}

namespace Dune {

/**
 * Bring-up aids shared by the whole engine. None of this is game logic.
 *
 * Why it exists: the target device (an iPhone, installed through TrollStore)
 * offers no debugger and no console, and screenshots cannot be taken from the
 * build machine's shell. So the engine writes a plain-text log through the
 * save manager, which the user can copy off the phone, and can dump its own
 * screen to BMP files on desktop runs.
 *
 * Config keys (in the [scummvm] domain or the target's domain):
 *   dune_dump=<dir>       run untimed, write <name>.bmp screenshots to <dir>
 *                         and exit after the last one
 *   dune_intro_start=<n>  CD only: start the intro at the n-th video
 *   dune_no_music=1       do not start the AdLib music
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

/** On a dump run, write the current screen as <name>.bmp; otherwise nothing. */
void dumpScreen(OSystem *system, const char *name);

} // namespace Dune

#endif // ENGINES_DUNE_DEBUG_H

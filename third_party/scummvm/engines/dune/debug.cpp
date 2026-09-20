/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#include "common/config-manager.h"
#include "common/file.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/ustr.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "image/bmp.h"

#include "dune/debug.h"

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

void dumpScreen(OSystem *system, const char *name) {
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

} // namespace Dune

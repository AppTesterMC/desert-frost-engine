/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#include "common/config-manager.h"
#include "common/scummsys.h"

#include "graphics/cursorman.h"

#include "dune/cursor.h"

namespace Dune {

namespace {

const uint kCursorSize = 16;
const uint kHotspotX = 0, kHotspotY = 0;

// Verbatim from DNCDPRG.EXE, DS:0x2588 (mask) and DS:0x25A8 (image).
const uint16 kArrowMask[kCursorSize] = {
	0x3fff, 0x1fff, 0x0fff, 0x07ff, 0x03ff, 0x01ff, 0x00ff, 0x007f,
	0x003f, 0x003f, 0x01ff, 0x10ff, 0x30ff, 0xf87f, 0xf87f, 0xfc7f
};
const uint16 kArrowImage[kCursorSize] = {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7c00, 0x7e00, 0x7f00,
	0x7f80, 0x7c00, 0x6c00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000
};

enum {
	kTransparent = 0,
	kBlack = 1,
	kWhite = 2
};

} // namespace

void showGameCursor() {
	byte pixels[kCursorSize * kCursorSize];
	for (uint y = 0; y < kCursorSize; ++y) {
		for (uint x = 0; x < kCursorSize; ++x) {
			const uint16 bit = 0x8000 >> x;
			byte &pixel = pixels[y * kCursorSize + x];
			if (kArrowImage[y] & bit)
				pixel = kWhite;
			else
				pixel = (kArrowMask[y] & bit) ? kTransparent : kBlack;
		}
	}

	const byte palette[3 * 3] = { 0, 0, 0, 0, 0, 0, 255, 255, 255 };
	CursorMan.replaceCursor(pixels, kCursorSize, kCursorSize, kHotspotX, kHotspotY, kTransparent);
	CursorMan.replaceCursorPalette(palette, 0, 3);
	CursorMan.showMouse(true);
}

void hideGameCursor() {
	CursorMan.showMouse(false);
}

void preferDirectTouch() {
	// The same key is read by the iOS and Android backends.
	if (!ConfMan.hasKey("touch_mode_2d_games"))
		ConfMan.set("touch_mode_2d_games", "direct", Common::ConfigManager::kTransientDomain);
}

} // namespace Dune

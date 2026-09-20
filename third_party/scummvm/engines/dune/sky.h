/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_SKY_H
#define ENGINES_DUNE_SKY_H

#include "common/scummsys.h"

class OSystem;

namespace Graphics {
struct Surface;
}

namespace Dune {

class Resource;

/**
 * The sky behind every outdoor view (balconies, the intro's dunes, later the
 * desert and ornithopter scenes).
 *
 * SKY.HSQ holds two sets of four 40-pixel-wide gradient tiles, stacked
 * vertically and repeated across the view: sprites 0-3 (20 rows each, the
 * "narrow" sky) and 4-7 (30 rows each, the "large" sky). The tiles use
 * colours 128-222, and the sheet carries 33 palettes for that range (offset
 * table entries 8-40), one per time of day; the original cross-fades between
 * neighbours as the day passes (sub_138B4 in the CD listing).
 *
 * The palette numbers for the four times of day below are the ones
 * codingstyle's swift-dune uses; the full time-of-day mapping is not decoded.
 */
enum SkyPalette {
	kSkyDay = 1,
	kSkyNight = 3,
	kSkySunset = 6,
	kSkySunrise = 16
};

enum SkyType {
	kSkyNarrow, ///< 4 x 20 rows: balconies, intro dunes
	kSkyLarge   ///< 4 x 30 rows: the palace stairs
};

/**
 * Set the sky palette and tile the sky from the top-left of target, width
 * pixels wide. Call it before drawing what stands in front of the sky, and
 * before the foreground's palette if that overlaps 128-222.
 */
bool drawSky(OSystem *system, Resource &resources, Graphics::Surface &target, SkyType type, int width,
			 uint palette);

} // namespace Dune

#endif // ENGINES_DUNE_SKY_H

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

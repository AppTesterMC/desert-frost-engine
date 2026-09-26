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

#include "common/array.h"
#include "common/system.h"

#include "graphics/surface.h"

#include "dune/resource.h"
#include "dune/sky.h"

#include "graphics/paletteman.h"
#include "dune/sprite.h"

namespace Dune {

bool drawSky(OSystem *system, Resource &resources, Graphics::Surface &target, SkyType type, int width,
		uint palette, bool panelTail) {
	Common::Array<byte> data;
	if (!resources.load("SKY.HSQ", data))
		return false;

	Sprite sky(system, data);
	const uint firstPaletteRecord = 8;
	if (panelTail) {
		// In the game (floppy seg000:3B13): a record's 95 colours are the
		// sky's 80 (128-207) and then the panel's 15 (240-254), which is why
		// the panel is blue-grey by day, orange at sunset and purple at night
		// outdoors. Written contiguously they would land on the characters'
		// colours (208-222).
		byte rgb[256 * 3];
		uint start = 0, count = 0;
		if (!sky.getPaletteRecord((uint16)(firstPaletteRecord + palette), rgb, start, count))
			return false;
		const uint skyCount = MIN<uint>(count, 80);
		system->getPaletteManager()->setPalette(rgb, start, skyCount);
		if (count > 80)
			system->getPaletteManager()->setPalette(rgb + 80 * 3, 240, MIN<uint>(count - 80, 15));
	} else if (!sky.setPaletteRecord(firstPaletteRecord + palette)) {
		return false;
	}

	const uint firstTile = type == kSkyNarrow ? 0 : 4;
	const int tileHeight = type == kSkyNarrow ? 20 : 30;
	for (int x = 0; x < width; x += 40)
		for (uint tile = 0; tile < 4; ++tile)
			sky.drawFrame(firstTile + tile, &target, x, (int)tile * tileHeight);
	return true;
}

} // namespace Dune

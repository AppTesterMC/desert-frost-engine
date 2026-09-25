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

#include "graphics/surface.h"

#include "dune/resource.h"
#include "dune/sky.h"
#include "dune/sprite.h"

namespace Dune {

bool drawSky(OSystem *system, Resource &resources, Graphics::Surface &target, SkyType type, int width,
		uint palette) {
	Common::Array<byte> data;
	if (!resources.load("SKY.HSQ", data))
		return false;

	Sprite sky(system, data);
	const uint firstPaletteRecord = 8;
	if (!sky.setPaletteRecord(firstPaletteRecord + palette))
		return false;

	const uint firstTile = type == kSkyNarrow ? 0 : 4;
	const int tileHeight = type == kSkyNarrow ? 20 : 30;
	for (int x = 0; x < width; x += 40)
		for (uint tile = 0; tile < 4; ++tile)
			sky.drawFrame(firstTile + tile, &target, x, (int)tile * tileHeight);
	return true;
}

} // namespace Dune

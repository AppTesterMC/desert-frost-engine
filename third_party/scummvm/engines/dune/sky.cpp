/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
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

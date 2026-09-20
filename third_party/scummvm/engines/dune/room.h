/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_ROOM_H
#define ENGINES_DUNE_ROOM_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Graphics {
struct Surface;
}

namespace Dune {

class Sprite;

/**
 * Renderer for the room descriptions in the .SAL files (PALACE, SIET, VILG,
 * HARK). Which sprite sheet a room is drawn with is decided by the location
 * table (see palace.h), not by the .SAL file.
 *
 * A file starts with a table of 16-bit room offsets. A room is a marker count
 * followed by command words until FFFF:
 *
 *   bit 15 clear      sprite or character marker: bits 0-8 sprite + 1 (1 =
 *                     marker), bit 9 x + 256, bits 10-12 scale, bit 13 flip y,
 *                     bit 14 flip x; then x, y, palette offset
 *   bits 15, !14      polygon filled with a gradient and LFSR noise
 *   bits 15, 14       line
 *
 * The layout and the polygon rasteriser follow madmoose's dune-rust, which was
 * derived from the original's sub_13B59. FINDINGS.md has the details,
 * including what is missing (line dithering, the sky).
 */
class Room {
public:
	explicit Room(const Common::Array<byte> &data) : _data(data) {}

	uint roomCount() const;
	bool draw(uint room, Sprite &sprites, Graphics::Surface &target, Sprite *characters = nullptr,
			const Common::Array<uint16> *markerSprites = nullptr) const;

private:
	const Common::Array<byte> &_data;
};

} // namespace Dune

#endif // ENGINES_DUNE_ROOM_H

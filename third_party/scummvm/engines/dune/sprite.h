/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_SPRITE_H
#define ENGINES_DUNE_SPRITE_H

#include "common/array.h"

namespace Graphics {
struct Surface;
}

class OSystem;

namespace Dune {

/**
 * A Cryo sprite sheet: palette blocks followed by independently stored
 * sprites (4-bit or 8-bit, optionally RLE-packed). Sheets are collections of
 * parts that a room, the panel or a scene places; they are not animations.
 * The format is described in FINDINGS.md.
 *
 * The object keeps its own copy of the sheet data, so it can outlive the
 * buffer it was made from.
 */
class Sprite {
public:
	Sprite(OSystem *system, const Common::Array<byte> &data);

	/** Apply the sheet's palette blocks to the screen palette. */
	bool setPalette();
	/**
	 * Apply one of a sheet's extra palettes. SKY.HSQ and SUNRS.HSQ list them
	 * in the sprite offset table after the real sprites: a record is a zero
	 * word, a size word, first colour, count, then count RGB triplets.
	 * @param tableEntry index into the offset table (SKY.HSQ: 8 + palette)
	 */
	bool setPaletteRecord(uint16 tableEntry);

	/** Number of entries in the offset table (sprites and palette records). */
	uint16 frameCount() const;
	bool drawFrame(uint16 frameIndex, uint16 x, uint16 y);
	/**
	 * Draw a sprite into target (or straight to the screen when null), clipped.
	 * @param scale           0-7, index into the original's shrink factors
	 * @param paletteOverride replaces the sprite's own palette offset when not 0
	 */
	bool drawFrame(uint16 frameIndex, Graphics::Surface *target, int x, int y, bool flipX = false, bool flipY = false,
				   uint scale = 0, byte paletteOverride = 0);

	/** Header of one sprite: where its pixels start, size, RLE flag, palette offset. */
	bool getFrameInfo(uint16 frameIndex, uint32 &dataOffset, uint16 &width,
					 uint16 &height, bool &compressed, int8 &paletteOffset) const;

private:
	OSystem *_system;
	Common::Array<byte> _data;
};

} // namespace Dune

#endif // ENGINES_DUNE_SPRITE_H

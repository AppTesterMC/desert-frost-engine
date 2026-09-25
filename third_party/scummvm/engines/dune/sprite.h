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
 * sprites (4-bit or 8-bit, optionally RLE-packed). Some character sheets add
 * an animation block after the ordinary sprite table. That block is decoded
 * using the original image-group/frame tables; it is not a guessed sequence
 * of individual sprites.
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
	/** Select an alternate palette record blended from the previous record. */
	bool setPaletteRecordBlend(uint16 tableEntry, uint16 previousTableEntry, uint level);

	/** Number of entries in the offset table (sprites and palette records). */
	uint16 frameCount() const;
	/** Append a second sheet's ordinary frames, as SHAI2 extends SHAI. */
	void mergeFrames(const Sprite &other);
	/** Use SHAI's raw six-byte sprite-position animation stream. */
	void setShaiAnimationFormat();
	bool drawFrame(uint16 frameIndex, uint16 x, uint16 y);
	/**
	 * Draw a sprite into target (or straight to the screen when null), clipped.
	 * @param scale           0-7, index into the original's shrink factors
	 * @param paletteOverride replaces the sprite's own palette offset when not 0
	 */
	bool drawFrame(uint16 frameIndex, Graphics::Surface *target, int x, int y, bool flipX = false, bool flipY = false,
				   uint scale = 0, byte paletteOverride = 0) const;
	/**
	 * Like drawFrame() but with a free source step in 8.8 fixed point:
	 * 0x100 draws 1:1, 0x200 at half size, 0xa00 at a tenth. The intro's
	 * desert walk, kiss and flight scenes shrink dune sprites by ratios the
	 * room table does not offer.
	 */
	bool drawFrameScaled(uint16 frameIndex, Graphics::Surface *target, int x, int y, uint32 factor,
						 bool flipX = false, bool flipY = false, byte paletteOverride = 0) const;
	/** Width and height of one sprite, or false if the index is not a sprite. */
	bool frameSize(uint16 frameIndex, uint16 &width, uint16 &height) const;
	/**
	 * Read one palette record (see setPaletteRecord()) without applying it:
	 * rgb receives count 8-bit triplets. ATTACK.HSQ keeps its sky-flash
	 * colours this way (records 53-55).
	 */
	bool getPaletteRecord(uint16 tableEntry, byte *rgb, uint &start, uint &count) const;

	/** Number of authored animation definitions in the optional animation block. */
	uint16 animationCount();
	/** Number of authored frames in one animation definition. */
	uint16 animationFrameCount(uint16 animationIndex);
	/** Draw one authored animation frame, including all of its image groups. */
	bool drawAnimationFrame(uint16 animationIndex, uint16 frameIndex, Graphics::Surface *target,
							int x = 0, int y = 0);
	/** Draw the authored animation at its 12 fps resource rate. */
	bool drawAnimation(uint16 animationIndex, uint32 elapsedMillis, Graphics::Surface *target,
					   int x = 0, int y = 0);

	/** Header of one sprite: where its pixels start, size, RLE flag, palette offset. */
	bool getFrameInfo(uint16 frameIndex, uint32 &dataOffset, uint16 &width,
					 uint16 &height, bool &compressed, int8 &paletteOffset) const;

private:
	struct AnimationImage {
		uint16 frame;
		int16 x, y;
	};
	struct AnimationGroup {
		Common::Array<AnimationImage> images;
	};
	struct AnimationFrame {
		Common::Array<uint16> groups;
	};
	struct Animation {
		int16 x, y;
		Common::Array<AnimationFrame> frames;
	};

	void parseAnimations();

	OSystem *_system;
	Common::Array<byte> _data;
	bool _animationsParsed;
	bool _shaiAnimationFormat;
	Common::Array<AnimationGroup> _animationGroups;
	Common::Array<Animation> _animations;
	const Sprite *_mergedFrames;
};

} // namespace Dune

#endif // ENGINES_DUNE_SPRITE_H

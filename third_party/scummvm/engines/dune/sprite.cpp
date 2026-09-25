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

#include "common/scummsys.h"
#include "common/endian.h"
#include "common/util.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/surface.h"
#include "graphics/paletteman.h"

#include "dune/debug.h"
#include "dune/sprite.h"

namespace Dune {

Sprite::Sprite(OSystem *system, const Common::Array<byte> &data) :
		_system(system), _data(data), _animationsParsed(false), _shaiAnimationFormat(false), _mergedFrames(nullptr) {
}

bool Sprite::setPalette() {
	if (_data.size() < 2)
		return false;

	const uint32 chunkEnd = READ_LE_UINT16(_data.data());
	if (chunkEnd < 2 || chunkEnd > _data.size())
		return false;

	uint32 position = 2;
	while (position + 2 <= chunkEnd) {
		const byte paletteStart = _data[position++];
		const byte paletteCount = _data[position++];
		if (paletteStart == 0xff && paletteCount == 0xff)
			break;
		if (paletteStart == 0 && paletteCount == 1) {
			// The original palette reader reserves colour 0 and skips its
			// three-byte placeholder.
			if (position + 3 > chunkEnd)
				return false;
			position += 3;
			continue;
		}
		if (paletteStart > 255 || paletteCount > 256 - paletteStart)
			return false;
		if (position + (uint32)paletteCount * 3 > chunkEnd)
			return false;

		// VGA DAC values are 6-bit. The original and the reference ports use
		// the DOS-era promotion (v << 2), which tops out at 252 rather than
		// rounding 63 to 255. This matters for the unpaletted BALCON sheet:
		// its colours are inherited from SKY.HSQ's active palette.
		byte palette[256 * 3];
		for (uint i = 0; i < (uint)paletteCount * 3; ++i) {
			const byte value = _data[position++] & 0x3f;
			palette[i] = value << 2;
		}
		_system->getPaletteManager()->setPalette(palette, paletteStart, paletteCount);
	}

	return true;
}

bool Sprite::setPaletteRecord(uint16 tableEntry) {
	if (tableEntry >= frameCount())
		return false;

	const uint32 tableStart = READ_LE_UINT16(_data.data());
	uint32 position = tableStart + READ_LE_UINT16(_data.data() + tableStart + tableEntry * 2);
	if (position + 6 > _data.size() || READ_LE_UINT16(_data.data() + position) != 0)
		return false; // A sprite, not a palette record.

	const uint start = _data[position + 4];
	const uint count = _data[position + 5];
	position += 6;
	if (start + count > 256 || position + count * 3 > _data.size())
		return false;

	byte palette[256 * 3];
	for (uint i = 0; i < count * 3; ++i) {
		const byte value = _data[position + i] & 0x3f;
		palette[i] = value << 2;
	}
	_system->getPaletteManager()->setPalette(palette, start, count);
	return true;
}

bool Sprite::setPaletteRecordBlend(uint16 tableEntry, uint16 previousTableEntry, uint level) {
	if (tableEntry >= frameCount() || previousTableEntry >= frameCount() || level > 256)
		return false;

	const uint32 tableStart = READ_LE_UINT16(_data.data());
	const uint32 position = tableStart + READ_LE_UINT16(_data.data() + tableStart + tableEntry * 2);
	const uint32 previousPosition = tableStart + READ_LE_UINT16(_data.data() + tableStart + previousTableEntry * 2);
	if (position + 6 > _data.size() || previousPosition + 6 > _data.size()
			|| READ_LE_UINT16(_data.data() + position) != 0
			|| READ_LE_UINT16(_data.data() + previousPosition) != 0)
		return false;

	const uint start = _data[position + 4];
	const uint count = _data[position + 5];
	const uint previousStart = _data[previousPosition + 4];
	const uint previousCount = _data[previousPosition + 5];
	if (start != previousStart || count != previousCount || start + count > 256
			|| position + 6 + count * 3 > _data.size()
			|| previousPosition + 6 + count * 3 > _data.size())
		return false;

	byte palette[256 * 3];
	for (uint i = 0; i < count * 3; ++i) {
		const uint from = _data[previousPosition + 6 + i] & 0x3f;
		const uint to = _data[position + 6 + i] & 0x3f;
		const uint value = (from * (256 - level) + to * level) / 256;
		palette[i] = value << 2;
	}
	_system->getPaletteManager()->setPalette(palette, start, count);
	return true;
}

uint16 Sprite::frameCount() const {
	if (_data.size() < 4)
		return 0;

	const uint32 paletteChunkEnd = READ_LE_UINT16(_data.data());
	if (paletteChunkEnd < 2 || paletteChunkEnd + 2 > _data.size())
		return 0;

	// The offset table has no length prefix: its first entry is both the
	// offset of frame 0 and the size of the table itself.
	const uint32 tableSize = READ_LE_UINT16(_data.data() + paletteChunkEnd);
	if (tableSize < 2 || paletteChunkEnd + tableSize > _data.size())
		return 0;

	const uint16 ownFrames = tableSize / 2;
	return _mergedFrames ? ownFrames + _mergedFrames->frameCount() : ownFrames;
}

void Sprite::mergeFrames(const Sprite &other) {
	_mergedFrames = &other;
}

void Sprite::setShaiAnimationFormat() {
	_shaiAnimationFormat = true;
	_animationsParsed = false;
}

bool Sprite::getFrameInfo(uint16 frameIndex, uint32 &dataOffset, uint16 &width,
						  uint16 &height, bool &compressed, int8 &paletteOffset) const {
	if (_data.size() < 4)
		return false;

	const uint32 paletteChunkEnd = READ_LE_UINT16(_data.data());
	if (paletteChunkEnd < 2 || paletteChunkEnd + 2 > _data.size())
		return false;

	const uint32 frameChunkStart = paletteChunkEnd;
	const uint32 tableSize = READ_LE_UINT16(_data.data() + frameChunkStart);
	if (tableSize < 2 || frameChunkStart + tableSize > _data.size())
		return false;

	const uint16 ownFrames = tableSize / 2;
	if (frameIndex >= ownFrames) {
		if (_mergedFrames)
			return _mergedFrames->getFrameInfo(frameIndex - ownFrames, dataOffset, width, height, compressed,
					paletteOffset);
		return false;
	}

	const uint32 offsetPosition = frameChunkStart + frameIndex * 2;
	const uint16 frameOffset = READ_LE_UINT16(_data.data() + offsetPosition);
	dataOffset = frameChunkStart + frameOffset;
	// Offsets are relative to the start of the offset table.
	if (dataOffset + 4 > _data.size())
		return false;

	const byte widthLow = _data[dataOffset++];
	const byte widthHigh = _data[dataOffset++];
	compressed = (widthHigh & 0x80) != 0;
	width = widthLow | ((widthHigh & 0x01) << 8);
	height = _data[dataOffset++];
	paletteOffset = (int8)_data[dataOffset++];
	return width && height;
}

bool Sprite::drawFrame(uint16 frameIndex, uint16 x, uint16 y) {
	return drawFrame(frameIndex, nullptr, x, y);
}

bool Sprite::drawFrame(uint16 frameIndex, Graphics::Surface *target, int x, int y, bool flipX, bool flipY,
		uint scale, byte paletteOverride) const {
	// Room sprites can be shrunk by one of eight fixed factors.
	static const uint16 scaleFactors[8] = { 0x100, 0x120, 0x140, 0x160, 0x180, 0x1c0, 0x200, 0x280 };
	return drawFrameScaled(frameIndex, target, x, y, scaleFactors[scale & 7], flipX, flipY, paletteOverride);
}

bool Sprite::frameSize(uint16 frameIndex, uint16 &width, uint16 &height) const {
	uint32 offset;
	bool compressed;
	int8 paletteOffset;
	return getFrameInfo(frameIndex, offset, width, height, compressed, paletteOffset);
}

bool Sprite::getPaletteRecord(uint16 tableEntry, byte *rgb, uint &start, uint &count) const {
	if (tableEntry >= frameCount())
		return false;
	const uint32 tableStart = READ_LE_UINT16(_data.data());
	const uint32 position = tableStart + READ_LE_UINT16(_data.data() + tableStart + tableEntry * 2);
	if (position + 6 > _data.size() || READ_LE_UINT16(_data.data() + position) != 0)
		return false;
	start = _data[position + 4];
	count = _data[position + 5];
	if (start + count > 256 || position + 6 + count * 3 > _data.size())
		return false;
	for (uint i = 0; i < count * 3; ++i)
		rgb[i] = (_data[position + 6 + i] & 0x3f) << 2;
	return true;
}

bool Sprite::drawFrameScaled(uint16 frameIndex, Graphics::Surface *target, int x, int y, uint32 factor,
		bool flipX, bool flipY, byte paletteOverride) const {
	const uint16 ownFrames = frameCount() - (_mergedFrames ? _mergedFrames->frameCount() : 0);
	if (frameIndex >= ownFrames && _mergedFrames)
		return _mergedFrames->drawFrameScaled(frameIndex - ownFrames, target, x, y, factor, flipX, flipY, paletteOverride);
	if (!factor)
		return false;

	uint32 dataOffset;
	uint16 width;
	uint16 height;
	bool compressed;
	int8 headerPaletteOffset;
	if (!getFrameInfo(frameIndex, dataOffset, width, height, compressed, headerPaletteOffset))
		return false;
	if (width > 320 || height > 200)
		return false;
	debugSetSpriteOrigin(x, y);

	// Blitting rules after dune-rust's blit.rs (see CREDITS.md).
	// A palette offset of 254/255 marks an 8-bit sprite (255: colour 0 is
	// transparent); anything else is 4-bit with the offset added to every
	// opaque pixel. Rows are stored independently: 4-bit rows are padded to
	// four pixels, and RLE runs never cross a row.
	const byte paletteOffset = paletteOverride ? paletteOverride : (byte)headerPaletteOffset;
	const bool eightBit = paletteOffset >= 254;
	const uint32 pitch = eightBit ? width : 2 * ((width + 3) / 4);

	Common::Array<byte> rows(pitch * height);
	if (compressed) {
		for (uint16 line = 0; line < height; ++line) {
			uint32 column = 0;
			while (column < pitch) {
				if (dataOffset >= _data.size())
					return false;
				const byte command = _data[dataOffset++];
				if (command & 0x80) {
					const uint32 count = 257 - command;
					if (dataOffset >= _data.size())
						return false;
					const byte value = _data[dataOffset++];
					for (uint32 i = 0; i < count; ++i, ++column)
						if (column < pitch)
							rows[line * pitch + column] = value;
				} else {
					const uint32 count = command + 1;
					if (dataOffset + count > _data.size())
						return false;
					for (uint32 i = 0; i < count; ++i, ++column) {
						if (column < pitch)
							rows[line * pitch + column] = _data[dataOffset];
						++dataOffset;
					}
				}
			}
		}
	} else {
		if (dataOffset + rows.size() > _data.size())
			return false;
		memcpy(rows.data(), _data.data() + dataOffset, rows.size());
	}
	const uint32 drawWidth = ((uint32)width << 8) / factor;
	const uint32 drawHeight = ((uint32)height << 8) / factor;

	Graphics::Surface *screen = target ? target : _system->lockScreen();
	if (!screen)
		return false;

	for (uint32 line = 0; line < drawHeight; ++line) {
		const int targetY = y + (int)(flipY ? drawHeight - 1 - line : line);
		if (targetY < 0 || targetY >= screen->h)
			continue;
		const uint32 sourceY = (line * factor) >> 8;
		const byte *row = rows.data() + sourceY * pitch;
		byte *dst = (byte *)screen->getBasePtr(0, targetY);

		for (uint32 column = 0; column < drawWidth; ++column) {
			const int targetX = x + (int)(flipX ? drawWidth - 1 - column : column);
			if (targetX < 0 || targetX >= screen->w)
				continue;
			const uint32 sourceX = (column * factor) >> 8;
			if (eightBit) {
				const byte pixel = row[sourceX];
				if (pixel || paletteOffset != 255)
					dst[targetX] = pixel;
			} else {
				const byte packed = row[sourceX / 2];
				const byte pixel = (sourceX & 1) ? (packed >> 4) : (packed & 0x0f);
				if (pixel)
					dst[targetX] = pixel + paletteOffset;
			}
		}
	}

	if (!target)
		_system->unlockScreen();
	return true;
}

void Sprite::parseAnimations() {
	if (_animationsParsed)
		return;
	_animationsParsed = true;
	_animationGroups.clear();
	_animations.clear();

	// The normal sprite table is followed by the animation block on character
	// sheets. Its first word is zero, which distinguishes it from a sprite
	// header. The location is obtained from the authored frame-table offset,
	// not by searching for a convenient byte pattern.
	if (_data.size() < 4)
		return;
	const uint32 paletteChunkEnd = READ_LE_UINT16(_data.data());
	if (paletteChunkEnd < 2 || paletteChunkEnd + 2 > _data.size())
		return;
	const uint32 frameChunkStart = paletteChunkEnd;
	const uint32 tableSize = READ_LE_UINT16(_data.data() + frameChunkStart);
	if (tableSize < 2 || frameChunkStart + tableSize > _data.size())
		return;

	uint32 animationOffset = 0;
	for (uint16 i = 0; i < tableSize / 2; ++i) {
		const uint32 entry = frameChunkStart + i * 2;
		const uint32 candidate = frameChunkStart + READ_LE_UINT16(_data.data() + entry);
		if (candidate + 2 <= _data.size() && READ_LE_UINT16(_data.data() + candidate) == 0) {
			animationOffset = candidate;
			break;
		}
	}
	if (!animationOffset || animationOffset + 14 > _data.size())
		return;

	if (_shaiAnimationFormat) {
		// SHAI is the one character resource whose animation tail is not the
		// normal group/definition table. After the zero marker and block-size
		// word it is a stream of (sprite, x, y) words; a rectangle-like run
		// (the area to restore) separates the frames. The parser follows
		// swift-dune's, keeping SHAI2 frame numbers, but each group is one
		// frame: the recording shows the 44 groups over about 3.7 s at the
		// resources' 12 fps, not the 7 s that doubling them would take.
		Animation animation;
		animation.x = 0;
		animation.y = 0;
		Common::Array<AnimationGroup> groups;
		AnimationGroup group;
		uint16 maxSpriteIndex = 0;
		uint32 position = animationOffset + 4;
		while (position + 6 <= _data.size()) {
			if (position + 8 <= _data.size()) {
				const uint16 x1 = READ_LE_UINT16(_data.data() + position);
				const uint16 y1 = READ_LE_UINT16(_data.data() + position + 2);
				const uint16 x2 = READ_LE_UINT16(_data.data() + position + 4);
				const uint16 y2 = READ_LE_UINT16(_data.data() + position + 6);
				if (x1 < x2 && y1 < y2 && x1 != (uint16)(maxSpriteIndex + 1)) {
					if (!group.images.empty()) {
						const uint16 groupIndex = groups.size();
						groups.push_back(group);
						AnimationFrame frame;
						frame.groups.push_back(groupIndex);
						animation.frames.push_back(frame);
						group = AnimationGroup();
					}
					// The four-word clear rectangle is consumed by the probe. The
					// following six bytes are the first authored sprite triple.
					position += 8;
					continue;
				}
			}

			AnimationImage image;
			image.frame = READ_LE_UINT16(_data.data() + position);
			image.x = (int16)READ_LE_UINT16(_data.data() + position + 2);
			image.y = (int16)READ_LE_UINT16(_data.data() + position + 4);
			maxSpriteIndex = image.frame;
			group.images.push_back(image);
			position += 6;
		}
		if (!group.images.empty()) {
			const uint16 groupIndex = groups.size();
			groups.push_back(group);
			AnimationFrame frame;
			frame.groups.push_back(groupIndex);
			animation.frames.push_back(frame);
		}
		_animationGroups = groups;
		if (!animation.frames.empty())
			_animations.push_back(animation);
		return;
	}

	const uint32 blockSize = READ_LE_UINT16(_data.data() + animationOffset + 2);
	if (blockSize < 14 || animationOffset + blockSize > _data.size())
		return;

	Animation animationHeader;
	animationHeader.x = (int16)READ_LE_UINT16(_data.data() + animationOffset + 4);
	animationHeader.y = (int16)READ_LE_UINT16(_data.data() + animationOffset + 6);
	const uint16 animationWidth = READ_LE_UINT16(_data.data() + animationOffset + 8);
	const uint16 animationHeight = READ_LE_UINT16(_data.data() + animationOffset + 10);
	const uint32 definitionOffset = READ_LE_UINT16(_data.data() + animationOffset + 12);
	if (!animationWidth || !animationHeight || animationOffset + 14 + 2 > _data.size())
		return;

	// The first word after the 14-byte header is the size, in bytes, of the
	// image-group offset table. The final offset is the boundary before the
	// group data, hence the -1 entry in the original readers.
	const uint32 groupsTable = animationOffset + 14;
	const uint32 groupsTableSize = READ_LE_UINT16(_data.data() + groupsTable);
	if (groupsTableSize < 2 || groupsTable + groupsTableSize > animationOffset + blockSize)
		return;
	const uint32 groupCount = groupsTableSize / 2 - 1;
	for (uint32 i = 0; i < groupCount; ++i) {
		// The first table word is both the byte size and the offset of the
		// first group. This slightly unusual layout is how the original
		// readers determine the group count.
		const uint32 groupOffset = groupsTable + READ_LE_UINT16(_data.data() + groupsTable + i * 2);
		if (groupOffset >= animationOffset + blockSize)
			return;

		AnimationGroup group;
		uint32 position = groupOffset;
		while (position < animationOffset + blockSize) {
			const byte image = _data[position++];
			if (!image)
				break;
			if (position + 2 > animationOffset + blockSize)
				return;
			AnimationImage entry;
			entry.frame = image - 1;
			entry.x = _data[position++];
			entry.y = _data[position++];
			group.images.push_back(entry);
		}
		if (position > animationOffset + blockSize || (position == animationOffset + blockSize &&
				_data[position - 1] != 0))
			return;
		_animationGroups.push_back(group);
	}

	// The animation definition TOC is explicitly stored in the header. Each
	// entry points to a byte stream of image-group references; 0 separates
	// frames and 0xff terminates one animation.
	const uint32 animationToc = animationOffset + 14 + definitionOffset - 2;
	if (definitionOffset < 2 || animationToc + 2 > animationOffset + blockSize)
		return;
	Common::Array<uint16> animationOffsets;
	uint16 lastOffset = 0;
	while (animationToc + animationOffsets.size() * 2 + 2 <= animationOffset + blockSize) {
		const uint16 next = READ_LE_UINT16(_data.data() + animationToc + animationOffsets.size() * 2);
		if (!animationOffsets.empty() && !(lastOffset < next && next < 0x100))
			break;
		animationOffsets.push_back(next);
		lastOffset = next;
	}
	if (animationOffsets.empty())
		return;

	for (uint i = 0; i < animationOffsets.size(); ++i) {
		const uint32 position = animationToc + animationOffsets[i];
		if (position >= animationOffset + blockSize)
			return;
		Animation animation = animationHeader;
		AnimationFrame frame;
		uint32 cursor = position;
		// The final BARO definition's terminator is just beyond its declared
		// block size; Swift's stream parser accepts it at resource EOF. Keep the
		// same boundary for the final authored definition only.
		const uint32 animationEnd = i + 1 == animationOffsets.size() ? _data.size() : animationOffset + blockSize;
		while (cursor < animationEnd && _data[cursor] != 0xff) {
			const byte groupIndex = _data[cursor++];
			if (!groupIndex) {
				animation.frames.push_back(frame);
				frame = AnimationFrame();
				continue;
			}
			if (groupIndex < 2 || _animationGroups.empty())
				return;
			// The original reader clamps an authored group reference to the
			// last available group. BARO.HSQ uses this deliberately at the end
			// of its fifth animation; rejecting it drops the whole Baron scene.
			frame.groups.push_back(MIN<uint16>(groupIndex - 2, _animationGroups.size() - 1));
		}
		// BARO's final authored definition places its 0xff terminator at the
		// first byte after the declared block, exactly as the original Swift
		// reader's resource-end loop accepts it.
		if (cursor >= animationEnd || _data[cursor] != 0xff)
			return;
		if (!frame.groups.empty())
			animation.frames.push_back(frame);
		if (animation.frames.empty())
			return;
		_animations.push_back(animation);
	}
}

uint16 Sprite::animationCount() {
	parseAnimations();
	return _animations.size();
}

uint16 Sprite::animationFrameCount(uint16 animationIndex) {
	parseAnimations();
	if (animationIndex >= _animations.size())
		return 0;
	return _animations[animationIndex].frames.size();
}

bool Sprite::drawAnimationFrame(uint16 animationIndex, uint16 frameIndex, Graphics::Surface *target, int x, int y) {
	parseAnimations();
	if (animationIndex >= _animations.size() || frameIndex >= _animations[animationIndex].frames.size())
		return false;

	const Animation &animation = _animations[animationIndex];
	const AnimationFrame &frame = animation.frames[frameIndex];
	for (uint i = 0; i < frame.groups.size(); ++i) {
		const uint16 groupIndex = frame.groups[i];
		if (groupIndex >= _animationGroups.size())
			return false;
		const AnimationGroup &group = _animationGroups[groupIndex];
		for (uint j = 0; j < group.images.size(); ++j) {
			const AnimationImage &image = group.images[j];
	if (!drawFrame(image.frame, target, x + animation.x + image.x, y + animation.y + image.y))
		return false;
		}
	}
	return true;
}

bool Sprite::drawAnimation(uint16 animationIndex, uint32 elapsedMillis, Graphics::Surface *target, int x, int y) {
	const uint16 frames = animationFrameCount(animationIndex);
	if (!frames)
		return false;
	const uint16 frame = (uint16)(((uint64)elapsedMillis * 12 / 1000) % frames);
	return drawAnimationFrame(animationIndex, frame, target, x, y);
}

} // namespace Dune

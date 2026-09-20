/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#include "common/scummsys.h"
#include "common/endian.h"
#include "common/util.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/surface.h"
#include "graphics/paletteman.h"

#include "dune/sprite.h"

namespace Dune {

Sprite::Sprite(OSystem *system, const Common::Array<byte> &data) :
		_system(system), _data(data) {
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
		if (paletteStart > 255 || paletteCount > 256 - paletteStart)
			return false;
		if (position + (uint32)paletteCount * 3 > chunkEnd)
			return false;

		// VGA DAC values are 6-bit; ScummVM palettes are 8-bit RGB triplets.
		byte palette[256 * 3];
		for (uint i = 0; i < (uint)paletteCount * 3; ++i) {
			const byte value = _data[position++] & 0x3f;
			// The DOS VGA DAC stores 6-bit components. Cryo promotes them by
			// four; replicating the high bits changes the recorded palette.
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

	return tableSize / 2;
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

	if (frameIndex >= tableSize / 2)
		return false;

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
		uint scale, byte paletteOverride) {
	uint32 dataOffset;
	uint16 width;
	uint16 height;
	bool compressed;
	int8 headerPaletteOffset;
	if (!getFrameInfo(frameIndex, dataOffset, width, height, compressed, headerPaletteOffset))
		return false;
	if (width > 320 || height > 200)
		return false;

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

	// Room sprites can be shrunk by one of eight fixed factors.
	static const uint16 scaleFactors[8] = { 0x100, 0x120, 0x140, 0x160, 0x180, 0x1c0, 0x200, 0x280 };
	const uint32 factor = scaleFactors[scale & 7];
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

} // namespace Dune

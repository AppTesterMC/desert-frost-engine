/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this program.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "common/file.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "dune/resource.h"

namespace Dune {

namespace {

class BitByteReader {
public:
	BitByteReader(const byte *data, uint32 size) : _data(data), _size(size), _position(0), _bits(0), _queue(0) {
	}

	bool getBit(byte &value) {
		if (_bits == 0) {
			if (_position + 2 > _size)
				return false;
			_queue = READ_LE_UINT16(_data + _position);
			_position += 2;
			_bits = 16;
		}

		value = _queue & 1;
		_queue >>= 1;
		--_bits;
		return true;
	}

	bool getByte(byte &value) {
		if (_position >= _size)
			return false;
		value = _data[_position++];
		return true;
	}

private:
	const byte *_data;
	uint32 _size;
	uint32 _position;
	uint16 _bits;
	uint16 _queue;
};

} // namespace

bool Resource::load(const Common::String &requestedName, Common::Array<byte> &data) const {
	Common::String name = requestedName;
	name.toUppercase();

	Common::File file;
	const bool hasArchive = _useArchive && file.open("DUNE.DAT");
	uint32 originalSize = 0;

	if (hasArchive) {
		const int64 archiveSize = file.size();
		if (archiveSize < 2)
			return false;

		const uint16 entries = file.readUint16LE();
		if (entries == 0 || entries > 4096)
			return false;

		bool found = false;
		uint32 dataOffset = 0;
		for (uint16 i = 0; i < entries; ++i) {
			char entryName[17];
			if (file.read(entryName, 16) != 16)
				return false;
			entryName[16] = '\0';

			const uint32 entrySize = file.readUint32LE();
			const uint32 entryOffset = file.readUint32LE();
			file.readByte(); // Reserved byte.

			Common::String normalizedName(entryName);
			normalizedName.toUppercase();
			if (normalizedName == name) {
				found = true;
				originalSize = entrySize;
				dataOffset = entryOffset;
				break;
			}
		}

		if (!found || dataOffset > archiveSize || originalSize > archiveSize - dataOffset)
			return false;

		file.seek(dataOffset);
	} else {
		if (!file.open(Common::Path(name)))
			return false;
		if (file.size() > 0xffffffffLL)
			return false;
		originalSize = (uint32)file.size();
	}

	data.resize(originalSize);
	if (originalSize && file.read(data.data(), originalSize) != originalSize)
		return false;
	file.close();

	if (data.size() < 6)
		return true;

	byte checksum = 0;
	for (uint i = 0; i < 6; ++i)
		checksum += data[i];

	// HSQ files use a six-byte header and a salt byte that makes this sum
	// equal to 0xAB. Uncompressed files pass through unchanged.
	if (checksum != 0xAB)
		return true;

	const uint32 unpackedSize = READ_LE_UINT16(data.data());
	const uint32 packedSize = READ_LE_UINT16(data.data() + 3);
	if (data[2] != 0 || packedSize != data.size() || packedSize < 6)
		return false;

	Common::Array<byte> unpacked(unpackedSize);
	if (!unpackHSQ(data.data() + 6, packedSize - 6, unpacked.data(), unpackedSize))
		return false;

	data.swap(unpacked);
	return true;
}

bool Resource::unpackHSQ(const byte *packed, uint32 packedSize, byte *unpacked, uint32 unpackedSize) {
	BitByteReader reader(packed, packedSize);
	uint32 outputPosition = 0;

	while (true) {
		byte bit;
		if (!reader.getBit(bit))
			return false;

		if (bit) {
			byte literal;
			if (!reader.getByte(literal) || outputPosition >= unpackedSize)
				return false;
			unpacked[outputPosition++] = literal;
			continue;
		}

		uint32 count;
		int32 offset;
		if (!reader.getBit(bit))
			return false;

		if (bit) {
			byte first;
			byte second;
			if (!reader.getByte(first) || !reader.getByte(second))
				return false;

			count = first & 0x7;
			offset = ((first >> 3) | (second << 5)) - 0x2000;
			if (count == 0) {
				byte extendedCount;
				if (!reader.getByte(extendedCount))
					return false;
				count = extendedCount;
				if (count == 0)
					break;
			}
		} else {
			if (!reader.getBit(bit))
				return false;
			count = bit * 2;
			if (!reader.getBit(bit))
				return false;
			count += bit;

			byte distance;
			if (!reader.getByte(distance))
				return false;
			offset = (int32)distance - 256;
		}

		count += 2;
		if (outputPosition > unpackedSize || count > unpackedSize - outputPosition)
			return false;

		const int32 sourcePosition = (int32)outputPosition + offset;
		if (sourcePosition < 0 || (uint32)sourcePosition >= outputPosition)
			return false;

		int32 source = sourcePosition;
		for (uint32 i = 0; i < count; ++i)
			unpacked[outputPosition++] = unpacked[source++];
	}

	return outputPosition == unpackedSize;
}

} // namespace Dune

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

/*
 * The Amiga release's formats, turned into the DOS layouts the rest of the
 * engine reads. Everything here was worked out from the release itself:
 * the installer "disk_to_hd" (which ships with its symbol table) for the
 * disk layout, the 68000 executable "dune" for the data segment and the
 * room sheets, and the files compared with the DOS floppy and CD ones.
 * FINDINGS.md, section "Amiga release", has the evidence.
 */

#include "common/endian.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "dune/amiga.h"
#include "dune/resource.h"

namespace Dune {

namespace {

bool g_amigaRelease = false;

struct AmigaDsRun {
	uint16 cd;    ///< CD data-segment offset
	uint16 amiga; ///< offset in the Amiga data segment
	uint16 count; ///< bytes (kind 0) or fields (kinds 1 and 2)
	byte kind;
};

#include "dune/amiga_ds_table.h"

// Full-screen pictures: 320 pixels, 5 interleaved bitplanes.
enum {
	kPictureWidth = 320,
	kPictureHeight = 152,
	kPicturePlanes = 5,
	kPicturePlaneBytes = kPictureWidth / 8,
	/** Palette byte of the stub sprite header that points at a decoded picture (see sprite.cpp). */
	kPictureStub = 253
};

/** Amiga 12-bit colour to three 6-bit VGA components (v * 63 / 15, rounded). */
void amigaColour(uint16 colour, byte *vga) {
	for (int i = 0; i < 3; ++i) {
		const uint v = (colour >> (8 - 4 * i)) & 0x0f;
		vga[i] = (byte)((v * 63 + 7) / 15);
	}
}

byte swapNibbles(byte b) {
	return (byte)((b << 4) | (b >> 4));
}

/**
 * One 4-bit or 8-bit sprite: the header's word is big-endian and the
 * palette byte comes before the height; 4-bit pixels have the left pixel in
 * the high nibble. Output: the DOS header and pixel order.
 */
bool convertSprite(const Common::Array<byte> &in, uint32 position, Common::Array<byte> &out) {
	if (position + 4 > in.size())
		return false;
	const uint16 head = READ_BE_UINT16(in.data() + position);
	const byte paletteOffset = in[position + 2];
	const byte height = in[position + 3];
	const uint width = head & 0x1ff;
	const bool rle = (head & 0x8000) != 0;
	const bool eightBit = paletteOffset >= 254;
	const uint32 pitch = eightBit ? width : 2 * ((width + 3) / 4);
	out.push_back((byte)(head & 0xff));
	out.push_back((byte)(head >> 8));
	out.push_back(height);
	out.push_back(paletteOffset);
	position += 4;
	if (rle) {
		for (uint line = 0; line < height; ++line) {
			uint32 column = 0;
			while (column < pitch) {
				if (position >= in.size())
					return false;
				const byte command = in[position++];
				out.push_back(command);
				if (command & 0x80) {
					if (position >= in.size())
						return false;
					const byte value = in[position++];
					out.push_back(eightBit ? value : swapNibbles(value));
					column += 257 - command;
				} else {
					const uint32 count = command + 1u;
					if (position + count > in.size())
						return false;
					for (uint32 i = 0; i < count; ++i) {
						const byte value = in[position++];
						out.push_back(eightBit ? value : swapNibbles(value));
					}
					column += count;
				}
			}
		}
	} else {
		const uint32 size = pitch * height;
		if (position + size > in.size())
			return false;
		for (uint32 i = 0; i < size; ++i)
			out.push_back(eightBit ? in[position + i] : swapNibbles(in[position + i]));
	}
	return true;
}

/** One row of 8-bit pixels in the DOS RLE (runs of 3 or more, literals up to 128). */
void encodeRow8(const byte *row, uint width, Common::Array<byte> &out) {
	uint i = 0;
	while (i < width) {
		uint run = 1;
		while (i + run < width && row[i + run] == row[i] && run < 128)
			++run;
		if (run >= 3) {
			out.push_back((byte)(257 - run));
			out.push_back(row[i]);
			i += run;
			continue;
		}
		const uint start = i;
		while (i < width && i - start < 128) {
			uint repeat = 1;
			while (i + repeat < width && row[i + repeat] == row[i] && repeat < 3)
				++repeat;
			if (repeat >= 3)
				break;
			++i;
		}
		out.push_back((byte)(i - start - 1));
		for (uint k = start; k < i; ++k)
			out.push_back(row[k]);
	}
}

/**
 * The executable's second sprite format (flag ds:14a9, blitter at code
 * 0x15238), used by the cinematic sheets and the ornithopter: a word
 * (bit 15 compressed, width in bits 0-8), a word (bit 8 colours 16-31,
 * height in the low byte), then four bitplanes one after the other, each
 * row ((width + 31) / 16) * 2 bytes. Compressed data (code 0x13dd0) is a
 * byte stream: 0 ends it, 1-127 copies 128 - n bytes, 128-255 repeats the
 * next byte 129 - (n & 0x7f) times. Pixel 0 is transparent. Output: a DOS
 * 8-bit RLE sprite with palette byte 255 (colour 0 transparent).
 */
bool convertPlanarSprite(const Common::Array<byte> &in, uint32 position, uint32 end, Common::Array<byte> &out) {
	if (position + 4 > end)
		return false;
	const uint16 head = READ_BE_UINT16(in.data() + position);
	const uint16 second = READ_BE_UINT16(in.data() + position + 2);
	const uint width = head & 0x1ff, height = second & 0xff;
	const uint rowBytes = ((width + 31) >> 4) * 2;
	const uint32 size = rowBytes * height * 4;
	if (!width || !height || width > 320)
		return false;
	Common::Array<byte> planes;
	position += 4;
	if (head & 0x8000) {
		while (true) {
			if (position >= end)
				return false;
			const byte n = in[position++];
			if (!n)
				break;
			if (n & 0x80) {
				if (position >= end)
					return false;
				const byte value = in[position++];
				for (uint k = 0; k < 129u - (n & 0x7f); ++k)
					planes.push_back(value);
			} else {
				const uint count = 128u - n;
				if (position + count > end)
					return false;
				for (uint k = 0; k < count; ++k)
					planes.push_back(in[position++]);
			}
		}
	} else {
		if (position + size > end)
			return false;
		planes = Common::Array<byte>(in.data() + position, size);
	}
	if (planes.size() < size)
		return false;
	const byte base = (second & 0x100) ? 16 : 0;
	out.push_back((byte)(width & 0xff));
	out.push_back((byte)((width >> 8) | 0x80));
	out.push_back((byte)height);
	out.push_back(255);
	Common::Array<byte> row(width);
	for (uint y = 0; y < height; ++y) {
		for (uint x = 0; x < width; ++x) {
			byte v = 0;
			for (uint p = 0; p < 4; ++p)
				if (planes[(p * height + y) * rowBytes + (x >> 3)] & (0x80 >> (x & 7)))
					v |= (byte)(1 << p);
			row[x] = v ? (byte)(v + base) : 0;
		}
		encodeRow8(row.data(), width, out);
	}
	return true;
}

/**
 * A character sheet's animation block: the same layout as the DOS one with
 * big-endian words (the 14-byte header, the image-group offset table and
 * the animation table); the group and frame streams are bytes.
 */
void convertAnimation(const Common::Array<byte> &in, uint32 start, uint32 end, Common::Array<byte> &out) {
	const uint32 base = out.size();
	for (uint32 i = start; i < end; ++i)
		out.push_back(in[i]);
	const uint32 size = end - start;
	auto swapWord = [&](uint32 at) {
		if (at + 2 <= size) {
			const byte t = out[base + at];
			out[base + at] = out[base + at + 1];
			out[base + at + 1] = t;
		}
	};
	if (size < 16)
		return;
	for (uint32 i = 0; i < 14; i += 2)
		swapWord(i);
	const uint32 groupTableSize = READ_BE_UINT16(in.data() + start + 14);
	for (uint32 i = 0; i < groupTableSize && 14 + i + 2 <= size; i += 2)
		swapWord(14 + i);
	// The animation table: as Sprite::parseAnimations() reads it, ascending
	// offsets below 0x100.
	const uint32 definitionOffset = READ_BE_UINT16(in.data() + start + 12);
	if (definitionOffset < 2)
		return;
	const uint32 table = 14 + definitionOffset - 2;
	uint16 last = 0;
	for (uint32 k = 0; table + 2 * k + 2 <= size; ++k) {
		const uint16 next = READ_BE_UINT16(in.data() + start + table + 2 * k);
		if (k && !(last < next && next < 0x100))
			break;
		swapWord(table + 2 * k);
		last = next;
	}
}

/**
 * A time-of-day palette record (SKY, SUNRS, ATTACK, BALCON): a zero word,
 * the size (0x6c), then 54 Amiga colours. The executable (code at 0x5296)
 * copies words 0-13 to colours 2-15, words 14-27 to colours 33-46 (the
 * panel's) and words 28-53, reversed, to the sky gradient the copper paints
 * into colour 1. The DOS record is (zero, size, first, count, RGB...): it
 * holds colours 2-15, so Sprite::setPaletteRecord() applies those; the panel
 * colours and the gradient (top band first) follow as 40 more triplets for
 * amigaSkyRecord().
 */
void convertPaletteRecord(const Common::Array<byte> &in, uint32 start, uint32 end, Common::Array<byte> &out) {
	const uint count = MIN<uint32>((end - start - 4) / 2, 54);
	auto colour = [&](uint word) -> uint16 {
		return word < count ? READ_BE_UINT16(in.data() + start + 4 + 2 * word) : 0;
	};
	const uint16 size = 2 + 54 * 3;
	out.push_back(0);
	out.push_back(0);
	out.push_back((byte)(size & 0xff));
	out.push_back((byte)(size >> 8));
	out.push_back(2);
	out.push_back(14);
	for (uint i = 0; i < 54; ++i) {
		byte vga[3];
		// Words 28-53 hold the gradient bottom band first.
		amigaColour(colour(i < 28 ? i : 28 + (53 - i)), vga);
		out.push_back(vga[0]);
		out.push_back(vga[1]);
		out.push_back(vga[2]);
	}
}

class HsqBodyReader {
public:
	HsqBodyReader(const byte *data, uint32 size) : _data(data), _size(size) {}
	bool bit(byte &value) {
		if (!_bits) {
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
	bool byteValue(byte &value) {
		if (_position >= _size)
			return false;
		value = _data[_position++];
		return true;
	}

private:
	const byte *_data;
	uint32 _size;
	uint32 _position = 0;
	uint16 _queue = 0;
	uint16 _bits = 0;
};

/** HSQ body with an unknown output size (up to @p limit bytes). */
bool unpackHsqBody(const byte *packed, uint32 packedSize, uint32 limit, Common::Array<byte> &out) {
	HsqBodyReader reader(packed, packedSize);
	out.clear();
	while (true) {
		byte b;
		if (!reader.bit(b))
			return false;
		if (b) {
			byte literal;
			if (!reader.byteValue(literal) || out.size() >= limit)
				return false;
			out.push_back(literal);
			continue;
		}
		uint32 count;
		int32 offset;
		if (!reader.bit(b))
			return false;
		if (b) {
			byte first, second;
			if (!reader.byteValue(first) || !reader.byteValue(second))
				return false;
			count = first & 7;
			offset = ((first >> 3) | (second << 5)) - 0x2000;
			if (!count) {
				byte extended;
				if (!reader.byteValue(extended))
					return false;
				if (!extended)
					return true;
				count = extended;
			}
		} else {
			byte high, low, distance;
			if (!reader.bit(high) || !reader.bit(low) || !reader.byteValue(distance))
				return false;
			count = high * 2 + low;
			offset = (int32)distance - 256;
		}
		count += 2;
		const int32 source = (int32)out.size() + offset;
		if (source < 0 || out.size() + count > limit)
			return false;
		for (uint32 i = 0; i < count; ++i)
			out.push_back(out[source + i]);
	}
}

} // namespace

void setAmigaRelease(bool amiga) {
	g_amigaRelease = amiga;
}

bool amigaRelease() {
	return g_amigaRelease;
}

Common::String amigaFileName(const Common::String &pcName) {
	Common::String name = pcName;
	name.toLowercase();
	// English text is the "2" set on this release (the DOS floppy's "1").
	if (name == "command1.hsq")
		return "command2.hsq";
	if (name == "phrase11.hsq")
		return "phrase21.hsq";
	if (name == "phrase12.hsq")
		return "phrase22.hsq";
	if (name == "icones.hsq")
		return "icone.hsq";
	if (name == "dnchar.bin")
		return "dunechar.hsq";
	if (name.hasSuffix(".sal")) {
		name.erase(name.size() - 4);
		return name + ".sam";
	}
	// No HERAD/AdLib music, no HNM video, no digitised sound on the Amiga.
	if (name.hasSuffix(".hnm") || name.hasSuffix(".voc") || name.hasSuffix(".agd") || name.hasSuffix(".m32"))
		return "";
	static const char *const kMissing[] = {
		"arrakis.hsq", "baghdad.hsq", "morning.hsq", "sekence.hsq", "sietchm.hsq", "warsong.hsq",
		"water.hsq", "wormintr.hsq", "wormsuit.hsq", "dunesdb.hsq", "duneadl.hsq", "dunevga.hsq",
		"dunepcs.hsq", "dunemid.hsq", "duneagd.hsq", "dune386.hsq"
	};
	for (uint i = 0; i < ARRAYSIZE(kMissing); ++i)
		if (name == kMissing[i])
			return "";
	return name;
}

bool decodeAmigaPicture(const byte *packed, uint32 packedSize, uint height, Common::Array<byte> &pixels) {
	Common::Array<byte> planar;
	const uint32 rowBytes = kPicturePlaneBytes * kPicturePlanes;
	if (!unpackHsqBody(packed, packedSize, rowBytes * height, planar) || planar.size() != rowBytes * height)
		return false;
	pixels.resize(kPictureWidth * height);
	for (uint y = 0; y < height; ++y) {
		byte *dst = pixels.data() + y * kPictureWidth;
		memset(dst, 0, kPictureWidth);
		for (uint plane = 0; plane < kPicturePlanes; ++plane) {
			const byte *src = planar.data() + (y * kPicturePlanes + plane) * kPicturePlaneBytes;
			for (uint x = 0; x < kPictureWidth; ++x)
				if (src[x >> 3] & (0x80 >> (x & 7)))
					dst[x] |= (byte)(1 << plane);
		}
	}
	return true;
}

bool convertAmigaSheet(const Common::Array<byte> &in, Common::Array<byte> &out, byte paletteShift, bool planar) {
	out.clear();
	if (in.size() < 4)
		return false;
	const uint32 paletteEnd = READ_BE_UINT16(in.data());
	if (paletteEnd < 2 || paletteEnd + 2 > in.size())
		return false;

	// Palette blocks: first colour, count, count big-endian 12-bit colours.
	Common::Array<byte> palette;
	uint32 position = 2;
	while (position + 2 <= paletteEnd) {
		const byte first = in[position], count = in[position + 1];
		position += 2;
		if (first == 0xff && count == 0xff)
			break;
		if (position + count * 2u > paletteEnd)
			return false;
		palette.push_back(first);
		palette.push_back(count);
		for (uint i = 0; i < count; ++i) {
			byte vga[3];
			amigaColour(READ_BE_UINT16(in.data() + position), vga);
			position += 2;
			palette.push_back(vga[0]);
			palette.push_back(vga[1]);
			palette.push_back(vga[2]);
		}
	}
	palette.push_back(0xff);
	palette.push_back(0xff);

	// The offset table, relative to its start; entry 0 is also its size.
	const uint32 table = paletteEnd;
	const uint32 entries = READ_BE_UINT16(in.data() + table) / 2;
	if (!entries || table + entries * 2 > in.size())
		return false;
	Common::Array<uint32> starts, sorted;
	for (uint32 i = 0; i < entries; ++i) {
		const uint32 start = table + READ_BE_UINT16(in.data() + table + 2 * i);
		starts.push_back(MIN<uint32>(start, in.size()));
		sorted.push_back(MIN<uint32>(start, in.size()));
	}
	sorted.push_back(in.size());
	Common::sort(sorted.begin(), sorted.end());
	auto entryEnd = [&](uint32 start) -> uint32 {
		for (uint i = 0; i < sorted.size(); ++i)
			if (sorted[i] > start)
				return sorted[i];
		return in.size();
	};

	Common::Array<byte> body;
	Common::Array<uint32> bodyOffsets;
	Common::Array<Common::Array<byte> > pictures;
	Common::Array<uint32> pictureStubs; // body offsets of the stubs, parallel to pictures
	for (uint32 i = 0; i < entries; ++i) {
		const uint32 start = starts[i];
		const uint32 end = entryEnd(start);
		bodyOffsets.push_back(body.size());
		if (start + 4 > in.size() || end <= start)
			continue;
		const uint16 head = READ_BE_UINT16(in.data() + start);
		const uint16 second = READ_BE_UINT16(in.data() + start + 2);
		Common::Array<byte> pixels;
		bool picture = false;
		if (head == 0 && second + 4 <= end - start && second > 0x100)
			picture = decodeAmigaPicture(in.data() + start + 4, end - start - 4, kPictureHeight, pixels);
		else if ((head & 0x1ff) > 320 || in[start + 3] > 200)
			picture = decodeAmigaPicture(in.data() + start, end - start, kPictureHeight, pixels);
		if (picture) {
			// A 320x152 8-bit sprite whose pixels follow the sheet; see Sprite::getFrameInfo().
			pictureStubs.push_back(body.size());
			pictures.push_back(pixels);
			const byte stub[8] = { (byte)(kPictureWidth & 0xff), (byte)(kPictureWidth >> 8), kPictureHeight,
				kPictureStub, 0, 0, 0, 0 };
			for (uint k = 0; k < 8; ++k)
				body.push_back(stub[k]);
			continue;
		}
		if (head == 0) {
			if (second == 0x6c)
				convertPaletteRecord(in, start, end, body);
			else
				convertAnimation(in, start, end, body);
			continue;
		}
		const uint32 before = body.size();
		if (planar) {
			if (convertPlanarSprite(in, start, end, body))
				continue;
			body.resize(before);
		}
		if (convertSprite(in, start, body)) {
			if (paletteShift && body[before + 3] < 254)
				body[before + 3] += paletteShift;
		} else {
			// Keep the bytes; the DOS reader rejects what it cannot parse.
			body.resize(before);
			for (uint32 k = start; k < end; ++k)
				body.push_back(in[k]);
		}
	}

	const uint32 paletteChunk = 2 + palette.size();
	const uint32 tableSize = entries * 2;
	if (tableSize + body.size() > 0xffff)
		warning("Dune: Amiga sheet larger than the DOS offsets allow");
	out.resize(paletteChunk);
	WRITE_LE_UINT16(out.data(), (uint16)paletteChunk);
	memcpy(out.data() + 2, palette.data(), palette.size());
	for (uint32 i = 0; i < entries; ++i) {
		const uint32 offset = tableSize + bodyOffsets[i];
		out.push_back((byte)(offset & 0xff));
		out.push_back((byte)(offset >> 8));
	}
	const uint32 bodyStart = out.size();
	for (uint32 i = 0; i < body.size(); ++i)
		out.push_back(body[i]);
	for (uint i = 0; i < pictures.size(); ++i) {
		WRITE_LE_UINT32(out.data() + bodyStart + pictureStubs[i] + 4, out.size());
		for (uint32 k = 0; k < pictures[i].size(); ++k)
			out.push_back(pictures[i][k]);
	}
	return true;
}

bool convertAmigaRooms(const Common::Array<byte> &in, Common::Array<byte> &out) {
	// Same stream as .SAL: an offset table, then per room a marker count and
	// commands until FFFF. Sprite and marker commands are two big-endian
	// words (command; y << 8 | x) and a palette byte. The Amiga files have no
	// polygons or lines: the pictures carry them.
	out = in;
	if (in.size() < 2)
		return false;
	const uint32 rooms = READ_BE_UINT16(in.data()) / 2;
	if (!rooms || rooms * 2 > in.size())
		return false;
	for (uint32 r = 0; r < rooms; ++r) {
		const uint32 start = READ_BE_UINT16(in.data() + 2 * r);
		WRITE_LE_UINT16(out.data() + 2 * r, start);
		uint32 position = start + 1;
		while (position + 2 <= in.size()) {
			const uint16 command = READ_BE_UINT16(in.data() + position);
			if (command == 0xffff)
				break;
			if (command & 0x8000) {
				warning("Dune: unexpected polygon or line in an Amiga room file");
				return false;
			}
			if (position + 5 > in.size())
				return false;
			WRITE_LE_UINT16(out.data() + position, command);
			out[position + 2] = in[position + 3]; // x
			out[position + 3] = in[position + 2]; // y
			position += 5;
		}
	}
	return true;
}

bool convertAmigaResource(const Common::String &amigaName, Common::Array<byte> &data) {
	Common::String name = amigaName;
	name.toLowercase();
	if (name.hasSuffix(".sam")) {
		Common::Array<byte> out;
		if (!convertAmigaRooms(data, out))
			return false;
		data.swap(out);
		return true;
	}
	if (name == "mirror.hsq") {
		// Only a picture (the reflected bedroom in its gilt frame), in the
		// bedroom's colours (POR.HSQ). As a sheet: frame 1 the picture,
		// frames 0 and 2 empty, as the DOS MIRROR.HSQ's frames are used.
		Common::Array<byte> pixels;
		if (!decodeAmigaPicture(data.data(), data.size(), kPictureHeight, pixels))
			return false;
		Common::Array<byte> out;
		const byte head[] = { 4, 0, 0xff, 0xff, 6, 0, 6, 0, 14, 0 };
		for (uint i = 0; i < sizeof(head); ++i)
			out.push_back(head[i]);
		const byte stub[8] = { (byte)(kPictureWidth & 0xff), (byte)(kPictureWidth >> 8), kPictureHeight, kPictureStub,
			0, 0, 0, 0 };
		for (uint i = 0; i < 8; ++i)
			out.push_back(stub[i]);
		for (uint i = 0; i < 4; ++i)
			out.push_back(0); // frame 2: an empty sprite
		WRITE_LE_UINT32(out.data() + out.size() - 8, out.size());
		out.insert_at(out.size(), pixels);
		data.swap(out);
		return true;
	}
	// Text, dialogue and map data are byte-identical in layout to the DOS files.
	static const char *const kPlain[] = {
		"command2.hsq", "condit.hsq", "dialogue.hsq", "phrase21.hsq", "phrase22.hsq", "globdata.hsq",
		"map.hsq", "map2.hsq", "dunechar.hsq", "tablat.bin", "m1.hsq", "m2.hsq", "m3.hsq"
	};
	for (uint i = 0; i < ARRAYSIZE(kPlain); ++i)
		if (name == kPlain[i])
			return true;
	if (!name.hasSuffix(".hsq"))
		return true;
	// The control panel's sprites use the second 32 colours: the copper
	// switches the palette below the view (row 152), so their pixels 0-31
	// show colours 32-63.
	const byte shift = name == "icone.hsq" ? 32 : 0;
	// The sheets the executable draws with its planar blitter (see
	// convertPlanarSprite); found by which decoding fits every sprite.
	static const char *const kPlanar[] = {
		"back.hsq", "back1.hsq", "credits.hsq", "fresk.hsq", "orny.hsq", "ornytk.hsq", "shai.hsq", "shai1.hsq",
		"shai2.hsq", "stars.hsq", "stars1.hsq", "ver.hsq"
	};
	bool planar = false;
	for (uint i = 0; i < ARRAYSIZE(kPlanar); ++i)
		planar = planar || name == kPlanar[i];
	Common::Array<byte> out;
	if (!convertAmigaSheet(data, out, shift, planar))
		return false;
	data.swap(out);
	return true;
}

Common::String amigaRoomSheet(byte roomCode) {
	// The file order of the Amiga disks from index 0x13 (dir.0 / disk_to_hd).
	static const char *const kSlots[14] = {
		"por.hsq", "prouge.hsq", "comm.hsq", "equi.hsq", "balcon.hsq", "corr.hsq", "siet0.hsq", "sas.hsq",
		"dunes2.hsq", "fort.hsq", "bunk.hsq", "harko.hsq", "serre.hsq", "bota.hsq"
	};
	const uint code = (uint)(byte)(roomCode - 1);
	const uint slot = code >> 4, room = code & 0x0f;
	if (slot == 14)
		return Common::String::format("siet%u.hsq", room);      // file 0x3c + room
	if (slot == 15)
		return Common::String::format("vilg%u.hsq", room + 1);  // file 0x49 + room
	return kSlots[slot];
}

// ---- The executable ----------------------------------------------------------

bool loadAmigaHunks(const Common::Array<byte> &file, Common::Array<byte> &code, Common::Array<uint32> &relocations) {
	code.clear();
	relocations.clear();
	uint32 p = 0;
	auto u32 = [&](uint32 &v) -> bool {
		if (p + 4 > file.size())
			return false;
		v = READ_BE_UINT32(file.data() + p);
		p += 4;
		return true;
	};
	uint32 v;
	if (!u32(v) || v != 0x3f3)
		return false;
	do {
		if (!u32(v))
			return false;
	} while (v);
	uint32 count, first, last;
	if (!u32(count) || !u32(first) || !u32(last) || last < first)
		return false;
	for (uint32 i = first; i <= last; ++i)
		if (!u32(v))
			return false;
	int hunk = -1;
	while (p + 4 <= file.size()) {
		if (!u32(v))
			break;
		v &= 0x3fffffff;
		if (v == 0x3e9 || v == 0x3ea) {
			uint32 longs;
			if (!u32(longs) || p + longs * 4 > file.size())
				return false;
			++hunk;
			if (hunk == 0)
				code = Common::Array<byte>(file.data() + p, longs * 4);
			p += longs * 4;
		} else if (v == 0x3eb) {
			if (!u32(v))
				return false;
			++hunk;
		} else if (v == 0x3ec) {
			while (true) {
				uint32 n, target;
				if (!u32(n))
					return false;
				if (!n)
					break;
				if (!u32(target))
					return false;
				for (uint32 i = 0; i < n; ++i) {
					if (!u32(v))
						return false;
					if (hunk == 0 && target == 0)
						relocations.push_back(v);
				}
			}
		} else if (v == 0x3f0) {
			while (true) {
				uint32 n;
				if (!u32(n))
					return false;
				if (!n)
					break;
				p += n * 4 + 4;
			}
		} else if (v == 0x3f1) {
			if (!u32(v))
				return false;
			p += v * 4;
		} else if (v == 0x3f2) {
			continue;
		} else {
			break;
		}
	}
	Common::sort(relocations.begin(), relocations.end());
	return !code.empty();
}

bool amigaInitialDataSegment(const Common::Array<byte> &code, const Common::Array<uint32> &relocations, byte *vars,
		uint size, int &scriptDelta) {
	// The data segment starts with rand_bits and game_time; the location
	// word 0x200a, 0x0180 and 0x2000 0x000a follow (the CD's first bytes,
	// big-endian).
	static const byte kHead[8] = { 0x20, 0x0a, 0x01, 0x80, 0x20, 0x00, 0x00, 0x0a };
	int base = -1;
	for (uint32 p = 4; p + sizeof(kHead) <= code.size(); p += 2)
		if (!memcmp(code.data() + p, kHead, sizeof(kHead))) {
			base = (int)p - 4;
			break;
		}
	if (base < 0)
		return false;
	const byte *am = code.data() + base;
	const uint32 amSize = code.size() - base;
	memset(vars, 0, size);

	auto isRelocated = [&](uint32 amigaOffset) -> bool {
		const uint32 at = amigaOffset + base;
		uint lo = 0, hi = relocations.size();
		while (lo < hi) {
			const uint mid = (lo + hi) / 2;
			if (relocations[mid] < at)
				lo = mid + 1;
			else
				hi = mid;
		}
		return lo < relocations.size() && relocations[lo] == at;
	};
	// Amiga byte offset -> CD offset, through the byte runs.
	auto toCd = [&](uint32 amigaOffset, uint16 &cd) -> bool {
		for (uint i = 0; i < ARRAYSIZE(kAmigaDsRuns); ++i) {
			const AmigaDsRun &r = kAmigaDsRuns[i];
			if (r.kind == 0 && amigaOffset >= r.amiga && amigaOffset < (uint32)r.amiga + r.count) {
				cd = (uint16)(r.cd + (amigaOffset - r.amiga));
				return true;
			}
		}
		return false;
	};

	for (uint i = 0; i < ARRAYSIZE(kAmigaDsRuns); ++i) {
		const AmigaDsRun &r = kAmigaDsRuns[i];
		for (uint k = 0; k < r.count; ++k) {
			if (r.kind == 0) {
				if (r.cd + k < size && r.amiga + k < amSize)
					vars[r.cd + k] = am[r.amiga + k];
			} else if (r.kind == 1) {
				const uint cd = r.cd + 2 * k, a = r.amiga + 2 * k;
				if (cd + 2 <= size && a + 2 <= amSize)
					WRITE_LE_UINT16(vars + cd, READ_BE_UINT16(am + a));
			} else {
				const uint cd = r.cd + 2 * k, a = r.amiga + 4 * k;
				if (cd + 2 > size || a + 4 > amSize || !isRelocated(a))
					continue;
				uint16 target;
				if (toCd(READ_BE_UINT32(am + a) - (uint32)base, target))
					WRITE_LE_UINT16(vars + cd, target);
			}
		}
	}
	for (uint i = 0; i < sizeof(kAmigaDsConst) && kAmigaDsConstStart + i < size; ++i)
		vars[kAmigaDsConstStart + i] = kAmigaDsConst[i];
	// The executable sets game_time to 2 before the first frame (code 0x010a).
	if (!READ_LE_UINT16(vars + 2))
		WRITE_LE_UINT16(vars + 2, 2);

	// The scripted scenes: the prospector's map lesson (CD code 0x12f8).
	static const byte kLesson[] = { 0x0e, 0x10, 0xff, 0x00, 0x02, 0x03, 0x05, 0x07, 0x06, 0x08, 0x02, 0x07 };
	scriptDelta = 0;
	for (uint32 p = 0; p + sizeof(kLesson) <= code.size(); ++p)
		if (!memcmp(code.data() + p, kLesson, sizeof(kLesson))) {
			scriptDelta = (int)p - 0x12f8;
			break;
		}
	return true;
}

} // namespace Dune

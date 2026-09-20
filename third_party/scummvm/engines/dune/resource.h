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

#ifndef ENGINES_DUNE_RESOURCE_H
#define ENGINES_DUNE_RESOURCE_H

#include "common/array.h"
#include "common/str.h"

namespace Dune {

/**
 * Reader for the two resource layouts used by the original DOS release.
 *
 * The CD release stores named files inside DUNE.DAT. The floppy release
 * stores the same files directly beside the executable. Individual files
 * may use Cryo's HSQ bit-stream compression.
 */
class Resource {
public:
	explicit Resource(bool useArchive) : _useArchive(useArchive) {}

	bool load(const Common::String &name, Common::Array<byte> &data) const;

	/** Decode an HSQ bit stream (without its six-byte header). */
	static bool unpackHSQ(const byte *packed, uint32 packedSize, byte *unpacked, uint32 unpackedSize);

private:
	bool _useArchive;
};

} // namespace Dune

#endif // ENGINES_DUNE_RESOURCE_H

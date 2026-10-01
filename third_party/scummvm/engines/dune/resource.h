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

#ifndef ENGINES_DUNE_RESOURCE_H
#define ENGINES_DUNE_RESOURCE_H

#include "common/array.h"
#include "common/str.h"

namespace Dune {

class SegaCdArchive;

/**
 * Reader for the two resource layouts used by the original DOS release.
 *
 * The CD release stores named files inside DUNE.DAT. The floppy release
 * stores the same files directly beside the executable. Individual files
 * may use Cryo's HSQ bit-stream compression.
 */
class Resource {
public:
	explicit Resource(bool useArchive, bool amiga = false) : _useArchive(useArchive), _amiga(amiga), _segaCd(nullptr), _language(1) {}

	/**
	 * Sega CD: serve the PC names that have a counterpart on the disc
	 * (SegaCdArchive::loadNamed) instead of reading DOS files.
	 */
	void setSegaCd(const SegaCdArchive *archive) { _segaCd = archive; }
	bool segaCd() const { return _segaCd != nullptr; }

	bool load(const Common::String &name, Common::Array<byte> &data) const;
	/** Physical bank access for the language-independent command identifiers. */
	bool loadUntranslated(const Common::String &name, Common::Array<byte> &data) const;

	/** Select a complete DOS text bank before constructing the panel/intro. */
	bool setLanguage(uint language);
	uint language() const { return _language; }

	/** Decode an HSQ bit stream (without its six-byte header). */
	static bool unpackHSQ(const byte *packed, uint32 packedSize, byte *unpacked, uint32 unpackedSize);

	bool amiga() const { return _amiga; }
	bool dosFloppy() const { return !_useArchive && !_amiga && !_segaCd; }

private:
	/** The Amiga release: its own file names, converted to the DOS layouts (amiga.cpp). */
	bool loadAmiga(const Common::String &name, Common::Array<byte> &data) const;

	bool _useArchive;
	bool _amiga;
	const SegaCdArchive *_segaCd;
	uint _language;
};

} // namespace Dune

#endif // ENGINES_DUNE_RESOURCE_H

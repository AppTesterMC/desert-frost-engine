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

#ifndef ENGINES_DUNE_AMIGA_H
#define ENGINES_DUNE_AMIGA_H

#include "common/array.h"
#include "common/str.h"

class OSystem;

namespace Graphics {
struct Surface;
}

namespace Dune {

class Resource;

/**
 * The Amiga release (Cryo/Virgin 1992, three disks, English "v1.0").
 *
 * The engine plays it by turning its data into the DOS layouts at load time,
 * so the decoders and the game logic stay release-neutral:
 *   - files: the loose files the original installer (disk_to_hd) writes, or
 *     scripts/dune_amiga_extract.py writes from the ADFs; lower case names,
 *     English text in the "2" files (command2, phrase21/22);
 *   - sprite sheets: big-endian words, 12-bit Amiga colours, high nibble
 *     first; full-screen pictures (320x152, 5 interleaved bitplanes, an HSQ
 *     body without header) instead of the DOS polygon rooms;
 *   - rooms (.SAM): the .SAL command stream with big-endian words;
 *   - the data segment: in the 68000 executable, big-endian, with 32-bit
 *     pointers and even-padded records; converted to the CD layout.
 * FINDINGS.md ("Amiga release") has the details and the evidence.
 */

/** Set once at start-up from the detection entry. */
void setAmigaRelease(bool amiga);
bool amigaRelease();

/** The Amiga file behind a DOS resource name, or "" when the release has none. */
Common::String amigaFileName(const Common::String &pcName);

/**
 * Convert an Amiga file (already un-HSQ'd) into the DOS layout of the same
 * resource. Files that need no conversion (text, dialogue, map) are left as
 * they are. Returns false when the data does not parse.
 */
bool convertAmigaResource(const Common::String &amigaName, Common::Array<byte> &data);

/**
 * Big-endian sprite sheet to the DOS layout (see FINDINGS.md). @p paletteShift
 * is added to the 4-bit sprites' palette offsets (32 for the panel's sheet);
 * @p planar selects the executable's planar sprite format (convertPlanarSprite).
 */
bool convertAmigaSheet(const Common::Array<byte> &in, Common::Array<byte> &out, byte paletteShift = 0,
		bool planar = false);
/** A .SAM room file to the .SAL layout. */
bool convertAmigaRooms(const Common::Array<byte> &in, Common::Array<byte> &out);
/**
 * Decode a full-screen picture: an HSQ bit stream (no header) of 5
 * interleaved bitplanes, 320 pixels wide, into 8-bit pixels (colours 0-31).
 */
bool decodeAmigaPicture(const byte *packed, uint32 packedSize, uint height, Common::Array<byte> &pixels);

/** The first hunk (code and data) of an AmigaDOS executable and its relocations to it. */
bool loadAmigaHunks(const Common::Array<byte> &file, Common::Array<byte> &code, Common::Array<uint32> &relocations);

/**
 * Fill a CD-layout data segment (@p size bytes) from the Amiga executable's
 * first hunk. @p scriptDelta receives where the scripted scenes lie relative
 * to their CD code offsets. Returns false when the data is not found.
 */
bool amigaInitialDataSegment(const Common::Array<byte> &code, const Common::Array<uint32> &relocations, byte *vars,
		uint size, int &scriptDelta);

/**
 * The sprite sheet of a room code (the room byte of the room tables). The
 * Amiga executable (code at 0x5546) adds 0x13 to the sheet slot, except for
 * slot 14 (the sietch rooms, SIET1-12 by room) and 15 (the villages, VILG1-6).
 */
Common::String amigaRoomSheet(byte roomCode);

// ---- Screen effects (amiga_gfx.cpp) ------------------------------------------

/** Where the sky gradient's 26 colours go in the 256-colour palette. */
enum { kAmigaSkyGradient = 64 };

/** The SKY.HSQ record of a game time (code 0x5254). */
uint amigaSkyRecord(uint16 gameTime);
/**
 * Apply the time-of-day record: the landscape colours 2-15 (when
 * @p landscape: the sietch and fortress exteriors and the desert), the
 * panel's 33-46 and the sky gradient.
 */
bool amigaSkyPalette(OSystem *system, Resource &resources, uint16 gameTime, bool landscape);
/**
 * Copy the Amiga interface colours (33-44 ramp, 47-63 fixed) to the DOS
 * interface indices 224-255 the shared drawing code uses.
 */
void amigaMirrorUiColours(OSystem *system);
/** Repaint colour 1 of the top @p rows lines with the gradient, as the copper does. */
void amigaSkyGradient(Graphics::Surface &target, uint rows = 152);
/**
 * The open desert's view where the DOS code draws SKY.HSQ and the DUNES
 * landscape: the Amiga draws DUNES3 pieces (code 0x5094, not ported), so the
 * time of day's sky gradient over its sand colour (2) stands in.
 */
void amigaDesertView(OSystem *system, Resource &resources, Graphics::Surface &view, uint16 gameTime);

} // namespace Dune

#endif // ENGINES_DUNE_AMIGA_H

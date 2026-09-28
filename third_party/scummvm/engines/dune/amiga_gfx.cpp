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
 * The Amiga release's screen effects that the DOS release does with
 * palettes: the sky is colour 1 of the pictures, repainted by the copper
 * every three lines from a 26-entry gradient; the time-of-day records also
 * carry the landscape colours (2-15) and the panel's (33-46).
 */

#include "common/endian.h"
#include "common/system.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/amiga.h"
#include "dune/resource.h"
#include "dune/sprite.h"

namespace Dune {

uint amigaSkyRecord(uint16 gameTime) {
	// Code 0x5254: the table at data 0x2c61 by time slot (sunrise 8, day 9,
	// sunset 10, night 11), plus four records per day of the week.
	static const byte kSlotRecord[16] = { 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 11, 11, 11 };
	return kSlotRecord[gameTime & 15] + ((gameTime >> 2) & 0x1c);
}

bool amigaSkyPalette(OSystem *system, Resource &resources, uint16 gameTime, bool landscape) {
	Common::Array<byte> data;
	if (!resources.load("SKY.HSQ", data) || data.size() < 4)
		return false;
	// The converted record (amiga.cpp, convertPaletteRecord): zero, size,
	// first 2, count 14, then 54 triplets: colours 2-15, 33-46, the gradient.
	const uint32 table = READ_LE_UINT16(data.data());
	const uint entry = amigaSkyRecord(gameTime);
	if (table + 2 * entry + 2 > data.size())
		return false;
	const uint32 record = table + READ_LE_UINT16(data.data() + table + 2 * entry);
	if (record + 6 + 54 * 3 > data.size() || READ_LE_UINT16(data.data() + record) != 0)
		return false;
	const byte *rgb6 = data.data() + record + 6;
	byte rgb[54 * 3];
	for (uint i = 0; i < sizeof(rgb); ++i)
		rgb[i] = (byte)((rgb6[i] & 0x3f) << 2);
	// Code 0x5296: colours 2-15 unless ds:1582 (the palace and the villages
	// keep their pictures' own), then the panel's 33-46 and the gradient.
	if (landscape)
		system->getPaletteManager()->setPalette(rgb, 2, 14);
	system->getPaletteManager()->setPalette(rgb + 14 * 3, 33, 14);
	system->getPaletteManager()->setPalette(rgb + 28 * 3, kAmigaSkyGradient, 26);
	return true;
}

void amigaSkyGradient(Graphics::Surface &target, uint rows) {
	// The copper list (code 0x13782): colour 1 changes every three lines
	// from the top of the view; the 26th entry holds from line 75 down.
	for (uint y = 0; y < rows && y < (uint)target.h; ++y) {
		byte *row = (byte *)target.getBasePtr(0, y);
		const byte colour = (byte)(kAmigaSkyGradient + MIN<uint>(y / 3, 25));
		for (int x = 0; x < target.w; ++x)
			if (row[x] == 1)
				row[x] = colour;
	}
}

void amigaDesertView(OSystem *system, Resource &resources, Graphics::Surface &view, uint16 gameTime) {
	view.fillRect(Common::Rect(0, 0, view.w, MIN<int>(78, view.h)), 1);
	if (view.h > 78)
		view.fillRect(Common::Rect(0, 78, view.w, view.h), 2);
	amigaSkyPalette(system, resources, gameTime, true);
	amigaSkyGradient(view);
	amigaMirrorUiColours(system);
}

void amigaMirrorUiColours(OSystem *system) {
	// The DOS interface colours are the room sheet's ramp at 240-254 and
	// PERS's reds and blues at 224-239. The Amiga keeps the panel's ramp
	// at 33-44 (from the room sheet or the time of day) and fixed interface
	// colours at 47-63 (POR.HSQ). Copy them where the DOS drawing expects them.
	// The fixed interface colours 47-63, as POR.HSQ (the first room) sets
	// them; no other sheet changes them.
	static const uint16 kInterface[17] = {
		0x5f5, 0xaaa, 0x555, 0xffc, 0xf03, 0x400, 0x22c, 0x55f, 0xff7, 0xfd1, 0xf91, 0xd61, 0xc41, 0x930, 0x620, 0x000, 0xfff
	};
	byte fixed[17 * 3];
	for (uint i = 0; i < 17; ++i)
		for (uint c = 0; c < 3; ++c)
			fixed[3 * i + c] = (byte)(((kInterface[i] >> (8 - 4 * c)) & 15) * 17);
	system->getPaletteManager()->setPalette(fixed, 47, 17);
	byte amiga[32 * 3];
	system->getPaletteManager()->grabPalette(amiga, 32, 32);
	auto colour = [&](uint index) -> const byte * { return amiga + 3 * (index - 32); };
	byte ui[32 * 3];
	// 224 red, 225 black, 226-234 dark red to orange, 235-239 dark to light blue.
	static const byte kFixed[16] = { 51, 62, 52, 61, 61, 60, 60, 59, 58, 57, 57, 53, 53, 53, 54, 54 };
	for (uint i = 0; i < 16; ++i)
		memcpy(ui + 3 * i, colour(kFixed[i]), 3);
	// 240-254: the ramp, 255 white.
	for (uint k = 0; k < 15; ++k)
		memcpy(ui + 3 * (16 + k), colour(33 + (k * 11 + 7) / 14), 3);
	memcpy(ui + 3 * 31, colour(63), 3);
	system->getPaletteManager()->setPalette(ui, 224, 32);
}

} // namespace Dune

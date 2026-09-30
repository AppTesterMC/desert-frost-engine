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
 * The ecology route: the live map's stage bits, the ecology troops' jobs
 * (wind trap, bulbs, irrigation), the vegetation discs that turn the land
 * Atreides and take the fortresses without a battle, and the equipment the
 * troops carry. Transcribed from the CD executable with capstone
 * (scripts/dune_disasm.py; notes/research/disasm-vegetation-63f0.txt,
 * disasm-ecology-jobs-7660.txt, disasm-7443.txt); names from madmoose's
 * dune-chani. See notes/research/gameplay-rules.md, "Ecology".
 */

#include "dune/world.h"

#include "common/util.h"

#include "dune/debug.h"
#include "dune/resource.h"

namespace Dune {

namespace {

enum {
	kWater = 27,        ///< location byte 0x1b: water, or the wind trap's assembly progress
	kBulbs = 26,        ///< location byte 0x1a
	kDiscRadius = 11,   ///< 0x0b
	kDiscLongitude = 12,///< 0x0c (word)
	kDiscLatitude = 14, ///< 0x0e (the byte; 0x0f carries the irrigation progress)
	kProgress = 15,
	kDensity = 18,
	kStatusVegetation = 0x01,
	kStatusAtreides = 0x08,
	kStatusWindTrap = 0x20,
	kBulbProgress = 0xec ///< bulb_growing_progress
};

} // namespace

void World::raiseSkill(uint id, uint skillClass, byte amount) {
	// seg000:6edd: skill byte 0x16 + class (spice, army, ecology), capped at
	// 0x5f; a new high nibble shows as "ability increased" (bitfield_10 bits 0-1).
	byte &skill = troopByte(id, 0x16 + skillClass);
	const byte before = skill;
	skill = (byte)MIN<uint>(0x5f, skill + amount);
	if ((skill ^ before) & 0xf0) {
		uint16 bits = READ_LE_UINT16(&troopByte(id, 0x10));
		bits = (uint16)((bits & ~3) | (skillClass + 1));
		WRITE_LE_UINT16(&troopByte(id, 0x10), bits);
	}
}

bool World::loadTablat() {
	// TABLAT.BIN: 8 bytes per latitude row, 0..98.
	if (_tablat.size() >= 8 * 99)
		return true;
	return _resources.load("TABLAT.BIN", _tablat) && _tablat.size() >= 8 * 99;
}

Common::Array<byte> &World::map() {
	if (_map.size() < kMapCells) {
		if (!_resources.load("MAP.HSQ", _map) || _map.size() < kMapCells - 3) {
			_log.line("World: MAP.HSQ missing");
			_map.clear();
		}
		_map.resize(kMapCells, 0);
		markPlaceCells();
	}
	return _map;
}

void World::resetMap() {
	_map.clear();
	map();
}

void World::markPlaceCells() {
	if (_map.size() < kMapCells)
		return;
	for (uint i = 0; i < locationCount(); ++i) {
		const uint16 offset = READ_LE_UINT16(&_state.vars[Location::kTableOffset + i * Location::kRecordSize + 6]);
		if (offset && offset < kMapCells)
			_map[offset] |= 0x40;
	}
}

int World::mapCell(uint16 longitude, int16 latitude) const {
	// map_func (seg000:b58b): the row by |latitude| from TABLAT (offset,
	// half the cells), the cell under the longitude rounded.
	const uint row = (uint)ABS(latitude);
	if ((row + 1) * 8 > _tablat.size())
		return -1;
	const int rowOffset = READ_BE_UINT16(_tablat.data() + 8 * row);
	const uint cells = 2u * READ_BE_UINT16(_tablat.data() + 8 * row + 2);
	if (!cells)
		return -1;
	const uint32 product = (uint32)longitude * cells;
	uint cell = (product >> 16) + ((product >> 15) & 1);
	if (cell >= cells)
		cell -= cells;
	const int offset = (int)kMapCentre + (latitude < 0 ? -rowOffset : rowOffset) + (int)cell;
	return offset >= 0 && offset < (int)kMapCells ? offset : -1;
}

byte World::cellStage(uint16 longitude, int16 latitude) const {
	const int o = const_cast<World *>(this)->loadTablat() ? mapCell(longitude, latitude) : -1;
	Common::Array<byte> &m = const_cast<World *>(this)->map();
	return o >= 0 && (uint)o < m.size() ? (m[o] & 0x30) : 0;
}

template<typename F>
void World::forDisc(uint16 longitude, int16 latitude, uint radius, int limit, F cell) {
	// The filled circle of seg000:64b2: for each row dy of the disc, the
	// span of cells around the centre's column, wrapping round the row.
	if (!loadTablat())
		return;
	Common::Array<byte> &m = map();
	const int r = (int)radius;
	for (int dy = -r; dy <= r; ++dy) {
		const int lat = latitude + dy;
		if (lat > limit || lat < -limit)
			continue;
		const uint row = (uint)ABS(lat);
		if ((row + 1) * 8 > _tablat.size())
			continue;
		const int rowOffset = READ_BE_UINT16(_tablat.data() + 8 * row);
		const int cells = 2 * READ_BE_UINT16(_tablat.data() + 8 * row + 2);
		if (!cells)
			continue;
		const int rowStart = (int)kMapCentre + (lat < 0 ? -rowOffset : rowOffset);
		const uint32 product = (uint32)longitude * (uint32)cells;
		const int centre = (int)(((product >> 16) + ((product >> 15) & 1)) % (uint)cells);
		int half = 0;
		while ((half + 1) * (half + 1) + dy * dy <= r * r)
			++half;
		for (int dx = -half; dx <= half; ++dx) {
			const int o = rowStart + (((centre + dx) % cells) + cells) % cells;
			if (o >= 0 && o < (int)m.size())
				cell((uint)o, m[o]);
		}
	}
}

bool World::friendlyPlace(uint index) const {
	// seg000:5d36: carry set for a sietch or village (appearance < 0x28,
	// the cmp's own carry) or an Atreides-held place (status bit 3).
	const Location l = location(index);
	return l.type < 0x28 || (l.status & kStatusAtreides);
}

void World::countHostiles(uint index, uint &harkonnen, uint &attacking) const {
	// seg000:5082 over the place's troops: Harkonnen (bitfield bit 7) and
	// Fremen attacking it (occupation 6); captured ones do not count.
	harkonnen = attacking = 0;
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		const byte *r = _state.vars + kTroopTable + (ids[i] - 1) * kTroopSize;
		if (r[3] & 0x20)
			continue;
		if (r[16] & 0x80)
			++harkonnen;
		else if (r[3] == 6)
			++attacking;
	}
}

void World::runEcologyJob(uint id, uint index) {
	byte *t = _state.vars + kTroopTable + (id - 1) * kTroopSize;
	byte *l = _state.vars + Location::kTableOffset + index * Location::kRecordSize;
	switch (t[3] & 0x0f) {
	case Troop::kBulbGrowing:
		// seg000:767d: bulbs where there are none grow over 256 periods
		// (bulb_growing_progress ds:ec), 16 at once; then the troop irrigates.
		if (!l[kBulbs]) {
			const byte p = (byte)(_state.b(kBulbProgress) + 1);
			_state.setB(kBulbProgress, p);
			if (p)
				return;
			l[kBulbs] = 0x10;
			_log.line(Common::String::format("Ecology: 16 bulbs grown at place %u", index));
		}
		setTroopOccupation(id, Troop::kIrrigation);
		return;
	case Troop::kWindTrap: {
		// seg000:7711: assembly progress in the water byte until it wraps.
		if (!(l[10] & kStatusWindTrap)) {
			t[0x15] = (byte)MIN<uint>(100, t[0x15] + 1); // troop_increase_motivation (6f48)
			// troop_0348a: min(255, 2 x motivation + byte 0x16) x men >> 12, at least 1.
			uint v = MIN<uint>(255, 2 * motivationModifier(id) + t[0x16]);
			uint gain = (v * t[26]) >> 12;
			if (!gain && t[26] >= 1)
				gain = 1;
			const uint water = l[kWater] + gain;
			if (water <= 0xff) {
				l[kWater] = (byte)water;
				return;
			}
			l[10] |= kStatusWindTrap;
			l[8] |= 8;
			l[kWater] = 5;
			_log.line(Common::String::format("Ecology: wind-trap assembled at place %u", index));
		}
		// troop_clear_occupation_bits_0_and_1 (6ac5): back to irrigation.
		t[3] = (byte)((t[3] & 0xfc));
		return;
	}
	case Troop::kIrrigation: {
		// seg000:7693. Not viable (6bd7): a sulking troop, no water, no wind
		// trap, or no bulbs carried.
		if ((t[3] & Troop::kStopped) || (READ_LE_UINT16(t + 0x12) & 0x30) || l[kWater] < 1 ||
				!(l[10] & kStatusWindTrap) || !(t[25] & 2)) {
			t[3] |= Troop::kStopped;
			return;
		}
		WRITE_LE_UINT16(t + 0x10, READ_LE_UINT16(t + 0x10) | 0x100);
		if (!(l[10] & kStatusVegetation)) {
			// The first vegetation: no more spice here; a disc of radius 4
			// round the place.
			l[10] |= kStatusVegetation;
			l[kDensity] = 0;
			WRITE_LE_UINT16(l + kDiscLongitude, READ_LE_UINT16(l + 2));
			l[kDiscLatitude] = l[4];
			l[kProgress] = 0;
			l[kDiscRadius] = 4;
			_state.setB(0xfa, 1); // vegetation_started_on_Dune
			_log.line(Common::String::format("Ecology: vegetation starts at place %u", index));
			spreadVegetation(index);
			return;
		}
		troopNewDay(id, index); // 76cb call 6e20 (floppy 842f)
		// The ecology skill / 4 (at least 1) fills the progress byte; each
		// wrap raises the skill, spends 12 water, widens the disc (up to 12)
		// and moves it 2 north (not past -82).
		uint step = t[0x18] >> 2;
		if (!step)
			step = 1;
		const uint progress = l[kProgress] + step;
		l[kProgress] = (byte)progress;
		if (progress <= 0xff)
			return;
		raiseSkill(id, 2, 1);
		if (l[kWater] < 12) {
			l[kWater] = 0;
			t[3] |= Troop::kStopped; // callback_troop_make_troop_stop_working (7085)
			WRITE_LE_UINT16(t + 0x10, READ_LE_UINT16(t + 0x10) & ~0x100);
			return;
		}
		l[kWater] -= 12;
		if (l[kDiscRadius] < 12)
			++l[kDiscRadius];
		const int lat = (int8)l[kDiscLatitude] - 2;
		if (lat >= -82)
			l[kDiscLatitude] = (byte)(int8)lat;
		spreadVegetation(index);
		return;
	}
	default:
		return;
	}
}

void World::spreadVegetation(uint index) {
	// seg000:6515 with the callback at 653a: every cell of the disc not yet
	// sprouting becomes Atreides land, a quarter of the sand ones sprout
	// (the rotating mask 0x44 on terrain < 8); a place's cell under it loses
	// its spice, and a Harkonnen fortress there falls (on the CD not the two
	// palaces; the floppy takes the Harkonnen palace as well).
	const byte *l = _state.vars + Location::kTableOffset + index * Location::kRecordSize;
	byte mask = 0x44;
	Common::Array<uint> fallen;
	forDisc(READ_LE_UINT16(l + kDiscLongitude), (int8)l[kDiscLatitude], l[kDiscRadius], 0x56, [&](uint o, byte &c) {
		if ((c & 0x30) == 0x10)
			return;
		if (c & 0x40) {
			for (uint i = 0; i < locationCount(); ++i) {
				byte *p = _state.vars + Location::kTableOffset + i * Location::kRecordSize;
				if (READ_LE_UINT16(p + 6) != o)
					continue;
				p[kDensity] = 0;
				// The CD (6582: cmp di, 138h) spares the two palaces; the
				// floppy (72da-7326) has no such test, so there the
				// vegetation takes the Harkonnen palace too (location 1,
				// type 0x30 >= 0x28): it is held, the ending opens to Paul.
				if (!friendlyPlace(i) && (i >= 2 || floppy())) {
					p[10] &= 0x7f;
					fallen.push_back(i);
				}
				break;
			}
		}
		const byte terrain = c & 0x0e;
		byte v = (byte)((c & 0xcf) | 0x20);
		if (terrain < 8) {
			const bool carry = (mask & 0x80) != 0;
			mask = (byte)((mask << 1) | (carry ? 1 : 0));
			if (carry)
				v = (byte)((v & 0xcf) | 0x10);
		}
		c = v;
	});
	for (uint i = 0; i < fallen.size(); ++i) {
		_log.line(Common::String::format("Ecology: the vegetation takes place %u from the Harkonnens", fallen[i]));
		fortressWon(fallen[i]); // seg000:658c (floppy 7326): the fortress-won routine itself
	}
}

void World::paintArea(uint index, byte stage, uint radius) {
	// seg000:6447 / 644e: the area round a place becomes Harkonnen (0x30) or
	// Atreides (0x20) land; sprouting cells keep their vegetation.
	const Location l = location(index);
	forDisc(l.longitude, (int16)l.latitude, radius, 0x5d, [&](uint, byte &c) {
		if ((c & 0x30) != 0x10)
			c = (byte)((c & 0xcf) | stage);
	});
}

void World::ecologyNewDay() {
	// seg000:63f0, pass 1: water behind every wind trap grows by 1 + half the
	// sprouting cells round the place, up to 250.
	Common::Array<byte> &m = map();
	for (uint i = 0; i < locationCount(); ++i) {
		byte *l = _state.vars + Location::kTableOffset + i * Location::kRecordSize;
		if (!(l[10] & kStatusWindTrap) || l[kWater] >= 0xfa)
			continue;
		const uint o = READ_LE_UINT16(l + 6);
		uint near = 0;
		for (uint k = 0; k < 6; ++k)
			if (o + k >= 1 && o + k - 1 < m.size() && (m[o + k - 1] & 0x30) == 0x10)
				++near;
		l[kWater] = (byte)MIN<uint>(0xfa, l[kWater] + 1 + near / 2);
	}
	// Pass 2 (seg000:65b6): 0x46 steps of the LFSR (taps 0x402); each column
	// visited is walked down the map every 0x7ff bytes, and sprouting cells
	// (0x10) grow into tufts (0x20). The executable keeps the state in its
	// code segment (cs:65b4, 1 at start), unsaved; so does World.
	uint16 state = _ecologyLfsr ? _ecologyLfsr : 1;
	for (uint n = 0; n < 0x46; ++n) {
		const bool carry = state & 1;
		state >>= 1;
		if (carry)
			state ^= 0x402;
		for (uint o = state; o < 0xc5f9 && o < m.size(); o += 0x7ff)
			if ((m[o] & 0x30) == 0x10)
				m[o] = (byte)((m[o] & 0xcf) | 0x20);
	}
	_ecologyLfsr = state;
}

void World::computeAreas() {
	// seg000:bfe3.
	Common::Array<byte> &m = map();
	uint all = 0, atreides = 0, harkonnen = 0;
	for (uint i = 0; i < 0xc5f9 && i < m.size(); ++i) {
		const byte s = m[i] & 0x30;
		if (s == 0x30)
			++harkonnen;
		else if (s)
			++atreides;
	}
	all = 0xc5f9 - 0x188 + 1;
	_state.setW(0xa2, (uint16)(atreides * 100 / all + 1));
	_state.setW(0xa4, (uint16)(harkonnen * 100 / all));
}

void World::placeFreeEquipment(uint index, byte counts[7]) const {
	// seg000:7f27: the place's stock (bytes 20-26) less what its troops hold.
	const byte *l = _state.vars + Location::kTableOffset + index * Location::kRecordSize;
	for (uint k = 0; k < 7; ++k)
		counts[k] = l[20 + k];
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		const byte eq = _state.vars[kTroopTable + (ids[i] - 1) * kTroopSize + 25];
		for (uint k = 0; k < 7; ++k)
			if ((eq & (0x80 >> k)) && counts[k])
				--counts[k];
	}
}

bool World::takeEquipment(uint id, uint type) {
	const Troop t = troop(id);
	if (!t.id || type >= 7 || t.location < Location::kTableOffset)
		return false;
	const uint index = (t.location - Location::kTableOffset) / Location::kRecordSize;
	byte counts[7];
	placeFreeEquipment(index, counts);
	byte &eq = troopByte(id, 25);
	if ((eq & (0x80 >> type)) || !counts[type])
		return false;
	eq |= (byte)(0x80 >> type);
	return true;
}

bool World::giveEquipment(uint id, uint type) {
	if (id < 1 || id > kTroops || type >= 7)
		return false;
	byte &eq = troopByte(id, 25);
	if (!(eq & (0x80 >> type)))
		return false;
	eq &= (byte)~(0x80 >> type);
	return true;
}

} // namespace Dune

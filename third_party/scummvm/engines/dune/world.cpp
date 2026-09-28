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
 * The data-segment layout comes from madmoose's dune-rust (crates/savegame)
 * and OpenRakis' annotated DNCDPRG_RECENT.ASM: the location records at
 * 0x100 (sub_15274, sub_15344), the character records at 0xfd8 (sub_193DF,
 * sub_19F40), the room tables and their pointer table at DS:0x13c4
 * (sub_13EFE, sub_13F27, _sub_15E4F_calc_SAL_index), and the player's
 * position bytes 4-11.
 */

#include "dune/world.h"

#include "common/endian.h"
#include "common/file.h"
#include "common/util.h"

#include "dune/debug.h"
#include "dune/resource.h"
#include "dune/text.h"

namespace Dune {

// The first bytes of the data segment: time 2, throne room (0x200a), the
// position bytes. Identical on both releases.
static const byte kDataSegmentHead[12] = { 0x00, 0x00, 0x02, 0x00, 0x0a, 0x20, 0x80, 0x01, 0x20, 0x00, 0x00, 0x0a };
// The palace room table's first record (the balcony), see palace.h.
static const byte kPalaceTableHead[5] = { 76, 2, 0, 253, 0 };

World::World(GameState &state, Resource &resources, StartupLog &log) :
		_state(state), _resources(resources), _log(log), _palaceTable(0), _pointerTable(0), _shift(0),
		_tablesFound(false), _floppy(false), _harvestRemainder(0) {
	seedRandom();
}

// ---- LZEXE --------------------------------------------------------------------

namespace {

struct LzexeBits {
	const Common::Array<byte> &data;
	uint position;
	uint16 buffer;
	uint count;
	bool ok;

	LzexeBits(const Common::Array<byte> &d, uint start) : data(d), position(start), buffer(0), count(0), ok(true) {
		reload();
	}
	void reload() {
		if (position + 2 > data.size()) {
			ok = false;
			buffer = 0;
		} else {
			buffer = READ_LE_UINT16(data.data() + position);
		}
		position += 2;
		count = 16;
	}
	uint bit() {
		const uint b = buffer & 1;
		if (--count == 0)
			reload();
		else
			buffer >>= 1;
		return b;
	}
	byte next() {
		if (position >= data.size()) {
			ok = false;
			return 0;
		}
		return data[position++];
	}
};

} // namespace

// The scheme of LZEXE 0.91 (Fabrice Bellard, 1989) as documented by
// unlzexe: a bit says literal (1) or match; a second bit picks a short
// match (2-5 bytes, 8-bit distance) or a long one (13-bit distance, length
// in the low bits with an escape for longer runs, the segment marker and
// the end).
bool unpackLzexe(const Common::Array<byte> &packed, Common::Array<byte> &unpacked) {
	unpacked.clear();
	if (packed.size() < 32 || packed[0] != 'M' || packed[1] != 'Z' || memcmp(packed.data() + 28, "LZ91", 4) != 0)
		return false;
	const uint headerParagraphs = READ_LE_UINT16(packed.data() + 8);
	const uint codeSegment = READ_LE_UINT16(packed.data() + 22);
	const uint loader = (headerParagraphs + codeSegment) * 16;
	if (loader + 16 > packed.size())
		return false;
	const uint packedParagraphs = READ_LE_UINT16(packed.data() + loader + 8);
	if (packedParagraphs * 16 > loader)
		return false;
	LzexeBits bits(packed, loader - packedParagraphs * 16);
	unpacked.reserve(loader * 2);
	for (;;) {
		if (!bits.ok)
			return false;
		if (bits.bit()) {
			unpacked.push_back(bits.next());
			continue;
		}
		int length;
		int distance;
		if (!bits.bit()) {
			length = (int)(bits.bit() << 1);
			length |= (int)bits.bit();
			length += 2;
			distance = (int)bits.next() - 256;
		} else {
			const byte low = bits.next();
			const byte high = bits.next();
			distance = (int)(((high & 0xf8) << 5) | low) - 8192;
			length = (high & 7) + 2;
			if (length == 2) {
				length = bits.next();
				if (length == 0)
					break; // end of the stream
				if (length == 1)
					continue; // segment change
				++length;
			}
		}
		if ((int)unpacked.size() + distance < 0)
			return false;
		for (int i = 0; i < length; ++i)
			unpacked.push_back(unpacked[unpacked.size() + distance]);
	}
	return !unpacked.empty();
}

// ---- initial data -------------------------------------------------------------

bool World::readExecutable(const char *name, Common::Array<byte> &image) const {
	Common::File file;
	if (!file.open(Common::Path(name)))
		return false;
	Common::Array<byte> raw;
	raw.resize(file.size());
	if (file.read(raw.data(), raw.size()) != raw.size())
		return false;
	if (raw.size() >= 32 && memcmp(raw.data() + 28, "LZ91", 4) == 0) {
		if (!unpackLzexe(raw, image)) {
			_log.line(Common::String::format("World: %s could not be unpacked (LZEXE)", name));
			return false;
		}
		return true;
	}
	image = raw;
	return true;
}

bool World::loadInitialData() {
	static const char *const executables[] = { "DUNEPRG.EXE", "DNCDPRG.EXE" };
	Common::Array<byte> image;
	uint found = 0;
	const char *used = nullptr;
	for (uint i = 0; i < ARRAYSIZE(executables) && !used; ++i) {
		if (!readExecutable(executables[i], image))
			continue;
		for (uint p = 0; p + sizeof(kDataSegmentHead) <= image.size(); ++p) {
			if (memcmp(image.data() + p, kDataSegmentHead, sizeof(kDataSegmentHead)) == 0) {
				found = p;
				used = executables[i];
				_floppy = i == 0;
				break;
			}
		}
	}
	if (!used) {
		_log.line("World: no executable with the initial data found; starting from an empty state");
		return findTables();
	}
	const uint available = MIN<uint>(GameState::kSize, image.size() - found);
	memcpy(_state.vars, image.data() + found, available);
	findSceneScripts(image);
	_log.line(Common::String::format("World: initial data from %s at %u (%u bytes)", used, found, available));
	return findTables();
}

void World::findSceneScripts(const Common::Array<byte> &image) {
	// The scripted scenes (the "Continue..." sequences, seg000:1707) live in
	// the code segment. The phase-scene handler (seg000:a1f7) loads four of
	// their offsets as immediates: mov bl,[2a]; mov ax,X; cmp bl,14h; jb;
	// mov ax,..; cmp bl,18h; ... The first, 0x12f8 on the CD (0x16c2 on the
	// floppy), is the prospector's map lesson, whose bytes are 0e 10 ff; that
	// fixes where the code segment starts in the image (512 for the CD file,
	// 0 for the unpacked floppy program).
	_scriptDelta = 0;
	_scriptBase = -1;
	static const byte kHead[] = { 0x8a, 0x1e, 0x2a, 0x00, 0xb8 };
	for (uint p = 0; p + 32 <= image.size(); ++p) {
		if (memcmp(image.data() + p, kHead, sizeof(kHead)) || image[p + 7] != 0x80 || image[p + 8] != 0xfb ||
				image[p + 9] != 0x14)
			continue;
		const uint16 x = READ_LE_UINT16(image.data() + p + 5);
		const uint headerSize = image.size() > 0x20 && image[0] == 'M' && image[1] == 'Z' ?
				READ_LE_UINT16(image.data() + 8) * 16u : 0u;
		const uint candidates[2] = { 0, headerSize };
		for (uint c = 0; c < 2; ++c) {
			const uint at = candidates[c] + x;
			if (at + 3 <= image.size() && image[at] == 0x0e && image[at + 1] == 0x10 && image[at + 2] == 0xff) {
				_scriptBase = (int)candidates[c];
				_scriptDelta = (int)x - 0x12f8;
				break;
			}
		}
		if (_scriptBase >= 0)
			break;
	}
	if (_scriptBase < 0) {
		_log.line("World: the scripted scenes were not found in the executable");
		return;
	}
	_code = image;
	_log.line(Common::String::format("World: scripted scenes at code offset delta %d", _scriptDelta));
}

bool World::sceneScript(uint16 cdOffset, Common::Array<byte> &bytes) const {
	bytes.clear();
	if (_scriptBase < 0)
		return false;
	const int at = _scriptBase + cdOffset + _scriptDelta;
	if (at < 0 || (uint)at >= _code.size())
		return false;
	for (uint i = 0; i < 96 && (uint)at + i < _code.size(); ++i)
		bytes.push_back(_code[at + i]);
	return true;
}

uint16 World::phaseSceneScript() const {
	// seg000:a1f7 (dialogue event 3).
	const byte phase = _state.b(GameState::kPhase);
	return phase < 0x14 ? 0x12f8 : phase < 0x18 ? 0x134f : phase < 0x30 ? 0x1370 : 0x12db;
}

bool World::findTables() {
	_tablesFound = false;
	_palaceTable = _pointerTable = 0;
	_shift = 0;
	for (uint p = 0x1000; p + sizeof(kPalaceTableHead) <= GameState::kSize; ++p) {
		if (memcmp(_state.vars + p, kPalaceTableHead, sizeof(kPalaceTableHead)) == 0) {
			_palaceTable = p;
			break;
		}
	}
	if (!_palaceTable) {
		_log.line("World: palace room table not found in the data segment");
		return false;
	}
	for (uint p = _palaceTable + 60; p + 2 <= GameState::kSize; p += 1) {
		if (READ_LE_UINT16(_state.vars + p) == _palaceTable && p >= 0x40) {
			_pointerTable = p - 2 * Location::kPalace;
			break;
		}
	}
	if (!_pointerTable) {
		_log.line("World: room pointer table not found in the data segment");
		return false;
	}
	_shift = (int)_palaceTable - 0x1225;
	_state.nameTable = (uint16)(GameState::kNameTable + _shift);
	_tablesFound = true;
	_log.line(Common::String::format("World: room tables at %#x, pointers at %#x, layout shift %d", _palaceTable,
			_pointerTable, _shift));
	return true;
}

// ---- locations ----------------------------------------------------------------

uint World::locationCount() const {
	uint count = 0;
	while (count < 80) {
		const uint offset = Location::kTableOffset + count * Location::kRecordSize;
		if (offset + 2 > GameState::kSize || (_state.vars[offset] == 0xff && _state.vars[offset + 1] == 0xff))
			break;
		++count;
	}
	return count;
}

Location World::location(uint index) const {
	Location location;
	memset(&location, 0, sizeof(location));
	const uint o = Location::kTableOffset + index * Location::kRecordSize;
	if (o + Location::kRecordSize > GameState::kSize)
		return location;
	const byte *r = _state.vars + o;
	location.firstName = r[0];
	location.lastName = r[1];
	location.longitude = READ_LE_UINT16(r + 2);
	location.latitude = (int16)READ_LE_UINT16(r + 4);
	location.type = r[8];
	location.troop = r[9];
	location.status = r[10];
	location.discoverPhase = r[11];
	location.spiceField = r[16];
	location.spiceAmount = r[17];
	location.spiceDensity = r[18];
	location.mapOffset = READ_LE_UINT16(r + 6);
	location.harvesters = r[20];
	location.ornithopters = r[21];
	location.knives = r[22];
	location.guns = r[23];
	location.modules = r[24];
	location.atomics = r[25];
	location.bulbs = r[26];
	location.water = r[27];
	return location;
}

void World::setOrnithopters(uint index, int delta) {
	const uint o = Location::kTableOffset + index * Location::kRecordSize + 21;
	if (o < GameState::kSize)
		_state.vars[o] = (byte)CLIP<int>(_state.vars[o] + delta, 0, 255);
}

void World::setLocationStatus(uint index, byte status) {
	const uint o = Location::kTableOffset + index * Location::kRecordSize + 10;
	if (o < GameState::kSize)
		_state.vars[o] = status;
}

Common::String World::locationName(uint index, const SentenceBank &sentences) const {
	const Location l = location(index);
	if (!l.firstName)
		return "";
	// Sietch names pair a first name (COMMAND 0-11) with a second one (12-22);
	// the palaces show "(Atreides)" and "(Harkonnen)".
	return sentences.command(l.firstName - 1) + "-" + sentences.command(11 + l.lastName);
}

uint World::currentLocation() const {
	const byte plusOne = _state.b(7);
	return plusOne ? plusOne - 1 : 0;
}

byte World::placeType() const {
	return _state.b(GameState::kLocationAndRoom + 1);
}

uint World::room() const {
	return _state.b(GameState::kLocationAndRoom);
}

void World::setPosition(uint locationIndex, uint room) {
	const Location l = location(locationIndex);
	_state.setB(GameState::kLocationAndRoom, (byte)room);
	_state.setB(GameState::kLocationAndRoom + 1, l.type);
	_state.setB(7, (byte)(locationIndex + 1));
	_state.setB(8, l.type);
	_state.setB(0x0b, (byte)room);
	// The name table words 1 and 2 feed the text codes 0x81 and 0x82
	// ("Welcome to \x81-\x82"): 1-based COMMAND ids.
	_state.setW(nameTableOffset() + 2, l.firstName);
	_state.setW(nameTableOffset() + 4, (uint16)(12 + l.lastName));
}

// ---- rooms --------------------------------------------------------------------

bool World::roomTable(byte placeType, Common::Array<RoomRecord> &rooms) const {
	rooms.clear();
	if (!_tablesFound || placeType >= kPointerTableEntries)
		return false;
	const uint start = READ_LE_UINT16(_state.vars + _pointerTable + 2 * placeType);
	// The tables sit back to back; the next pointer in address order ends
	// this one. The sietch tables overlap on purpose (windows into one list).
	uint end = GameState::kSize;
	for (uint i = 0; i < kPointerTableEntries; ++i) {
		const uint other = READ_LE_UINT16(_state.vars + _pointerTable + 2 * i);
		if (other > start && other < end)
			end = other;
	}
	if (placeType == Location::kPalace)
		end = MIN<uint>(end, start + 12 * 5);
	for (uint p = start; p + 5 <= end && p + 5 <= GameState::kSize; p += 5) {
		RoomRecord record;
		record.code = _state.vars[p];
		for (uint e = 0; e < 4; ++e)
			record.exits[e] = _state.vars[p + 1 + e];
		if (record.code == 0xff)
			break;
		rooms.push_back(record);
	}
	return !rooms.empty();
}

const char *World::salFile(byte placeType) {
	// _sub_15E4F_calc_SAL_index: sietch, palace, village, Harkonnen.
	if (placeType <= Location::kSietchMax)
		return "SIET.SAL";
	if (placeType == Location::kPalace)
		return "PALACE.SAL";
	if (placeType <= Location::kVillageMax)
		return "VILG.SAL";
	return "HARK.SAL";
}

Common::String World::sheetFor(const RoomRecord &room) const {
	// Resource ids 0x13 + slot of the executable's sprite-sheet list. The
	// releases differ where the CD replaced the exteriors by videos: its
	// slot 0 is GENERIC.HSQ (the arrival video's last picture is the
	// backdrop) and POR sits at slot 6, while the floppy draws SIET0, VILG
	// and FORT from slots 6, 8 and 9 and names POR through slot 0 (the
	// palace's codes 1-4). Slot 9 is "libre" on the CD.
	static const char *const kCdSheets[16] = {
		"GENERIC.HSQ", "PROUGE.HSQ", "COMM.HSQ", "EQUI.HSQ", "BALCON.HSQ", "CORR.HSQ", "POR.HSQ", "SIET1.HSQ",
		"XPLAIN9.HSQ", nullptr, "BUNK.HSQ", "FINAL.HSQ", "SERRE.HSQ", "BOTA.HSQ", "PALPLAN.HSQ", "SUN.HSQ"
	};
	static const char *const kFloppySheets[16] = {
		"POR.HSQ", "PROUGE.HSQ", "COMM.HSQ", "EQUI.HSQ", "BALCON.HSQ", "CORR.HSQ", "SIET0.HSQ", "SIET1.HSQ",
		"VILG.HSQ", "FORT.HSQ", "BUNK.HSQ", "FINAL.HSQ", "SERRE.HSQ", "BOTA.HSQ", "PALPLAN.HSQ", "SUN.HSQ"
	};
	const char *name = (_floppy ? kFloppySheets : kCdSheets)[room.sheetSlot()];
	return name ? name : "";
}

const char *World::arrivalVideo(byte placeType) {
	// RESOURCE_LIST_HNM entries 6-10 in the CD executable: SIET, PALACE,
	// PALACE, FORT, FORT for the five place kinds of sub_15E4F.
	if (placeType <= Location::kSietchMax)
		return "SIET.HNM";
	if (placeType <= Location::kVillageMax)
		return "PALACE.HNM";
	return "FORT.HNM";
}

// ---- characters ---------------------------------------------------------------

Character World::character(uint index) const {
	Character c;
	memset(&c, 0, sizeof(c));
	const uint o = kCharacterTable + index * kCharacterSize;
	if (index >= kCharacters || o + kCharacterSize > GameState::kSize)
		return c;
	c.room = _state.vars[o];
	c.placeType = _state.vars[o + 1];
	c.locationPlusOne = _state.vars[o + 3];
	c.index = _state.vars[o + 14];
	c.flags = _state.vars[o + 15];
	return c;
}

bool World::characterInRoom(uint index) const {
	const uint o = kCharacterTable + index * kCharacterSize;
	if (index >= kCharacters || o + 4 > GameState::kSize)
		return false;
	// loc_136EE compares the record's two first words with ds:4 and ds:6:
	// (room, place type) and (0x80, location + 1). A record with 0xFF as
	// its location is somewhere else (Thufir and Duncan at the start).
	// The Leto loop (dune_fix_leto_loop, off by default): the Duke's death
	// (phase 0x4c, CD sub_11166, floppy phase callback) moves Jessica and
	// queues vision 0x105 but never clears Leto's record, so the original
	// keeps him in the throne room, listed and talking. With the fix he is
	// in no room from then on.
	if (index == 0 && _fixLetoLoop && _state.b(GameState::kPhase) >= 0x4c)
		return false;
	return _state.vars[o] == _state.b(GameState::kLocationAndRoom) &&
		   _state.vars[o + 1] == _state.b(GameState::kLocationAndRoom + 1) &&
		   _state.vars[o + 2] == _state.b(6) && _state.vars[o + 3] == _state.b(7);
}

void World::peopleInRoom(Common::Array<byte> &people) const {
	people.clear();
	// sal_read_position_markers: persons_in_room ^ persons_travelling_with -
	// the companions (ds:10) stand wherever Paul is.
	const uint16 with = _state.w(GameState::kPersonsWith);
	for (uint c = 0; c < kCharacters; ++c)
		if (c != kFremen && c != kFremenChief && (characterInRoom(c) || ((with >> c) & 1)))
			people.push_back((byte)c);
	// sub_13127: at a sietch, each Fremen troop of the place stands in room
	// 2 (room 1 when ds:0x2B is set, not handled): a troop not hired yet
	// through character 14's record, a hired one's chief through 15's.
	if (placeType() <= Location::kSietchMax && room() == 2) {
		Common::Array<uint> ids;
		troopsAt(currentLocation(), ids);
		bool fremen = false;
		uint chiefs = 0;
		for (uint i = 0; i < ids.size(); ++i) {
			const Troop t = troop(ids[i]);
			if (t.harkonnen())
				continue;
			if (t.hired())
				++chiefs;
			else
				fremen = true;
		}
		if (fremen)
			people.push_back(kFremen);
		// Further chiefs are groups 16, 17, ... (drawn as 15, sub_13D2F).
		for (uint k = 0; k < chiefs; ++k)
			people.push_back((byte)(kFremenChief + k));
	}
	// init_room_persons (seg000:3157): at a village of appearance 0x21 the
	// smuggler (character 13, record ds:10a8) stands in every room.
	if (placeType() == Location::kVillageMin) {
		bool listed = false;
		for (uint i = 0; i < people.size(); ++i)
			listed |= people[i] == kSmuggler;
		if (!listed)
			people.push_back(kSmuggler);
	}
	// seg000:316e: a defeated Harkonnen troop at a fortress stands in its
	// room 3 as the Harkonnen captain, character 12 (record ds:1098).
	if (placeType() >= Location::kFortressMin && placeType() <= Location::kFortressMax && room() == 3 &&
			captainTroop(currentLocation())) {
		bool listed = false;
		for (uint i = 0; i < people.size(); ++i)
			listed |= people[i] == kCaptain;
		if (!listed)
			people.push_back(kCaptain);
	}
}

void World::stageSmugglers(uint index) {
	// seg000:2318 / 235f: the village's smugglers are the record (six of 17
	// bytes from ds:10d8) whose first byte is the place's first name; it
	// becomes ds:10b4, its fields staged for the conditions: ds:1c its flags,
	// ds:1d byte 1, ds:20 its bill (+0e), ds:1f days since the bill (+10),
	// and ds:1e days since the last visit (+3), 1 on the first.
	const byte first = locationByte(index, 0);
	uint p = ds(0x10d8);
	for (uint k = 0; k < 8 && _state.vars[p] != 0xff; ++k, p += 17) {
		if (_state.vars[p] != first)
			continue;
		WRITE_LE_UINT16(&_state.vars[ds(0x10b4)], (uint16)p);
		_state.setB(0x1c, _state.vars[p + 2]);
		const uint16 bill = READ_LE_UINT16(&_state.vars[p + 0x0e]);
		_state.setW(0x20, bill);
		const byte today = (byte)(_state.w(GameState::kGameTime) >> 4);
		_state.setB(0x1f, bill ? (byte)(today - _state.vars[p + 0x10]) : 0);
		_state.setB(0x1d, _state.vars[p + 1]);
		byte since = (byte)(today - _state.vars[p + 3]);
		if (!(_state.vars[p + 2] & 8)) {
			since = 1;
			_state.vars[p + 2] |= 8;
		}
		_state.setB(0x1e, since);
		return;
	}
}

uint World::captainTroop(uint index) const {
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		const Troop t = troop(ids[i]);
		if (t.harkonnen() && (t.occupation & 0x20))
			return ids[i];
	}
	return 0;
}

void World::prepareCaptain() {
	// seg000:932e: the captain names the fort he knows of: the one kept in his
	// record (+0c), or else the nearest hidden fort within 30 cells, which he
	// remembers; that place is staged for the conditions (ds:11ce), and
	// his dialogue event reveals it (speaker 12, event 8).
	const uint id = captainTroop(currentLocation());
	if (!id)
		return;
	uint16 known = READ_LE_UINT16(&troopByte(id, 0x0c));
	if (!known) {
		uint dist;
		const int fort = nearestHiddenHarkonnen(currentLocation(), dist);
		if (fort < 0 || dist >= 0x1e)
			return;
		known = placeOffset((uint)fort);
		WRITE_LE_UINT16(&troopByte(id, 0x0c), known);
	}
	if (known >= Location::kTableOffset)
		stageLocationForConditions((known - Location::kTableOffset) / Location::kRecordSize);
	_log.line(Common::String::format("Story: the Harkonnen captain (troop %u) knows of place %u", id,
			(known - Location::kTableOffset) / Location::kRecordSize));
}

void World::addCharisma(uint amount) {
	_state.setB(kCharisma, (byte)MIN<uint>(200, _state.b(kCharisma) + amount));
}

uint World::contactRange() const {
	const uint o = ds(0x1176);
	return READ_LE_UINT16(&_state.vars[o]);
}

uint World::raiseContactRange() {
	uint16 range = (uint16)contactRange();
	if (_state.b(0x0a) & 2) {
		addCharisma(0x28);
		range = (uint16)(0xffce + 0x14);
	} else if (range == 1) {
		addCharisma(10);
		range = 10 + 0x14;
	} else {
		range = (uint16)(range + 0x14);
	}
	WRITE_LE_UINT16(&var(0x1176), range);
	_state.setB(0xd5, range >= 100 ? 0 : (byte)(0x80 - (range & 0xff) / 6));
	return range;
}

void World::revealPointedPlace(uint cdOffset) {
	const uint16 pointer = READ_LE_UINT16(&var(cdOffset));
	if (pointer < Location::kTableOffset || (pointer - Location::kTableOffset) % Location::kRecordSize)
		return;
	const uint index = (pointer - Location::kTableOffset) / Location::kRecordSize;
	if (index < locationCount())
		_state.vars[Location::kTableOffset + index * Location::kRecordSize + 10] &= 0x7f;
}

void World::phaseCallback(byte phase, uint16 &cutscene, uint16 &vision) {
	cutscene = vision = 0;
	// The characters' records (0xfd8 + 16 i): room, place type, 0x80, location + 1.
	auto character = [&](uint index) -> byte * { return _state.vars + kCharacterTable + index * kCharacterSize; };
	auto reveal = [&](std::initializer_list<uint> places) {
		for (uint p : places)
			markDiscovered(p);
	};
	enum { Leto, Jessica, Thufir, Duncan, Gurney, Stilgar, Kynes, Chani, Harah };
	switch (phase) {
	case 0x04: // sub_11011
		--var(0x122a);
		reveal({ 10, 17 });
		break;
	case 0x08: // sub_11027: the passage from room 2 to room 12 opens
		var(0x122e) &= 0x7f;
		break;
	case 0x0c: // sub_1102f: the communication room's doors; the gathering scene
		var(0x124a) &= 0x7f;
		var(0x1247) &= 0x7f;
		WRITE_LE_UINT16(&var(0x121d), 0xffff);
		cutscene = 0x1321;
		break;
	case 0x10: // sub_11045
		character(Leto)[0] = 5;
		character(Jessica)[0] = 9;
		addSighting(0x10b); // seg000:104d: the Emperor's first message
		break;
	case 0x14: // sub_11053
		character(Leto)[0] = 0x0a;
		reveal({ 21, 22, 23 });
		break;
	case 0x1c: // sub_110a4
		var(0x1245) &= 0x7f;
		WRITE_LE_UINT16(&var(0x1217), 0xffff);
		break;
	case 0x20: // sub_110b2: Thufir comes to the palace, and falls into 0x28's place
		character(Thufir)[3] = 1;
		reveal({ 64 });
		break;
	case 0x28: // sub_110b8: Sihaya-Clam on the map
		reveal({ 64 });
		break;
	case 0x2c: // sub_110be: Stilgar met
		WRITE_LE_UINT16(&var(0x1154), _state.w(GameState::kGameTime));
		WRITE_LE_UINT16(character(Gurney), 0x2006);
		WRITE_LE_UINT16(character(Gurney) + 2, 0x0180);
		WRITE_LE_UINT16(character(Thufir), 0x2008);
		WRITE_LE_UINT16(character(Thufir) + 2, 0x0180);
		character(Jessica)[0] = 0x0a;
		WRITE_LE_UINT16(character(Jessica) + 2, 0x0180);
		WRITE_LE_UINT16(&var(0x1201), 0x0109);
		addCharisma(0x14);
		var(0x0a) |= 0x10;
		reveal({ 45, 44, 46, 48, 49 });
		break;
	case 0x30: // sub_11103: "Something terrible has happened in the palace!"
		queueVision(4); // seg000:1103
		addSighting(0x1409); // the Baron's message (variant 0x14)
		break;
	case 0x34: // sub_1110f
		if (character(Jessica)[0] != 8) {
			character(Jessica)[0] = 0x0a;
			WRITE_LE_UINT16(character(Jessica) + 2, 0x0180);
		}
		break;
	case 0x38: // sub_11127: the Duke leaves on his expedition
		character(Leto)[3] = 0xff;
		break;
	case 0x40: // sub_1112d
		character(Harah)[15] |= 2;
		break;
	case 0x44: // sub_11133: Oxtyn-Tabr on the map
		reveal({ 26 });
		break;
	case 0x48: // sub_11139: Chani met
		addCharisma(0x0a);
		cutscene = 0x1313;
		character(Chani)[15] = (byte)((character(Chani)[15] | 0x10) & ~2);
		var(0x1178) = (byte)(_state.b(GameState::kFremenTroops) + 2);
		reveal({ 27, 28, 25, 69 });
		break;
	case 0x4c: // sub_11166: the Duke is killed
		++var(0x1141);
		character(Jessica)[0] = 2;
		WRITE_LE_UINT16(character(Jessica) + 2, 0x0180);
		queueVision(0x105); // seg000:1175
		break;
	case 0x50: // sub_1117b: after riding a worm
		var(0x0a) |= 0x40;
		addCharisma(0x28);
		character(Jessica)[0] = 9;
		break;
	case 0x54: // sub_11188: the greenhouse door
		var(0x1259) &= 0x7f;
		WRITE_LE_UINT16(&var(0x1211), 0xffff);
		break;
	case 0x58: // sub_11196: Liet Kynes met
		var(0x0a) |= 0x20;
		cutscene = 0x12fb;
		reveal({ 63, 60, 61, 67, 65 });
		break;
	case 0x5c: // sub_111b3
		character(Kynes)[0] = 5;
		var(0x11d0) += 0x0c;
		WRITE_LE_UINT16(&var(0x1156), (uint16)((_state.w(GameState::kGameTime) >> 4) + 3));
		++var(0x1141);
		break;
	case 0x60: // sub_111cb: Chani is taken to the Harkonnen palace
		_state.setB(0xff, 0);
		character(Chani)[0] = 2;
		character(Chani)[1] = location(1).type;
		character(Chani)[2] = 0x80;
		character(Chani)[3] = 2;
		break;
	default: // 0x18, 0x24, 0x3c, 0x68, 0x6c: nothing; 0x64: the illness (not yet)
		break;
	}
	_log.line(Common::String::format("Story: phase %#x callback (cutscene %#x, vision %#x)", phase, cutscene, vision));
}

void World::settleCharacter(uint index) {
	const uint o = kCharacterTable + index * kCharacterSize;
	if (index >= kCharacters || o + 4 > GameState::kSize)
		return;
	_state.vars[o] = _state.b(GameState::kLocationAndRoom);
	_state.vars[o + 1] = _state.b(GameState::kLocationAndRoom + 1);
	_state.vars[o + 2] = _state.b(6);
	_state.vars[o + 3] = _state.b(7);
}

Troop World::troop(uint id) const {
	Troop t;
	memset(&t, 0, sizeof(t));
	if (id < 1 || id > kTroops)
		return t;
	const byte *r = _state.vars + kTroopTable + (id - 1) * kTroopSize;
	t.id = r[0];
	t.next = r[1];
	t.occupation = r[3];
	t.location = READ_LE_UINT16(r + 4);
	t.kind = r[16];
	t.dissatisfaction = r[18];
	t.motivation = r[21];
	t.spiceSkill = r[22];
	t.armySkill = r[23];
	t.ecologySkill = r[24];
	t.equipment = r[25];
	t.population = r[26] * 10u;
	return t;
}

void World::troopsAt(uint locationIndex, Common::Array<uint> &ids) const {
	ids.clear();
	uint id = location(locationIndex).troop;
	while (id >= 1 && id <= kTroops && ids.size() < kTroops) {
		ids.push_back(id);
		id = troop(id).next;
	}
}

uint World::troopForPerson(uint group) const {
	if (group < kFremen)
		return 0;
	if (group == kFremen)
		return localTroop(false);
	Common::Array<uint> ids;
	troopsAt(currentLocation(), ids);
	uint k = group - kFremenChief;
	for (uint i = 0; i < ids.size(); ++i) {
		const Troop t = troop(ids[i]);
		if (!t.harkonnen() && t.hired() && k-- == 0)
			return ids[i];
	}
	return 0;
}

uint World::localTroop(bool hired) const {
	Common::Array<uint> ids;
	troopsAt(currentLocation(), ids);
	for (uint i = 0; i < ids.size(); ++i) {
		const Troop t = troop(ids[i]);
		if (!t.harkonnen() && t.hired() == hired)
			return ids[i];
	}
	return 0;
}

uint World::rollRandom(uint range) {
	// The executable keeps rolling random bits in the word at ds:0; its
	// generator is not transcribed, a 16-bit Galois LFSR stands in (it is
	// saved with the game, so runs stay reproducible).
	uint16 r = _state.w(GameState::kRandomBits);
	if (!r)
		r = 0xace1;
	r = (uint16)((r >> 1) ^ ((r & 1) ? 0xb400 : 0));
	_state.setW(GameState::kRandomBits, r);
	return range ? r % range : r;
}

uint World::motivationModifier(uint id) const {
	// seg000:6efd.
	const Troop t = troop(id);
	const uint job = t.occupation & 0x0f;
	int m = t.motivation + (_state.b(0xfa) ? 20 : 0);
	if (job == 6) {
		if (t.location == Location::kTableOffset + currentLocation() * Location::kRecordSize)
			m = MIN(m + 30, 100);
	} else if ((job & 0x0e) == 8) {
		m = 100;
	} else {
		m = MIN(m, 100);
	}
	const byte phase = _state.b(GameState::kPhase);
	if (phase >= 0x64 && phase < 0x68)
		m = MAX(m - 40, 10);
	return (uint)m;
}

bool World::troopAgreesToFollow(uint id) const {
	// seg000:95c1. ds:ac is the Harkonnen population sum (bytes, men / 10).
	uint harkonnen = 0;
	for (uint i = 1; i <= kTroops; ++i) {
		const Troop t = troop(i);
		if (t.id && t.harkonnen())
			harkonnen += t.population / 10;
	}
	const uint charisma = _state.b(kCharisma);
	return harkonnen < 1000 || charisma > 100 || (100 - charisma) / 4 <= motivationModifier(id);
}

bool World::rallyTroop(uint id) {
	// troop_rally_troop, seg000:66ce.
	const Troop t = troop(id);
	if (!t.id || t.hired() || t.harkonnen())
		return false;
	_state.setB(GameState::kFremenTroops, (byte)(_state.b(GameState::kFremenTroops) + 1));
	// seg000:66e1: with ds:1178 troops rallied the story moves to phase 0x4c
	// (the Harkonnens strike back: Leto's death).
	if (_state.b(GameState::kFremenTroops) >= var(0x1178) && _state.b(GameState::kPhase) < 0x4c)
		_requestedPhase = 0x4c;
	changeCharisma(1); // seg000:6f78, with the motivation spill
	troopByte(id, 3) = (byte)((t.occupation & 0x20) | Troop::kWaitingForOrders);
	WRITE_LE_UINT16(&troopByte(id, 0x0a), _state.w(GameState::kGameTime));
	WRITE_LE_UINT16(&troopByte(id, 0x0c), 0);
	WRITE_LE_UINT16(&troopByte(id, 0x0e), 0);
	troopByte(id, 0x14) = (byte)(_state.w(GameState::kGameTime) >> 4);
	// seg000:6704: the first troop rallied at a place paints its Atreides disc.
	const int place = troopPlace(id);
	if (place >= 0 && !locationByte((uint)place, 11)) {
		locationByte((uint)place, 11) = 2;
		paintArea((uint)place, 0x20, 2);
	}
	_log.line(Common::String::format("Troops: troop %u rallied (%u men), %u Fremen troops, charisma %u", id,
			t.population, _state.b(GameState::kFremenTroops), _state.b(kCharisma)));
	return true;
}

void World::setTroopOccupation(uint id, byte occupation) {
	// seg000:6acb: store the job, clear the stopped bit and restart its clocks.
	if (id < 1 || id > kTroops)
		return;
	const Troop t = troop(id);
	if ((t.occupation & 0x0f) == occupation)
		return;
	if (occupation == Troop::kIrrigation) {
		// seg000:6adc: only at place 62 (record 0x7c8) does ecology without
		// bulbs there become bulb growing.
		const int index = (int)(t.location - Location::kTableOffset) / Location::kRecordSize;
		if (index == kBulbPlace && !location((uint)index).bulbs)
			occupation = Troop::kBulbGrowing;
	}
	// seg000:6aea writes the whole byte: a new job clears the captured (0x20),
	// moving and not-hired bits; 6aed clears the refusals (speech 0x30).
	troopByte(id, 3) = occupation;
	troopByte(id, 0x12) &= 0xcf;
	// seg000:6b06: a job other than waiting shows its skill class in the
	// troop's lines (speech 0x2000 spice, 0x4000 army, 0x8000 ecology).
	if (occupation != Troop::kWaitingForOrders)
		troopByte(id, 0x13) |= (byte)(0x20 << ((occupation & 0x0f) >> 2));
	// Re-derive "working" (bitfield_10 bit 8) and the stopped bit from the
	// job's viability test (seg000:6b96 for mining).
	uint16 bits = READ_LE_UINT16(&troopByte(id, 0x10)) & ~0x100;
	if (occupation == Troop::kSpiceMining) {
		const int index = (int)(t.location - Location::kTableOffset) / Location::kRecordSize;
		const Location l = index >= 0 ? location((uint)index) : Location();
		const bool viable = index >= 0 && !(bits & 0x200) && !(READ_LE_UINT16(&troopByte(id, 0x12)) & 0x30) &&
							l.spiceDensity >= 1 && ((l.status ^ 0x40) & 0x41) == 0;
		if (viable)
			bits |= 0x100;
		else
			troopByte(id, 3) |= Troop::kStopped;
	} else if (occupation != Troop::kWaitingForOrders) {
		bits |= 0x100;
	}
	WRITE_LE_UINT16(&troopByte(id, 0x10), bits);
	WRITE_LE_UINT16(&troopByte(id, 0x0a), _state.w(GameState::kGameTime));
	WRITE_LE_UINT16(&troopByte(id, 0x0c), 0);
	WRITE_LE_UINT16(&troopByte(id, 0x0e), 0);
}

uint World::harvestRate(uint id) const {
	// troop_update_harvest_rate, seg000:708a.
	const Troop t = troop(id);
	const int index = (int)(t.location - Location::kTableOffset) / Location::kRecordSize;
	if (index < 0 || (uint)index >= locationCount())
		return 0;
	const Location l = location((uint)index);
	uint product = ((motivationModifier(id) + (t.spiceSkill & 0xf0)) & 0xff) * (t.population / 10);
	if (!(t.equipment & 0x80))
		product >>= 2; // no harvester
	return ((((l.spiceDensity & 0xf0) + 1) * ((product >> 8) & 0xff)) >> 7) & 0x1ff;
}

void World::raiseSpiceSkill(uint id, byte amount) {
	// seg000:6edd: + amount, capped at 0x5f.
	troopByte(id, 0x16) = (byte)MIN<uint>(0x5f, troopByte(id, 0x16) + amount);
}

void World::mineSpice(uint id, uint index) {
	// seg000:6fe5. Harvester breakdowns and saboteurs (seg000:714c) are not
	// transcribed.
	const Troop t = troop(id);
	const Location l = location(index);
	const bool viable = !(READ_LE_UINT16(&troopByte(id, 0x10)) & 0x200) && !(READ_LE_UINT16(&troopByte(id, 0x12)) & 0x30) &&
						l.spiceDensity >= 1 && ((l.status ^ 0x40) & 0x41) == 0;
	if (!viable) {
		WRITE_LE_UINT16(&troopByte(id, 0x0c), 0);
		WRITE_LE_UINT16(&troopByte(id, 0x0e), 0);
		troopByte(id, 3) |= Troop::kStopped;
		return;
	}
	troopByte(id, 3) &= ~Troop::kStopped;
	WRITE_LE_UINT16(&troopByte(id, 0x10), READ_LE_UINT16(&troopByte(id, 0x10)) | 0x100); // working
	const uint kg = harvestRate(id);
	WRITE_LE_UINT16(&troopByte(id, 0x0c), (uint16)kg);
	const uint16 total = READ_LE_UINT16(&troopByte(id, 0x0e));
	WRITE_LE_UINT16(&troopByte(id, 0x0e), (uint16)(total + kg));
	if (((total + kg) ^ total) & 0xff80)
		raiseSpiceSkill(id, 1);
	// The stock counts 10 kg batches; the remainder carries over (ds:46e1,
	// outside the saved block).
	const uint carried = kg + _harvestRemainder;
	_state.setW(kSpiceStock, (uint16)MIN<uint>(0xffff, _state.w(kSpiceStock) + carried / 10));
	_harvestRemainder = carried % 10;
	// The place's density drops by (kg + remainder) / field size.
	if (l.spiceAmount) {
		const uint eaten = kg + locationByte(index, 19);
		locationByte(index, 19) = (byte)(eaten % l.spiceAmount);
		const uint drop = eaten / l.spiceAmount;
		locationByte(index, 18) = (byte)(drop >= l.spiceDensity ? 0 : l.spiceDensity - drop);
	}
}

void World::prospect(uint id, uint index) {
	// seg000:70cc.
	const Location l = location(index);
	if (l.status & 0x02)
		return; // a battle suspends the job
	if (!(l.status & 0x40)) {
		const Troop t = troop(id);
		uint16 duration = READ_LE_UINT16(&troopByte(id, 0x0c));
		if (!duration) {
			const uint speed = MAX<uint>(1, t.motivation + t.spiceSkill);
			duration = (uint16)(((uint)l.spiceAmount << 4) / speed);
			WRITE_LE_UINT16(&troopByte(id, 0x0c), duration);
		}
		const uint16 elapsed = (uint16)(_state.w(GameState::kGameTime) - READ_LE_UINT16(&troopByte(id, 0x0a)));
		if (duration > elapsed) {
			// Progress in percent for the troop's panel.
			if (elapsed)
				WRITE_LE_UINT16(&troopByte(id, 0x0e), (uint16)(elapsed * 100u / duration));
			return;
		}
		raiseSpiceSkill(id, 2);
		locationByte(index, 10) |= 0x40;
		_log.line(Common::String::format("Troops: troop %u prospected place %u", id, index));
	}
	WRITE_LE_UINT16(&troopByte(id, 0x0e), 100);
	// seg000:9d5f: with destinations left the prospectors march to the next
	// (sub_B072); otherwise the job is done and they wait for new orders.
	if (id == kProspectorTroop && prospectorDestination(0) && issueMoveOrder(id, 0))
		return;
	troopByte(id, 3) |= Troop::kStopped;
}

void World::runPeriod() {
	// run_events_for_current_time_period, seg000:1b23: the troops' jobs
	// (seg000:6c6f; the table at 6c26 has spice mining and prospecting
	// transcribed so far), then the period's action (seg000:1db3).
	// Once the Harkonnen palace has fallen (ds:c2 >= 7) no troop or
	// time-of-day event runs any more (seg000:1b5e).
	const bool troopEvents = _state.b(kShipmentPaused) < 7;
	for (uint id = 1; troopEvents && id < kTroops; ++id) {
		const Troop t = troop(id);
		if (!t.id)
			continue;
		// seg000:6c92-6ceb: a marching troop always travels; one sulking or
		// refusing (speech 0x430) sits out, unless ds:fa clears its 0x30.
		if (t.occupation & 0x40) {
			if (!(t.occupation & 0xa0) || (READ_LE_UINT16(&troopByte(id, 0x12)) & 0x430))
				troopTravelStep(id); // seg000:6ced -> 8308
			continue;
		}
		if (READ_LE_UINT16(&troopByte(id, 0x12)) & 0x430) {
			if (!_state.b(0xfa))
				continue;
			troopByte(id, 0x12) &= 0xcf;
			if (READ_LE_UINT16(&troopByte(id, 0x12)) & 0x400)
				continue;
		}
		if (t.occupation & 0xa0)
			continue;
		const int index = (int)(t.location - Location::kTableOffset) / Location::kRecordSize;
		if (index < 0 || (uint)index >= locationCount())
			continue;
		// The new-day routine (floppy sub_9A58, ds:423A set on the first
		// period of a day) starts the spice (9C1E), army (9E28) and
		// irrigation (A2C7) handlers; its fort conversion lives in
		// militaryTraining, its north/south quarrel here.
		const byte job = t.occupation & 0x0f;
		if (timeSlot() == 0 && (job == Troop::kSpiceMining || job == Troop::kMilitaryTraining || job == Troop::kIrrigation))
			fremenQuarrel(id, (uint)index);
		switch (t.occupation & 0x0f) {
		case Troop::kMilitaryTraining:
			if (!t.harkonnen())
				militaryTraining(id, (uint)index);
			break;
		case Troop::kEspionage:
			if (!t.harkonnen())
				espionageTick(id, (uint)index);
			break;
		case 6:
			if (!t.harkonnen() && !(t.occupation & 0x10))
				attackTick(id, (uint)index);
			break;
		case Troop::kSpiceMining:
			mineSpice(id, (uint)index);
			break;
		case Troop::kProspecting:
			prospect(id, (uint)index);
			break;
		case Troop::kIrrigation:
		case Troop::kWindTrap:
		case Troop::kBulbGrowing:
			if (!t.harkonnen())
				runEcologyJob(id, (uint)index);
			break;
		default:
			break;
		}
	}
	if (troopEvents && timeSlot() == 3) {
		// actions_time_in_day_3 (seg000:20a4): the Emperor's shipments.
		uint16 sighting = 0;
		if (shipmentDay(sighting))
			_emperorEnding = true;
		if (sighting)
			addSighting(sighting);
	}
	if (timeSlot() == 8 && shipmentReminderDue())
		queueVision(0x30b); // actions_time_in_day_8 (seg000:1dda)
	if (timeSlot() == 15 && (rollRandom(2) & 1)) {
		// seg000:1d10: every Harkonnen troop of 1..199 (x 10 men) gains ten men.
		for (uint id = 1; id <= kTroops; ++id) {
			const Troop t = troop(id);
			if (t.id && t.harkonnen() && troopByte(id, 26) >= 1 && troopByte(id, 26) < 200)
				++troopByte(id, 26);
		}
	}
	if (timeSlot() == 0)
		ecologyNewDay(); // seg000:63f0, on the new day
	// seg000:1b86: the current place is staged again for the conditions.
	stageLocationForConditions(currentLocation());
	if (timeSlot() == 0) {
		// New day (seg000:1c46): the Harkonnen production, sum of density / 8
		// over the places they still work, plus some chance (seg000:1cda).
		uint sum = 0;
		for (uint i = 0; i < locationCount(); ++i) {
			const Location l = location(i);
			if (l.isFortress() || l.type == Location::kHarkonnenPalace || l.hidden())
				sum += l.spiceDensity / 8;
		}
		_state.setW(0xa8, (uint16)(sum + rollRandom(sum / 16 + 1)));
		// Today's production (ds:a6): stock gained since yesterday.
		const uint16 stock = _state.w(kSpiceStock);
		_state.setW(0xa6, (uint16)(stock >= _state.w(0x1170) ? stock - _state.w(0x1170) : 0));
		_state.setW(0x1170, stock);
		// The spice troops at work, for the logs (the harvest is per period).
		uint miners = 0, rate = 0, idle = 0;
		for (uint id = 1; id < kTroops; ++id) {
			const Troop t = troop(id);
			if (!t.id || !t.hired() || t.harkonnen() || (t.occupation & 0x0f) != Troop::kSpiceMining)
				continue;
			++miners;
			const uint kg = READ_LE_UINT16(&troopByte(id, 0x0c));
			rate += kg;
			if (!kg || (t.occupation & (Troop::kStopped | 0x40)))
				++idle;
		}
		_log.line(Common::String::format("Clock: day %u, spice %u kg; %u miners (%u idle), %u kg a period", day(),
				spiceStock(), miners, idle, rate));
	}
}

void World::advanceTime(uint slots) {
	for (uint n = 0; n < slots; ++n) {
		_state.setW(GameState::kGameTime, (uint16)(_state.w(GameState::kGameTime) + 1));
		runPeriod();
	}
}

uint World::day() const {
	return 1 + _state.w(GameState::kGameTime) / kSlotsPerDay;
}

uint World::timeSlot() const {
	return _state.w(GameState::kGameTime) % kSlotsPerDay;
}

uint World::spiceStock() const {
	return _state.w(kSpiceStock) * 10u;
}

void World::markDiscovered(uint index) {
	// location_mark_discovered, seg000:425b.
	const Location l = location(index);
	if (!l.hidden())
		return;
	locationByte(index, 10) &= ~0x80;
	locationByte(index, 11) = 0;
	if (l.isSietch())
		_state.setB(GameState::kSietchesAvailable, (byte)(_state.b(GameState::kSietchesAvailable) + 1));
	_log.line(Common::String::format("World: place %u discovered", index));
	// Tuono-Harg (first name 3 Tuono, last name 6 Harg: the word 0x0603) moves the story to 0x10.
	if (l.firstName == 3 && l.lastName == 6)
		_requestedPhase = 0x10;
}

bool World::discoverable(uint index) const {
	const Location l = location(index);
	// seg000:4125-4131: any hidden place (villages too) once the story phase
	// reaches its byte 0x0b.
	return l.hidden() && l.discoverPhase != 0xff && l.discoverPhase <= _state.b(GameState::kPhase);
}

void World::applyCelimynTuekFix() {
	// The wiki's save patch (place record 0c 05 .. 03 00 80 ff: type 3, no
	// troop, hidden, discovery phase 0xff) done in memory: byte 0x0b becomes
	// 0x58, so the flight search (floppy sub_6223, 6257: cmp ds:2A, [si+0Bh])
	// finds the sietch from phase 0x58 on.
	if (!_fixCelimynTuek)
		return;
	for (uint i = 0; i < locationCount(); ++i) {
		const Location l = location(i);
		if (l.firstName != 0x0c || l.lastName != 0x05 || !l.hidden() || l.discoverPhase != 0xff)
			continue;
		locationByte(i, 11) = 0x58;
		_log.line(Common::String::format("Option: dune_fix_celimyn_tuek: place %u discovery phase 0xff -> 0x58", i));
	}
}

uint World::rowCells(int latitude) const {
	const uint row = (uint)ABS(latitude);
	if ((row + 1) * 8 > _tablat.size())
		return 0;
	return 2u * READ_BE_UINT16(_tablat.data() + 8 * row + 2);
}

uint World::unitsPerCell(int latitude) const {
	// Built at startup from TABLAT; checked against the floppy's ds:43C7 on all 99 rows.
	const uint cells = rowCells(latitude);
	return cells ? (131072 + cells) / (2 * cells) : 65535;
}

namespace {

/** x86 idiv: the quotient truncated toward zero. */
int truncDiv(int a, int b) {
	const int q = ABS(a) / ABS(b);
	return (a >= 0) == (b > 0) ? q : -q;
}

} // namespace

bool World::compassAngle(uint16 fromLng, int16 fromLat, uint16 toLng, int16 toLat, byte &angle) {
	int bx = (int16)(toLat - fromLat), dx = (int16)(toLng - fromLng);
	if (bx < -0x80 || bx >= 0x80) {
		bx >>= 1;
		dx >>= 1;
	}
	bx = (int16)((bx & 0xff) << 8); // the latitude in the units of a longitude cell (x256)
	const int ax = ABS(bx), cx = ABS(dx);
	if (cx >= ax) {
		if (cx < 1)
			return false;
		const byte al = (byte)truncDiv(0x20 * bx, dx);
		angle = (byte)(dx >= 0 ? al + 0x40 : al + 0xc0);
		return true;
	}
	if (ax < 1)
		return false;
	byte al = (byte)truncDiv(0x20 * dx, bx);
	if (bx >= 0)
		al = (byte)(al - 0x80);
	angle = (byte)-al;
	return true;
}

void World::travelStep(uint16 &longitude, int16 &latitude, byte &fraction, byte &heading) const {
	// 7E19: the heading as a major component of 0x20 and a minor one.
	int cdx, cbx;
	const byte bl = (byte)(heading + 0x20);
	if ((bl & 0x7f) >= 0x40) {
		byte a = (byte)(heading - 0x40);
		cdx = 0x20;
		if (bl & 0x80) {
			cdx = -0x20;
			a = (byte)-(byte)(a - 0x80);
		}
		cbx = (int8)a;
	} else {
		byte a = heading;
		cbx = -0x20;
		if (bl & 0x80) {
			a = (byte)-(byte)(a - 0x80);
			cbx = 0x20;
		}
		cdx = (int8)a;
	}
	// 7E87: both scaled by the units per cell at this latitude.
	const int bp = (int)unitsPerCell(latitude);
	int lngDelta = truncDiv(bp * cdx, 0x20);
	const int latDelta = truncDiv(cbx * bp, 0x20);
	int ax = ABS(latDelta) + fraction;
	if ((ax >> 8) > 1) {
		lngDelta = truncDiv(lngDelta * 256, ax);
		ax = 0x100;
	}
	fraction = (byte)(ax & 0xff);
	int rows = ax >> 8;
	if (latDelta < 0)
		rows = -rows;
	latitude = (int16)(latitude + rows);
	longitude = (uint16)(longitude + lngDelta);
	if ((uint16)(latitude + 0x60) >= 0xc0) {
		heading = (byte)(heading + 0x80);
		longitude = (uint16)(longitude + 0x8000);
	}
}

uint World::cellDistance(uint16 lng0, int16 lat0, uint16 lng1, int16 lat1) const {
	// seg000:7c8f: max(|dlng| / units per cell at the first latitude, |dlat|).
	const uint cells = MAX<uint>(1, rowCells(lat0));
	const uint dLng = (uint)ABS((int)(int16)(lng1 - lng0)) * cells / 65536;
	return MAX<uint>(dLng, (uint)ABS(lat1 - lat0));
}

bool World::prepareNewGame() {
	Common::Array<byte> map2;
	_harvestRemainder = 0;
	if (!_resources.load("TABLAT.BIN", _tablat) || !_resources.load("MAP2.HSQ", map2) || map2.size() < 50681) {
		_log.line("World: TABLAT.BIN or MAP2.HSQ missing, spice fields not prepared");
		return false;
	}
	// seg000:0169: a histogram of MAP2's bytes, each count starting at 7.
	uint histogram[256];
	for (uint i = 0; i < 256; ++i)
		histogram[i] = 7;
	for (uint i = 0; i < 50681; ++i)
		++histogram[map2[i]];
	const uint kMapCentre = 0x62fc;
	for (uint i = 0; i < locationCount(); ++i) {
		const Location l = location(i);
		const uint row = (uint)ABS(l.latitude);
		if ((row + 1) * 8 > _tablat.size())
			continue;
		const int rowOffset = READ_BE_UINT16(_tablat.data() + 8 * row);
		const uint cells = 2u * READ_BE_UINT16(_tablat.data() + 8 * row + 2);
		// sub_1b58b: the cell under the longitude, rounded; sub_1b5c5 snaps
		// the longitude to that cell's start.
		const uint32 product = (uint32)l.longitude * cells;
		const uint cell = (product >> 16) + ((product >> 15) & 1);
		const int offset = (int)kMapCentre + (l.latitude < 0 ? -rowOffset : rowOffset) + (int)cell;
		if (offset < 0 || offset >= (int)map2.size() || !cells)
			continue;
		WRITE_LE_UINT16(&locationByte(i, 2), (uint16)((((uint32)cell & 0xffff) << 16) / cells));
		WRITE_LE_UINT16(&locationByte(i, 6), (uint16)offset);
		locationByte(i, 16) = map2[offset];
		locationByte(i, 17) = (byte)(histogram[map2[offset]] >> 4);
	}
	// seg000:01e0: each troop of a place gets the place's record and position,
	// and its region (the place's first-name id) in the low nibble of byte 18,
	// bit 7 for the southern regions.
	for (uint i = 0; i < locationCount(); ++i) {
		const Location l = location(i);
		Common::Array<uint> ids;
		troopsAt(i, ids);
		for (uint k = 0; k < ids.size(); ++k) {
			const uint id = ids[k];
			WRITE_LE_UINT16(&troopByte(id, 4), (uint16)(Location::kTableOffset + i * Location::kRecordSize));
			WRITE_LE_UINT16(&troopByte(id, 6), READ_LE_UINT16(&locationByte(i, 2)));
			WRITE_LE_UINT16(&troopByte(id, 8), (uint16)l.latitude);
			const byte region = l.firstName & 0x0f;
			byte high = troopByte(id, 18) & 0x70;
			if (region > 3)
				high ^= 0x80;
			if (region > 5)
				high ^= 0x80;
			if (region > 9)
				high ^= 0x80;
			troopByte(id, 18) = (byte)(high | region);
		}
	}
	_log.line("World: spice fields and troops prepared (seg000:0169)");
	resetMap(); // a new game's map, the places' cells marked (seg000:01a1)
	return true;
}

} // namespace Dune

/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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

// Troop move orders, the march and the arrival (seg000:84a6, 8308, 8604,
// 8357), with the chain and equipment bookkeeping they share. Addresses are
// the CD 3.7 executable's; notes/speedrun/battle-worm-spec.md section 2.

#include "dune/world.h"

#include "common/config-manager.h"
#include "common/system.h"
#include "common/util.h"

#include "dune/debug.h"

namespace Dune {

namespace {
enum {
	kTroopNext = 0x01,
	kTroopSlot = 0x02,
	kTroopOccupation = 0x03,
	kTroopLocation = 0x04,
	kTroopLongitude = 0x06,
	kTroopLatitude = 0x08,
	kTroopTime = 0x0a,
	kTroopDepC = 0x0c,
	kTroopDepE = 0x0e,
	kTroopBits = 0x10,
	kTroopSpeech = 0x12,
	kTroopMotivation = 0x15,
	kTroopEquipment = 0x19,
	kTroopPopulation = 0x1a,
	kMoving = 0x40,
	kHarkonnenBit = 0x80, ///< bits10 bit 7
	kHiddenBit = 0x10     ///< bits10 bit 4: a hidden Harkonnen troop
};
} // namespace

void World::seedRandom() {
	// The executable seeds both generators from the BIOS tick count at
	// startup (seg000:00c3), so they are not part of a save. A harness run
	// pins them with dune_rng_seed so its checkpoints stay reproducible.
	uint32 seed = g_system->getMillis();
	if (ConfMan.hasKey("dune_rng_seed"))
		seed = (uint32)ConfMan.getInt("dune_rng_seed");
	_rngA = (uint16)(seed ^ 0x1234);
	_rngB = (uint16)((seed >> 16) ^ seed ^ 0x5a5a);
}

uint16 World::lcgRand() {
	// rand, seg000:e3cc: s = s * 0xcbd1 + 1; al = bits 8-15 of the new state,
	// ah = bits 16-23 of the product.
	const uint32 p = (uint32)_rngA * 0xcbd1u + 1;
	_rngA = (uint16)p;
	return (uint16)(((p >> 8) & 0xff) | (((p >> 16) & 0xff) << 8));
}

uint16 World::lcgRandMasked(uint16 mask) {
	// rand_masked, seg000:e3b7: the same with 0xe56d on the second state.
	const uint32 p = (uint32)_rngB * 0xe56du + 1;
	_rngB = (uint16)p;
	return (uint16)((((p >> 8) & 0xff) | (((p >> 16) & 0xff) << 8)) & mask);
}

int World::troopPlace(uint id) const {
	const uint16 off = READ_LE_UINT16(_state.vars + kTroopTable + (id - 1) * kTroopSize + kTroopLocation);
	if (off < Location::kTableOffset)
		return -1;
	const uint index = (off - Location::kTableOffset) / Location::kRecordSize;
	return index < locationCount() ? (int)index : -1;
}

int World::placeIndex(uint16 offset) const {
	if (offset < Location::kTableOffset)
		return -1;
	const uint index = (offset - Location::kTableOffset) / Location::kRecordSize;
	return index < locationCount() ? (int)index : -1;
}

void World::shiftProspectorQueue() {
	// shift_prospector_troop_destinations_array (seg000:8347): the words at
	// +2..+7 move down one slot; the fourth word (0) ends the list.
	for (uint k = 0; k < 3; ++k)
		setProspectorDestination(k, READ_LE_UINT16(_state.vars + ds(0x11d3) + 2 * (k + 1)));
}

uint16 World::placeOffset(uint index) {
	return (uint16)(Location::kTableOffset + index * Location::kRecordSize);
}

void World::unlinkTroop(uint id) {
	// seg000:858c: take the troop out of its place's chain.
	const int index = troopPlace(id);
	if (index < 0)
		return;
	byte &head = locationByte((uint)index, 9);
	if (head == id) {
		head = troopByte(id, kTroopNext);
	} else {
		uint prev = head, guard = 0;
		while (prev >= 1 && prev <= kTroops && guard++ < kTroops) {
			if (troopByte(prev, kTroopNext) == id) {
				troopByte(prev, kTroopNext) = troopByte(id, kTroopNext);
				break;
			}
			prev = troopByte(prev, kTroopNext);
		}
	}
	troopByte(id, kTroopNext) = 0;
}

uint World::linkTroop(uint id, uint index) {
	// seg000:851f: Fremen take the first free slot from 1, Harkonnens from 9;
	// the troop joins the end of the chain.
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i)
		if (ids[i] == id) {
			// Already in this chain: linking it again would loop the chain.
			_log.line(Common::String::format("Troops: troop %u is already at place %u", id, index));
			return troopByte(id, kTroopSlot);
		}
	const bool harkonnen = (troopByte(id, kTroopBits) & kHarkonnenBit) != 0;
	uint slot = harkonnen ? 9 : 1;
	for (bool clash = true; clash && slot < 31;) {
		clash = false;
		for (uint i = 0; i < ids.size(); ++i)
			if (ids[i] != id && troopByte(ids[i], kTroopSlot) == slot) {
				clash = true;
				++slot;
				break;
			}
	}
	troopByte(id, kTroopSlot) = (byte)slot;
	troopByte(id, kTroopNext) = 0;
	WRITE_LE_UINT16(&troopByte(id, kTroopLocation), placeOffset(index));
	if (ids.empty())
		locationByte(index, 9) = (byte)id;
	else
		troopByte(ids.back(), kTroopNext) = (byte)id;
	if (!harkonnen && slot > 8) {
		// seg000:85cc: a Fremen troop past slot 8 evicts the first captured
		// Harkonnen troop there.
		for (uint i = 0; i < ids.size(); ++i)
			if ((troopByte(ids[i], kTroopBits) & kHarkonnenBit) && (troopByte(ids[i], kTroopOccupation) & 0x20)) {
				removeFromPlay(ids[i]);
				break;
			}
	}
	return slot;
}

void World::registerEquipment(uint id, uint index, int sign, byte mask) {
	// seg000:7f5f (+) and 7f75 (-): a troop's items count in its place's row
	// (bit 7 harvesters at +0x14 .. bit 1 bulbs at +0x1a).
	const byte e = troopByte(id, kTroopEquipment) & mask;
	for (uint k = 0; k < 7; ++k) {
		if (!(e & (0x80 >> k)))
			continue;
		byte &count = locationByte(index, 0x14 + k);
		count = (byte)(sign > 0 ? MIN<uint>(255, count + 1) : (count ? count - 1 : 0));
	}
}

void World::removeFromPlay(uint id) {
	// seg000:66b1: the troop leaves the game.
	unlinkTroop(id);
	WRITE_LE_UINT16(&troopByte(id, kTroopLocation), 0);
	troopByte(id, kTroopOccupation) = 0xa0;
	troopByte(id, kTroopPopulation) = 0;
	troopByte(id, kTroopEquipment) = 0;
}

bool World::wouldQuarrel(uint id, uint place) const {
	if (place >= locationCount() || location(place).type >= 0x21)
		return false; // only sietches and the palace quarrel (7BEA)
	const byte *self = _state.vars + kTroopTable + (id - 1) * kTroopSize;
	byte sides = (self[kTroopOccupation] & 0x2f) ? 0 : ((self[kTroopSpeech] & 0x80) ? 2 : 1);
	Common::Array<uint> ids;
	troopsAt(place, ids);
	for (uint k = 0; k < ids.size(); ++k) {
		if (ids[k] == id)
			continue;
		const byte *r = _state.vars + kTroopTable + (ids[k] - 1) * kTroopSize;
		if (!(r[kTroopOccupation] & 0x2f))
			sides |= (r[kTroopSpeech] & 0x80) ? 2 : 1;
	}
	return sides == 3;
}

bool World::prepareQuarrelTest(uint &north, uint &south, uint &place) {
	// The first Fremen troop of each half (troop byte 0x12 bit 7) standing
	// at a sietch; the southern one is moved into the northern one's sietch.
	north = south = 0;
	for (uint id = 1; id <= kTroops && (!north || !south); ++id) {
		const Troop t = troop(id);
		const int at = troopPlace(id);
		if (!t.id || t.harkonnen() || at < 0 || (uint)at == currentLocation() || !location((uint)at).isSietch())
			continue;
		uint &slot = (troopByte(id, kTroopSpeech) & 0x80) ? south : north;
		if (!slot)
			slot = id;
	}
	if (!north || !south)
		return false;
	place = (uint)troopPlace(north);
	unlinkTroop(south);
	WRITE_LE_UINT16(&troopByte(south, kTroopLocation), placeOffset(place));
	linkTroop(south, place);
	const uint pair[2] = { north, south };
	for (uint k = 0; k < 2; ++k) {
		applyJob(pair[k], Troop::kSpiceMining);
		troopByte(pair[k], kTroopMotivation) = 30;
	}
	return true;
}

bool World::prepareSmallRulesTest(uint &id, uint &place) {
	id = 0;
	for (uint t = 1; t <= kTroops && !id; ++t) {
		const Troop tr = troop(t);
		const int at = troopPlace(t);
		if (tr.id && !tr.harkonnen() && t != kProspectorTroop && at >= 0 && (uint)at != currentLocation() &&
				location((uint)at).isSietch())
			id = t;
	}
	if (!id)
		return false;
	place = (uint)troopPlace(id);
	// Alone at its sietch, so the ring grows by one a day.
	Common::Array<uint> ids;
	troopsAt(place, ids);
	for (uint k = 0; k < ids.size(); ++k)
		if (ids[k] != id && !troop(ids[k]).harkonnen()) {
			unlinkTroop(ids[k]);
			WRITE_LE_UINT16(&troopByte(ids[k], kTroopLocation), placeOffset(0));
			linkTroop(ids[k], 0);
		}
	troopByte(id, kTroopOccupation) &= 0x0f; // hired
	placeTroopForTest(id, place, Troop::kSpiceMining, 0x80);
	troopByte(id, kTroopMotivation) = 50;
	troopByte(id, 0x14) = (byte)((_state.w(GameState::kGameTime) >> 4) - 10);
	locationByte(place, 10) &= ~0x01;
	locationByte(place, 11) = 2;
	return true;
}

void World::placeTroopForTest(uint id, uint place, byte job, byte equipment) {
	if (troopPlace(id) != (int)place) {
		unlinkTroop(id);
		WRITE_LE_UINT16(&troopByte(id, kTroopLocation), placeOffset(place));
		linkTroop(id, place);
	}
	applyJob(id, job);
	// The place's stock (bytes 0x14..0x1a) counts what its troops hold.
	const byte added = (byte)(equipment & ~troopByte(id, kTroopEquipment));
	for (uint k = 0; k < 7; ++k)
		if (added & (0x80 >> k))
			++locationByte(place, 0x14 + k);
	troopByte(id, kTroopEquipment) |= equipment;
	WRITE_LE_UINT16(&troopByte(id, kTroopBits), READ_LE_UINT16(&troopByte(id, kTroopBits)) & 0x00ff);
}

bool World::prepareSaboteurTest(uint &miner, uint &place) {
	// The first Fremen troop the saboteurs can reach (speech bit 6, from the
	// initial data) standing somewhere away from Paul.
	miner = 0;
	for (uint id = 1; id <= kTroops && !miner; ++id) {
		const Troop t = troop(id);
		const int at = troopPlace(id);
		if (t.id && !t.harkonnen() && at >= 0 && (uint)at != currentLocation() && (troopByte(id, kTroopSpeech) & 0x40))
			miner = id;
	}
	if (!miner)
		return false;
	place = (uint)troopPlace(miner);
	prepareFieldForTest(place);
	placeTroopForTest(miner, place, Troop::kSpiceMining, 0x80);
	return true;
}

void World::prepareFieldForTest(uint place) {
	// A prospected, unexhausted field with spice to mine.
	locationByte(place, 10) = (byte)((locationByte(place, 10) | 0x40) & ~0x05);
	if (!locationByte(place, 17))
		locationByte(place, 17) = 0x80;
	if (locationByte(place, 18) < 0x40)
		locationByte(place, 18) = 0x80;
}

byte World::wormChance(uint place) {
	return var(0x1141 + locationByte(place, 0));
}

uint World::takeFortsForTest(int keep) {
	for (uint i = 2; i < locationCount(); ++i) {
		const Location l = location(i);
		if (!l.isFortress() || (int)i == keep)
			continue;
		Common::Array<uint> ids;
		troopsAt(i, ids);
		for (uint k = 0; k < ids.size(); ++k)
			if (troopByte(ids[k], kTroopBits) & kHarkonnenBit)
				removeFromPlay(ids[k]);
		locationByte(i, 8) &= 7;
		locationByte(i, 10) &= 0x7f; // known
		locationByte(i, 11) = 5;
	}
	uint left = 0;
	for (uint i = 0; i < locationCount(); ++i)
		if (!friendlyPlace(i))
			++left;
	return left;
}

void World::greenOverForTest(uint sietch, uint target, uint radius) {
	const Location t = location(target);
	WRITE_LE_UINT16(&locationByte(sietch, 12), t.longitude);
	locationByte(sietch, 14) = (byte)(int8)t.latitude;
	locationByte(sietch, 11) = (byte)radius;
	spreadVegetation(sietch);
}

void World::finalBattleForTest(uint count, Common::Array<uint> &ids) {
	ids.clear();
	_state.setB(kShipmentPaused, 6);
	for (uint id = 1; id <= kTroops && ids.size() < count; ++id) {
		const Troop t = troop(id);
		// Not the prospectors (troop 3, ds:08e0): the after-battle pass
		// (75af) leaves them attacking.
		if (!t.id || t.harkonnen() || troopPlace(id) <= 1 || id == kProspectorTroop)
			continue;
		if (!t.hired())
			rallyTroop(id);
		unlinkTroop(id);
		linkTroop(id, 1);
		troopByte(id, kTroopOccupation) = 6;
		WRITE_LE_UINT16(&troopByte(id, kTroopSpeech), 0);
		troopByte(id, kTroopMotivation) = 100;
		troopByte(id, 0x17) = 0x5f;  // army skill
		troopByte(id, 0x19) = 0x3c;  // krys knives, laser guns, weirding modules, atomics
		troopByte(id, 0x1a) = 200;   // 2000 men
		ids.push_back(id);
	}
	locationByte(1, 10) |= 0x02; // the palace is in battle
}

void World::lowerMotivation(uint id, byte amount) {
	// seg000:6f93 (floppy 7cfb): motivation - amount, not below 0; below 5
	// it is 4, the troop stops (7085: occupation bit 4) and sulks (speech
	// word | 0x20, the refusal the dispatcher skips, 6c92).
	if (id < 1 || id > kTroops)
		return;
	byte &m = troopByte(id, kTroopMotivation);
	m = (byte)(m >= amount ? m - amount : 0);
	if (m < 5) {
		m = 4;
		troopByte(id, kTroopOccupation) |= Troop::kStopped;
		WRITE_LE_UINT16(&troopByte(id, kTroopSpeech), READ_LE_UINT16(&troopByte(id, kTroopSpeech)) | 0x20);
		_log.line(Common::String::format("Troops: troop %u sulks (motivation 4)", id));
	}
}

void World::troopContacted(uint id) {
	// map_close_troop_contact_popup (CD 7b86-7b89, floppy 88a4-88a7): the
	// day of the last contact, which the daily decay counts from (6e4e).
	// The ds:4c test before it (7b72) always reads 0: 7b65 has just
	// cleared it.
	if (id < 1 || id > kTroops)
		return;
	// 7b7c-7b81 (floppy 889a-889f): the contact's news are told: word 0x10
	// keeps only 0x3f0 (the saboteurs' 0x8000 and the other event bits go,
	// the damaged harvester's 0x200 stays), word 0x12 loses 0x1a00 (merged
	// 0x200, cured 0x800, 0x1000). Checked on Spice86 (floppy, the
	// saboteurs' patched save): troop 1's word 0x10 0x8300 -> 0x0300, byte
	// 0x14 1 -> 5 (the day).
	WRITE_LE_UINT16(&troopByte(id, kTroopBits), READ_LE_UINT16(&troopByte(id, kTroopBits)) & 0x3f0);
	WRITE_LE_UINT16(&troopByte(id, kTroopSpeech), READ_LE_UINT16(&troopByte(id, kTroopSpeech)) & 0xe5ff);
	troopByte(id, 0x14) = (byte)(_state.w(GameState::kGameTime) >> 4);
}

void World::dropHarvester(uint id) {
	if (id < 1 || id > kTroops || !(troopByte(id, 0x19) & 0x80))
		return;
	troopByte(id, 0x19) &= 0x7f;
	_log.line(Common::String::format("Troops: troop %u leaves its harvester", id));
}

bool World::mergeSmallTroop(uint id) {
	// seg000:6d19, from the troop walk (6c99) for a troop under 0x14 (x 10
	// men): not with occupation bits 0xe3 (moving, unhired, Harkonnen, jobs
	// with bits 0-1: prospecting, espionage ...), not with bitfield bit 7,
	// not the prospectors (0x8e0). The walk over the place's Fremen troops
	// (661d via 6906: occupation below 0x80; 6d5f skips 0xa0 and the
	// prospectors) picks the one with the fewest men that still fits in a
	// byte, the last of equals. It gets the men, the equipment of both
	// (the small troop keeps the common part), speech word 0x200 ("A small
	// troop has merged with us.", condition 528), and the small troop leaves
	// the game (66b1).
	const byte occ = troopByte(id, kTroopOccupation);
	if ((occ & 0xe3) || (troopByte(id, kTroopBits) & 0x80) || id == kProspectorTroop)
		return false;
	const int place = troopPlace(id);
	if (place < 0)
		return false;
	const byte pop = troopByte(id, kTroopPopulation);
	byte limit = (byte)~pop;
	uint into = 0;
	Common::Array<uint> ids;
	troopsAt((uint)place, ids);
	for (uint k = 0; k < ids.size(); ++k) {
		const uint o = ids[k];
		const byte oc = troopByte(o, kTroopOccupation);
		if (oc >= 0x80 || (oc & 0xa0) || o == kProspectorTroop || o == id)
			continue;
		const byte op = troopByte(o, kTroopPopulation);
		if (limit < op)
			continue;
		into = o;
		limit = op;
	}
	if (!into)
		return false;
	troopByte(into, kTroopPopulation) = (byte)(troopByte(into, kTroopPopulation) + pop);
	const byte eq = troopByte(id, kTroopEquipment);
	troopByte(id, kTroopEquipment) = (byte)(eq & troopByte(into, kTroopEquipment));
	troopByte(into, kTroopEquipment) |= eq;
	WRITE_LE_UINT16(&troopByte(into, kTroopSpeech), READ_LE_UINT16(&troopByte(into, kTroopSpeech)) | 0x200);
	_log.line(Common::String::format("Troops: troop %u (%u men) merges into troop %u at place %d", id, pop * 10, into, place));
	const byte keep = troopByte(id, kTroopEquipment);
	removeFromPlay(id);
	troopByte(id, kTroopEquipment) = keep; // 66b1 leaves the record's equipment byte
	return true;
}

void World::skillDecay(uint id) {
	// seg000:6d7b, after every job handler (6cc0): when the clock word is a
	// multiple of 64, ax = 0xc000 rotated left by the job's class (occupation
	// >> 2), masked with the speech word; bit 15 takes 1 from the ecology
	// skill (0x18), bit 14 from the army skill (0x17), bit 13 from the spice
	// skill (0x16), none below 0. So a spice troop can lose army and ecology,
	// an army troop only ecology, an ecology troop nothing.
	if (_state.w(GameState::kGameTime) & 0x3f)
		return;
	const uint cls = (troopByte(id, kTroopOccupation) & 0x0f) >> 2;
	const uint16 mask = (uint16)(((0xc000u << cls) | (0xc000u >> (16 - cls))) & 0xffff) & READ_LE_UINT16(&troopByte(id, kTroopSpeech));
	static const uint kSkill[3] = { 0x18, 0x17, 0x16 };
	for (uint k = 0; k < 3; ++k)
		if ((mask & (0x8000 >> k)) && troopByte(id, kSkill[k]))
			--troopByte(id, kSkill[k]);
}

int World::raidSource(uint index, bool stage) {
	// harkonnen_pick_attack_target's source (CD 2047-2070, floppy 235f-2388):
	// the nearest hidden fortress when it is within 30 (ds:e2/e4), else the
	// nearest known one within 30 (ds:dc/de); not the Harkonnen palace;
	// holding Harkonnen troops and no attacking Fremen. -1 when none.
	if (stage)
		stageLocationForConditions(index);
	uint16 from;
	if (_state.w(0xe2) < 0x1e)
		from = _state.w(0xe4);
	else if (_state.w(0xdc) < 0x1e)
		from = _state.w(0xde);
	else
		return -1;
	const int src = placeIndex(from);
	if (src < 0 || src == 1)
		return -1;
	uint h, attacking;
	countHostiles((uint)src, h, attacking);
	return h && !attacking ? src : -1;
}

bool World::pickRaidTarget(uint &target, uint &source) {
	// harkonnen_pick_attack_target (CD 2017, floppy 232f; the same code):
	// among the sietches (type below 0x20) neither hidden nor in battle
	// (status 0x82), without an ill troop (1e24: speech 0x400), north of the
	// best so far (latitude below it, from 100): the place's figures are
	// staged (331e); it needs Fremen troops other than prospectors (ds:60 -
	// ds:63), a fortress within 30 (the nearest hidden one, ds:e2/e4, else
	// the nearest known one, ds:dc/de), not the Harkonnen palace, holding
	// Harkonnen troops and no attacking Fremen (5098). The northernmost
	// wins, the first of equals.
	int16 best = 100;
	bool found = false;
	for (uint i = 0; i < locationCount(); ++i) {
		const Location l = location(i);
		if (l.type >= 0x20 || (l.status & 0x82) || !(l.latitude < best))
			continue;
		if (illTroopAt(i)) // CD 2034 -> 1e24 (floppy 234c -> 2169: its first troop only)
			continue;
		stageLocationForConditions(i);
		if (_state.b(0x60) == _state.b(0x63))
			continue;
		const int src = raidSource(i, false);
		if (src < 0)
			continue;
		best = l.latitude;
		target = i;
		source = (uint)src;
		found = true;
	}
	return found;
}

void World::harkonnenRaid() {
	// actions_time_in_day_4 (CD 1f64, floppy 227c): from phase 0x3c, or 7
	// days (0x70 periods) after Stilgar's stamp (ds:1154, phase 0x2c), on an
	// even day, unless the byte an attack period sets says to skip once, and
	// on the rolled-out bit 15 of ds:0 (rol: one time in two).
	if (ConfMan.hasKey("dune_no_raids"))
		return; // test setups about something else (the epidemic check)
	if (_state.b(GameState::kPhase) < 0x3c) {
		const uint16 stamp = READ_LE_UINT16(&var(0x1154));
		const uint16 now = _state.w(GameState::kGameTime);
		if (now < stamp || (uint16)(now - stamp) < 0x70)
			return;
	}
	if (_state.w(GameState::kGameTime) & 0x10)
		return;
	const byte skip = _state.vars[raidSuppressOffset()];
	_state.vars[raidSuppressOffset()] = 0;
	if (skip)
		return;
	const uint16 r = _state.w(0);
	_state.setW(0, (uint16)((r << 1) | (r >> 15)));
	if (!(r & 0x8000))
		return;
	uint target, source;
	if (!pickRaidTarget(target, source))
		return;
	_state.setB(0xc4, (byte)(_state.b(0xc4) + 1)); // sietches attacked
	// 1f9b-1fc9: twice, the first Harkonnen troop (bitfield 0x80) of the
	// fort's chain gets occupation 0x8d, marches to the sietch, is no longer
	// hidden and arrives at once.
	for (uint n = 0; n < 2; ++n) {
		Common::Array<uint> ids;
		troopsAt(source, ids);
		uint raider = 0;
		for (uint k = 0; k < ids.size() && !raider; ++k)
			if (troopByte(ids[k], kTroopBits) & kHarkonnenBit)
				raider = ids[k];
		if (!raider)
			break;
		troopByte(raider, kTroopOccupation) = 0x8d;
		issueMoveOrder(raider, target);
		troopByte(raider, kTroopBits) &= ~kHiddenBit;
		// 8357 lands it at once; the engine's march may already have (its
		// first sub-steps), and a second link would loop the chain.
		if (troopByte(raider, kTroopOccupation) & kMoving)
			troopArrive(raider);
		_log.line(Common::String::format("Raid: Harkonnen troop %u from place %u attacks sietch %u", raider, source, target));
	}
	locationByte(target, 10) |= 0x02; // in battle
	startAttack(target);              // 83fd: the troops there defend it
	// 140ae: the characters staying there (record word 2 = 0x80, place + 1)
	// go to its room 1.
	const uint16 here = (uint16)(((target + 1) << 8) | 0x80);
	const uint16 room1 = (uint16)((location(target).type << 8) | 1);
	for (uint c = 0; c < 9; ++c) {
		byte *rec = _state.vars + kCharacterTable + c * kCharacterSize;
		if (READ_LE_UINT16(rec + 2) == here)
			WRITE_LE_UINT16(rec, room1);
	}
	// "The Harkonnens are attacking ...!" (0x0c), or the prospectors'
	// warning when they are there (0x0d), from a troop chief (71b2).
	const bool prospectors = troopPlace(kProspectorTroop) == (int)target;
	queueVision(prospectors ? 0x0f0d : 0x0f0c, placeOffset(target));
	_log.line(Common::String::format("Raid: the Harkonnens attack sietch %u (from place %u)", target, source));
	if (_state.w(6) == here) {
		// 2000-2010: Paul is there: room 1, and the night battle (ds:2b).
		_state.setW(GameState::kLocationAndRoom, room1);
		_state.setB(0x0b, 1);
		_state.setB(0x2b, 1);
		seedBattleGauge(target); // 2010 -> 6144
		_log.line("Raid: Paul is in the attacked sietch");
	}
}

void World::troopNewDay(uint id, uint index) {
	// troop_location_do_stuff_upon_new_day (CD 6e20, floppy 7b88; the same
	// code), called first by the spice (6fe5 / 7d4e), army (71ef / 7f58)
	// and irrigation (76cb / 842f) handlers; it acts on the first period of
	// a day (ds:46de / ds:423a).
	if (timeSlot() != 0 || index >= locationCount())
		return;
	if (!fortressConversion(index))
		return;
	byte *l = &locationByte(index, 0);
	// 6cfc (floppy 7a64): the ring round a sietch (type below 0x20) grows
	// by one a day for each working troop there, up to 12, unless it is
	// irrigated (status bit 0: byte 0x0b is the vegetation's disc then);
	// the disc becomes Atreides land (644e, stage 0x20; vegetation cells
	// keep theirs). The first troop rallied there set it to 2 (6704).
	if (l[8] < 0x20 && l[11] < 12 && !(l[10] & 0x01)) {
		++l[11];
		paintArea(index, 0x20, l[11]);
	}
	// 6e4e (floppy 7bb6): more than 8 days since the troop's byte 0x14 (the
	// day it rallied, 6701, or its last map contact, 7b89): motivation - 1.
	const byte day = (byte)(_state.w(GameState::kGameTime) >> 4);
	if ((byte)(day - troopByte(id, 0x14)) > 8)
		lowerMotivation(id, 1);
	fremenQuarrel(id, index);
}

void World::fremenQuarrel(uint id, uint index) {
	// The north/south quarrel, the tail of the new-day routine that every
	// spice, army and irrigation troop runs first (floppy sub_9A58 9A95-9AB8,
	// CD troop_location_do_stuff_upon_new_day 6e20). Only the troop at the
	// head of its place's chain runs it, once for the place.
	if (troopByte(id, 0) != locationByte(index, 9))
		return;
	// Callback 7BEA (CD 6e82): nothing where Paul is, nothing outside a
	// sietch or the palace (type >= 0x21); the troops that count are content
	// below motivation 40 (0x28) and spice mining (occupation & 0x2f == 0:
	// job 0, stopped/moving/unhired bits allowed). Each sets bit 1 for the
	// north, bit 2 for the south (troop byte 0x12 bit 7, seg000:01e0).
	if (index == currentLocation() || location(index).type >= 0x21)
		return;
	Common::Array<uint> ids;
	troopsAt(index, ids);
	byte sides = 0;
	for (uint k = 0; k < ids.size(); ++k) {
		if (troopByte(ids[k], kTroopMotivation) >= 0x28 || (troopByte(ids[k], kTroopOccupation) & 0x2f))
			continue;
		sides |= (troopByte(ids[k], kTroopSpeech) & 0x80) ? 2 : 1;
	}
	if (sides != 3)
		return;
	// Both halves: callback 7C10 (CD 6ea8) stops every troop there below
	// motivation 40 that mines or trains for the army (occupation & 0x2b ==
	// 0): sub_9CBE sets occupation bit 4 and the speech byte gets bit 4, the
	// flag the job dispatcher skips (6c92, word 0x430) and the chiefs' lines
	// read (PHRASE12 248-251, staged at ds:34). Then Duncan's telepathic
	// message type 2 about the place, "Nothing coming from ... I wonder what's
	// going on there!" (sub_4BB0 ax=0x302, CD 6e77).
	for (uint k = 0; k < ids.size(); ++k) {
		if (troopByte(ids[k], kTroopMotivation) >= 0x28 || (troopByte(ids[k], kTroopOccupation) & 0x2f & 0xfb))
			continue;
		troopByte(ids[k], kTroopOccupation) |= Troop::kStopped;
		troopByte(ids[k], kTroopSpeech) |= 0x10;
		_log.line(Common::String::format("Troops: troop %u quarrels at place %u (Fremen from the %s)", ids[k], index,
				(troopByte(ids[k], kTroopSpeech) & 0x80) ? "south" : "north"));
	}
	queueVision(0x302, placeOffset(index));
}

void World::applyJob(uint id, byte job) {
	// seg000:6ad4 / 6acb: the job with its clocks restarted.
	troopByte(id, kTroopOccupation) = job; // seg000:6aea: the whole byte
	troopByte(id, kTroopSpeech) &= 0xcf;   // 6aed: the quarrel/refusal bits 4-5 go (floppy sub_973E 9759)
	if (job != Troop::kWaitingForOrders)
		troopByte(id, kTroopSpeech + 1) |= (byte)(0x20 << ((job & 0x0f) >> 2)); // 6b06
	WRITE_LE_UINT16(&troopByte(id, kTroopTime), _state.w(GameState::kGameTime));
	WRITE_LE_UINT16(&troopByte(id, kTroopDepC), 0);
	WRITE_LE_UINT16(&troopByte(id, kTroopDepE), 0);
}

bool World::issueMoveOrder(uint id, uint dest) {
	// troop_issue_move_order, seg000:84a6.
	if (id < 1 || id > kTroops || dest >= locationCount())
		return false;
	byte &occ = troopByte(id, kTroopOccupation);
	if (id == kProspectorTroop) {
		// prospector_sync_destination_queue (seg000:848f): drop the heads
		// already reached (the head is where the troop stands, not moving).
		uint16 head = prospectorDestination(0);
		while (head && head == READ_LE_UINT16(&troopByte(id, kTroopLocation)) && !(occ & kMoving)) {
			shiftProspectorQueue();
			head = prospectorDestination(0);
		}
		const int next = placeIndex(head);
		if (next < 0)
			return false;
		dest = (uint)next;
	}
	if (occ & kMoving) {
		WRITE_LE_UINT16(&troopByte(id, kTroopLocation), placeOffset(dest));
		if ((occ & 3) == 3)
			occ &= ~3;
		return true;
	}
	const int from = troopPlace(id);
	const Location d = location(dest);
	// 84d2 (floppy 91ce): the rest of this block, the defence check, the
	// refusal and the motivation cost, is for a troop whose occupation byte
	// is 6 (attacking) only; any other troop just marches (checked on
	// Spice86: a spice troop's GO & SEARCH keeps its motivation).
	const bool attacking = occ == 6;
	if (attacking && d.type < Location::kFortressMin && (d.status & 0x02)) {
		// The executable leaves the troop unlinked and still here (an
		// original bug, spec section 2.2); refuse the order instead.
		_log.line(Common::String::format("Troops: troop %u cannot march to place %u under attack", id, dest));
		return false;
	}
	// troop_06ebf (floppy sub_9AF7, called first at B096): the troops of the
	// place being left that quarrel (speech bit 4, see fremenQuarrel) take
	// their job again (troop_06ad4 with their own occupation), so moving a
	// troop away settles the quarrel. The marching troop is still linked.
	if (from >= 0) {
		Common::Array<uint> left;
		troopsAt((uint)from, left);
		for (uint k = 0; k < left.size(); ++k)
			if (troopByte(left[k], kTroopSpeech) & 0x10) {
				applyJob(left[k], troopByte(left[k], kTroopOccupation) & 0x0f);
				_log.line(Common::String::format("Troops: troop %u stops quarrelling at place %d", left[k], from));
			}
	}
	unlinkTroop(id);
	if (from >= 0) {
		if ((occ & 0x0f) == 6 && (location((uint)from).status & 0x02)) {
			uint h, attacking;
			countHostiles((uint)from, h, attacking);
			if (!attacking)
				battleLost((uint)from);
		}
		registerEquipment(id, (uint)from, -1);
		const Location f = location((uint)from);
		WRITE_LE_UINT16(&troopByte(id, kTroopLongitude), f.longitude);
		WRITE_LE_UINT16(&troopByte(id, kTroopLatitude), (uint16)f.latitude);
	}
	if (attacking && d.type < Location::kFortressMin)
		lowerMotivation(id, 3); // seg000:84fe -> 6f93
	WRITE_LE_UINT16(&troopByte(id, kTroopLocation), placeOffset(dest));
	occ |= kMoving;
	troopByte(id, kTroopSlot) = 0;
	_log.line(Common::String::format("Troops: troop %u marches from place %d to place %u", id, from, dest));
	if (!(troopByte(id, kTroopBits) & kHiddenBit))
		travelSubsteps(id, 7);
	return true;
}

bool World::travelSubstep(uint id) {
	// seg000:8604: one unit along the dominant gap; a gap under 7 cells is arrival.
	const int dest = troopPlace(id);
	if (dest < 0)
		return true;
	const Location d = location((uint)dest);
	uint16 lng = READ_LE_UINT16(&troopByte(id, kTroopLongitude));
	int16 lat = (int16)READ_LE_UINT16(&troopByte(id, kTroopLatitude));
	const uint cells = MAX<uint>(1, rowCells(lat));
	const uint unit = MAX<uint>(1, 65536 / cells);
	const int dLng = (int16)(d.longitude - lng);
	const int dLat = d.latitude - lat;
	const uint gapLng = (uint)ABS(dLng) / unit, gapLat = (uint)ABS(dLat);
	if (MAX(gapLng, gapLat) < 7)
		return true;
	const bool minor = (lcgRand() & 1) != 0;
	if (gapLng >= gapLat) {
		lng = (uint16)(lng + (dLng > 0 ? (int)unit : -(int)unit));
		if (minor && dLat)
			lat = (int16)(lat + (dLat > 0 ? 1 : -1));
	} else {
		lat = (int16)(lat + (dLat > 0 ? 1 : -1));
		if (minor && gapLng)
			lng = (uint16)(lng + (dLng > 0 ? (int)unit : -(int)unit));
	}
	WRITE_LE_UINT16(&troopByte(id, kTroopLongitude), lng);
	WRITE_LE_UINT16(&troopByte(id, kTroopLatitude), (uint16)lat);
	return false;
}

void World::travelSubsteps(uint id, uint n) {
	for (uint k = 0; k < n; ++k)
		if (travelSubstep(id)) {
			troopArrive(id);
			return;
		}
}

void World::troopTravelStep(uint id) {
	// troop_travel_step, seg000:8308: 4 sub-steps a period, 8 with an ornithopter.
	travelSubsteps(id, (troopByte(id, kTroopEquipment) & 0x40) ? 8 : 4);
}

int World::searchedEquipment(uint id) const {
	// The three GO & SEARCH handlers (CD 7734 army, 775c ecology, 776d
	// spice). The class is (occupation & 0x0f) >> 2 (troop_get_occupation_
	// bits_2_and_3, 693b), which picks the menu and so the handler.
	if (id < 1 || id > kTroops)
		return -1;
	const byte *r = _state.vars + kTroopTable + (id - 1) * kTroopSize;
	const byte eq = r[kTroopEquipment];
	const uint cls = (r[kTroopOccupation] & 0x0f) >> 2;
	if (cls == 0) {
		// 776d: a harvester (bit 7), then an orni (bit 6).
		if (!(eq & 0x80))
			return 0;
		return (eq & 0x40) ? -1 : 1;
	}
	if (cls == 1) {
		// 7734: krys (0x20), laser guns (0x10), weirding modules (0x08), atomics (0x04).
		for (uint item = 2; item <= 5; ++item)
			if (!(eq & (0x80 >> item)))
				return (int)item;
		return -1;
	}
	// 775c: bulbs (0x02).
	return (eq & 0x02) ? -1 : 6;
}

bool World::searchEquipmentHere(uint id, uint item) {
	// seg000:77d7 (CD only): the free count at the troop's own place (7f27,
	// the troop itself counted out) must reach 1, or 2 for an orni at Paul's
	// place (ds:1150, the place Paul is at or last left: currentLocation()).
	const int here = troopPlace(id);
	if (here < 0 || item >= 7)
		return false;
	byte counts[7];
	placeFreeEquipment((uint)here, counts);
	const byte need = (item == 1 && (uint)here == currentLocation()) ? 2 : 1;
	if (counts[item] < need)
		return false;
	const byte before = troopByte(id, kTroopEquipment);
	troopByte(id, kTroopEquipment) |= (byte)(0x80 >> item);
	const byte after = troopByte(id, kTroopEquipment);
	// troop_07d81: ds:3d the items added, ds:3e those removed, ds:3f = 0x40
	// for an orni taken where Paul is ("Thanks for giving me your orni!").
	const byte changed = before ^ after;
	_state.setB(0x3d, after & changed);
	_state.setB(0x3e, before & changed);
	_state.setB(0x3f, ((after & changed & 0x40) && (uint)here == currentLocation()) ? 0x40 : 0);
	// 7db1: an irrigation troop's viability is checked again (6c15).
	if ((troopByte(id, kTroopOccupation) & 0x0f) == Troop::kIrrigation)
		troopByte(id, kTroopOccupation) &= (byte)~Troop::kStopped;
	_log.line(Common::String::format("Troops: troop %u takes item %u at its own place %d (equipment %#x)", id, item, here, after));
	return true;
}

int World::equipmentSearchTarget(uint id, uint item, uint *distance) const {
	// seg000:7f90 (floppy 8ca1).
	if (id < 1 || id > kTroops || item >= 7)
		return -1;
	const uint16 hereOffset = READ_LE_UINT16(_state.vars + kTroopTable + (id - 1) * kTroopSize + kTroopLocation);
	const int here = placeIndex(hereOffset);
	if (here < 0)
		return -1;
	const Location h = location((uint)here);
	uint16 best = 0xffff;
	int target = -1;
	for (uint i = 2; i < locationCount(); ++i) { // di = 0x138: the palaces are not searched
		const Location l = location(i);
		if ((l.status & 0x80) || l.type >= Location::kFortressMin || (int)i == here)
			continue;
		const int16 dLng = (int16)(uint16)(l.longitude - h.longitude);
		const int16 dLat = (int16)(uint16)(l.latitude - h.latitude);
		const uint16 aLng = (uint16)(dLng < 0 ? -dLng : dLng);
		const uint16 aLat = (uint16)(dLat < 0 ? -dLat : dLat);
		// 7fcf-7fe6: dl = |dlng| >> 8; the larger (by low bytes) of that and
		// |dlat|; 50 or more is too far; a village counts a quarter.
		uint16 dx = (uint16)((aLng >> 8) & 0xff);
		if ((byte)dx < (byte)aLat)
			dx = aLat;
		if ((byte)dx >= 0x32)
			continue;
		if (l.type >= Location::kVillageMin)
			dx >>= 2;
		if (dx >= best)
			continue;
		byte counts[7];
		placeFreeEquipment(i, counts);
		if (!counts[item])
			continue;
		// 8018: the troops already marching there for the same item (bits
		// 0-1 set, not on their way back) each take one.
		for (uint t = 1; t < kTroops; ++t) {
			const byte *r = _state.vars + kTroopTable + (t - 1) * kTroopSize;
			if (!(r[kTroopOccupation] & kMoving) || (r[kTroopOccupation] & 3) != 3)
				continue;
			if (READ_LE_UINT16(r + kTroopLocation) != placeOffset(i) || READ_LE_UINT16(r + kTroopDepC) == placeOffset(i) ||
					r[kTroopDepE] != item)
				continue;
			if (counts[item])
				--counts[item];
		}
		// 804c: one orni stays for Paul where he is, before the worms (phase 0x50).
		if (i == currentLocation() && _state.b(0x2a) < 0x50 && counts[1])
			--counts[1];
		if (!counts[item])
			continue;
		best = dx;
		target = (int)i;
	}
	if (distance)
		*distance = best;
	return target;
}

bool World::startEquipmentSearch(uint id, uint item, uint target) {
	// seg000:77b4: +0e = item | bit << 8, +0c = the place left, then 84a6.
	const int from = troopPlace(id);
	if (from < 0 || item >= 7)
		return false;
	WRITE_LE_UINT16(&troopByte(id, kTroopDepE), (uint16)(item | ((0x80u >> item) << 8)));
	WRITE_LE_UINT16(&troopByte(id, kTroopDepC), placeOffset((uint)from));
	_log.line(Common::String::format("Troops: troop %u goes from place %d to place %u to search for item %u", id, from, target, item));
	return issueMoveOrder(id, target);
}

void World::troopArrive(uint id) {
	// troop_arrive_at_destination, seg000:8357.
	const int dest = troopPlace(id);
	if (dest < 0)
		return;
	const uint index = (uint)dest;
	// seg000:8357: the prospectors drop their queue's head on reaching it.
	if (id == kProspectorTroop && prospectorDestination(0) == placeOffset(index))
		shiftProspectorQueue();
	const Location d = location(index);
	WRITE_LE_UINT16(&troopByte(id, kTroopLongitude), d.longitude);
	WRITE_LE_UINT16(&troopByte(id, kTroopLatitude), (uint16)d.latitude);
	byte &occ = troopByte(id, kTroopOccupation);
	if (friendlyPlace(index) && !(d.status & 0x02)) {
		// 83a7-83ba: a friendly place, not under attack; espionage and
		// attack (5, 6) read as 0 here. Occupation bits 0-1 set (3, 7, 0x0b)
		// is GO & SEARCH FOR EQUIPMENT.
		const byte low = occ & 0x0f;
		if (low != 5 && low != 6 && (low & 3) == 3) {
			const uint16 home = READ_LE_UINT16(&troopByte(id, kTroopDepC));
			uint16 wanted = READ_LE_UINT16(&troopByte(id, kTroopDepE));
			if (placeOffset(index) != home) {
				// seg000:841f (floppy 911b): at the searched place. The free
				// count (7f27: the stock less what the troops there hold; the
				// arriving troop is not linked) gives the item: the place's
				// count drops, the troop's bit is set and +0f (the bit still
				// wanted) is cleared. Either way the troop turns back without
				// stopping (+04 = +0c) and stays on the march.
				const uint item = wanted & 0xff;
				byte counts[7];
				placeFreeEquipment(index, counts);
				const bool found = item < 7 && counts[item];
				if (found) {
					byte &count = locationByte(index, 0x14 + item);
					count = count ? count - 1 : 0;
					troopByte(id, kTroopEquipment) |= (byte)(wanted >> 8);
					wanted &= 0x00ff;
				}
				WRITE_LE_UINT16(&troopByte(id, kTroopDepE), wanted);
				WRITE_LE_UINT16(&troopByte(id, kTroopLocation), home);
				_log.line(Common::String::format("Troops: troop %u reaches place %u searching for item %u: %s; it goes back to place %d",
						id, index, item, found ? "found, taken" : "none free", placeIndex(home)));
				return;
			}
			// seg000:844d: home again; bits 0-1 go and the class's first job
			// (0 spice mining, 4 military training, 8 irrigation) restarts.
			occ &= 0xfc;
			_log.line(Common::String::format("Troops: troop %u is back at place %u from the search (%s, equipment %#x)", id,
					index, (wanted & 0xff00) ? "nothing found" : "item brought", troopByte(id, kTroopEquipment)));
		}
	}
	occ &= ~kMoving;
	linkTroop(id, index);
	registerEquipment(id, index, +1);
	const byte job = occ & 0x0f;
	if ((occ & 0x80) && friendlyPlace(index) && !(d.status & 0x02)) {
		// 83c9-83ce: an occupation with bit 7 (a raiding Harkonnen troop,
		// 0x8d) keeps it.
		_log.line(Common::String::format("Troops: Harkonnen troop %u arrives at place %u", id, index));
		return;
	}
	if (friendlyPlace(index) && !(d.status & 0x02)) {
		applyJob(id, (job == 5 || job == 6) ? 4 : job);
		_log.line(Common::String::format("Troops: troop %u arrives at place %u", id, index));
		return;
	}
	applyJob(id, job);
	_log.line(Common::String::format("Troops: troop %u arrives at hostile place %u", id, index));
	if (job == 5 && d.hidden()) {
		markDiscovered(index);
		_log.line(Common::String::format("Troops: espionage reveals place %u", index));
		return;
	}
	if (locationByte(index, 9) == id) {
		battleWon(index);
		return;
	}
	startAttack(index);
}

void World::startAttack(uint index) {
	// seg000:83fd: every hired troop there attacks; a prospector just stops.
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		byte &occ = troopByte(ids[i], kTroopOccupation);
		if ((occ & 0x80) || (troopByte(ids[i], kTroopBits) & kHarkonnenBit) || (occ & 0x60))
			continue;
		if ((occ & 0x0f) == Troop::kProspecting)
			occ |= Troop::kStopped;
		else if ((occ & 0x0f) != 6)
			applyJob(ids[i], 6);
	}
	_log.line(Common::String::format("Battle: the troops at place %u attack", index));
}

} // namespace Dune

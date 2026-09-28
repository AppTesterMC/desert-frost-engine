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

void World::applyJob(uint id, byte job) {
	// seg000:6ad4 / 6acb: the job with its clocks restarted.
	troopByte(id, kTroopOccupation) = job; // seg000:6aea: the whole byte
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
	if (d.type < Location::kFortressMin && (d.status & 0x02)) {
		// The executable leaves the troop unlinked and still here (an
		// original bug, spec section 2.2); refuse the order instead.
		_log.line(Common::String::format("Troops: troop %u cannot march to place %u under attack", id, dest));
		return false;
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
	if (d.type < Location::kFortressMin) {
		// seg000:6f93: motivation - 3; below 5 the troop sulks.
		byte &m = troopByte(id, kTroopMotivation);
		m = (byte)(m >= 3 ? m - 3 : 0);
		if (m < 5) {
			m = 4;
			occ |= Troop::kStopped;
			WRITE_LE_UINT16(&troopByte(id, kTroopSpeech), READ_LE_UINT16(&troopByte(id, kTroopSpeech)) | 0x20);
		}
	}
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
	occ &= ~kMoving;
	linkTroop(id, index);
	registerEquipment(id, index, +1);
	const byte job = occ & 0x0f;
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

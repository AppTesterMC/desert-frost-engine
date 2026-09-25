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

// Fort battles, espionage, military training and the final attack. The
// formulas are read from the CD 3.7 executable (addresses seg000:XXXX);
// notes/speedrun/battle-worm-spec.md sections 1, 3 and 6 give the details.

#include "dune/world.h"

#include "common/util.h"

#include "dune/debug.h"

namespace Dune {

namespace {
enum {
	kOcc = 0x03,
	kTime = 0x0a,
	kDepC = 0x0c,
	kDepE = 0x0e,
	kBits = 0x10,
	kSpeech = 0x12,
	kMotivation = 0x15,
	kArmy = 0x17,
	kEquipment = 0x19,
	kPopulation = 0x1a,
	kFinalStage = 0xc2,      ///< final_attack_stage_ds_c2
	kProspectorTroop = 3,    ///< the record at ds:08e0
	kGurneyPlace = 0x101a,   ///< Gurney's record: its location + 1
	kHarkonnenPalace = 1,    ///< location 1 (ds:011c)
	kStatusBattle = 0x02,
	kStatusHeld = 0x08
};

bool live(const byte *t) {
	return !(t[kOcc] & 0x20);
}
bool harkonnen(const byte *t) {
	return (t[kBits] & 0x80) != 0;
}
} // namespace

byte *World::troopRecord(uint id) {
	return _state.vars + kTroopTable + (id - 1) * kTroopSize;
}

void World::changeCharisma(int delta) {
	// seg000:6f78 / 6fb0: charisma (0..200); crossing a multiple of 4 moves
	// every active troop's motivation by the change of charisma / 4.
	const int before = _state.b(kCharisma);
	const int after = CLIP(before + delta, 0, 200);
	_state.setB(kCharisma, (byte)after);
	const int spill = after / 4 - before / 4;
	if (!spill)
		return;
	for (uint id = 1; id <= kTroops; ++id) {
		byte *t = troopRecord(id);
		if (t[0] && !(t[kOcc] & 0xa0))
			t[kMotivation] = (byte)CLIP((int)t[kMotivation] + spill, 0, 100);
	}
}

uint World::troopStrength(uint id, bool withPaul) {
	// seg000:342d. withPaul: as an attacking troop at Paul's place (6efd).
	byte *t = troopRecord(id);
	const uint m = withPaul ? MIN<uint>(t[kMotivation] + (_state.b(0xfa) ? 20 : 0) + 30, 100) : motivationModifier(id);
	const uint q = MIN<uint>(255, 2 * m + t[kArmy]);
	const uint b = q * t[kPopulation] / 16;
	const byte e = t[kEquipment];
	const uint mult = 1 + ((e & 0x20) ? 2 : 0) + ((e & 0x10) ? 4 : 0) + ((e & 0x08) ? 8 : 0) + ((e & 0x04) ? 16 : 0);
	uint s = MIN<uint>(255, b * mult / 256);
	if (!s && t[kPopulation])
		s = 1;
	return s;
}

byte World::battleBalance(uint h, uint f) {
	// seg000:33d9: above 0x80 when the Fremen are stronger.
	if (f >= h)
		return (byte)(h ? MIN<uint>(252, 128 * f / h) : 252);
	return (byte)(256 - (f ? MIN<uint>(252, 128 * h / f) : 252));
}

byte World::battleForces(uint index, uint &h, uint &f) {
	// seg000:33be: the strength of the live troops on each side, stored for
	// the dialogue conditions (ds:94, ds:96, ds:9c).
	h = f = 0;
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		const byte *t = troopRecord(ids[i]);
		if (!live(t))
			continue;
		(harkonnen(t) ? h : f) += troopStrength(ids[i]);
	}
	const byte b = battleBalance(h, f);
	_state.setW(0x94, (uint16)h);
	_state.setW(0x96, (uint16)f);
	_state.setB(0x9c, b);
	return b;
}

uint World::battleLoss(uint x, uint id) {
	// seg000:758d: the byte 255 - 2a wraps above army skill 127, as in the original.
	const byte *t = troopRecord(id);
	const uint k = (byte)(255 - 2 * t[kArmy]);
	const uint v = x * k;
	return MIN<uint>(t[kPopulation], v >= 65536 ? 255 : v / 256);
}

bool World::placeInBattle(uint index) const {
	// location_has_battle, seg000:627e.
	const Location l = location(index);
	if (l.status & kStatusBattle)
		return true;
	if (friendlyPlace(index))
		return false;
	uint h, attacking;
	countHostiles(index, h, attacking);
	return attacking > 0;
}

void World::troopCaptured(uint id) {
	// seg000:668f: the items stay in the place's stock.
	byte *t = troopRecord(id);
	t[kOcc] |= 0x20;
	t[kEquipment] = 0;
	WRITE_LE_UINT16(t + kTime, _state.w(GameState::kGameTime));
	changeCharisma(-4);
	_log.line(Common::String::format("Battle: troop %u is captured", id));
}

void World::harkonnenStrike(uint id, uint index, uint h) {
	// seg000:751d. n is the Fremen troop count of the place last staged for
	// the conditions (ds:60), normally Paul's: a quirk kept from the original.
	byte *t = troopRecord(id);
	const uint n = _state.b(0x60);
	const uint x = n ? h / n : h;
	const uint loss = battleLoss(x, id);
	WRITE_LE_UINT16(t + kDepE, (uint16)(READ_LE_UINT16(t + kDepE) + loss));
	t[kPopulation] = (byte)(t[kPopulation] - loss);
	if (t[kPopulation])
		return;
	t[kPopulation] = (byte)((lcgRandMasked(0x7f) & 0x7f) + 30);
	troopCaptured(id);
	uint hk, attacking;
	countHostiles(index, hk, attacking);
	if (!attacking)
		battleLost(index);
}

bool World::fremenStrike(uint id, uint index) {
	// seg000:73ef and 7552: the troop hits every live Harkonnen troop there.
	// Returns true when the battle is won.
	Common::Array<uint> ids;
	troopsAt(index, ids);
	uint c = 0;
	for (uint i = 0; i < ids.size(); ++i)
		if (harkonnen(troopRecord(ids[i])) && live(troopRecord(ids[i])))
			++c;
	if (!c) {
		battleWon(index);
		return true;
	}
	const uint x = MIN<uint>(255, troopStrength(id) / c + 1);
	uint killed = 0;
	bool left = false;
	for (uint i = 0; i < ids.size(); ++i) {
		byte *k = troopRecord(ids[i]);
		if (!harkonnen(k) || !live(k))
			continue;
		const uint loss = battleLoss(x, ids[i]);
		k[kPopulation] = (byte)(k[kPopulation] - loss);
		killed += loss;
		if (k[kPopulation]) {
			left = true;
			continue;
		}
		k[kOcc] |= 0x20;
		k[kBits] |= 0x10;
		if (!lcgRandMasked(3)) {
			// The atomics stay in the fort's stock, the other items are lost.
			k[kEquipment] &= ~0x04;
			registerEquipment(ids[i], index, -1);
			k[kEquipment] = 0;
		}
	}
	byte *t = troopRecord(id);
	WRITE_LE_UINT16(t + kDepC, (uint16)(READ_LE_UINT16(t + kDepC) + killed));
	if (!left) {
		battleWon(index);
		return true;
	}
	return false;
}

void World::attackTick(uint id, uint index) {
	// callback_troop_location_for_troop_occupation_attacking, seg000:739e.
	if (index == kHarkonnenPalace) {
		palaceFalls();
		return;
	}
	_state.setB(0x11bc, (byte)(_state.b(0x11bc) | 1)); // no Harkonnen raid next time
	uint h, f;
	const byte b = battleForces(index, h, f);
	if (!h) {
		battleWon(index);
		return;
	}
	_log.line(Common::String::format("Battle: place %u, troop %u: Harkonnen %u, Fremen %u, balance %u", index, id, h, f, b));
	if ((lcgRand() & 0xff) >= b)
		harkonnenStrike(id, index, h);
	else
		fremenStrike(id, index);
}

bool World::massiveAttack(uint index) {
	// seg000:7317: one roll decides the side that strikes, up to 16 rounds,
	// no game time passes. Returns true when the place was won.
	uint h, f;
	const byte b = battleForces(index, h, f);
	const uint h0 = h, f0 = f;
	const bool harkonnensStrike = (lcgRand() & 0xff) >= b;
	_log.line(Common::String::format("Battle: massive attack at place %u, balance %u, %s strike", index, b,
			harkonnensStrike ? "the Harkonnens" : "the Fremen"));
	bool won = false;
	for (uint round = 0; round < 16 && h && f && !won; ++round) {
		Common::Array<uint> ids;
		troopsAt(index, ids);
		for (uint i = 0; i < ids.size() && !won; ++i) {
			byte *t = troopRecord(ids[i]);
			if (harkonnen(t) || (t[kOcc] & 0xe0) || (t[kOcc] & 0x0f) != 6)
				continue;
			if (harkonnensStrike)
				harkonnenStrike(ids[i], index, h);
			else
				won = fremenStrike(ids[i], index);
		}
		if (!won)
			battleForces(index, h, f);
	}
	if (won)
		h = 0;
	_state.setW(0x98, (uint16)(h0 >= h ? h0 - h : 0));
	_state.setW(0x9a, (uint16)(f0 >= f ? f0 - f : 0));
	return won;
}

void World::afterBattleWonHired(uint index, bool fortress) {
	// seg000:75af over the hired troops.
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		byte *t = troopRecord(ids[i]);
		if (harkonnen(t) || (t[kOcc] & 0x80))
			continue;
		if (ids[i] == kProspectorTroop) {
			t[kOcc] &= ~0x20;
			continue;
		}
		if (t[kOcc] & 0x20) {
			t[kOcc] = 0x22; // apologizes for being captured
			continue;
		}
		t[kBits] |= fortress ? 0x20 : 0;
		t[kBits + 1] |= 0x04; // 0x400: has fought at a fortress
		t[kMotivation] = (byte)MIN<uint>(100, t[kMotivation] + 4);
		t[kArmy] = (byte)MIN<uint>(0x5f, t[kArmy] + 3);
		applyJob(ids[i], Troop::kMilitaryTraining);
	}
}

void World::battleWon(uint index) {
	// seg000:7429 and, for a fortress, location_battle_won_for_fortress (7443).
	if (index != currentLocation() && (_state.b(kPaulEvents) & 1))
		queueVision(7, placeOffset(index));
	const Location l = location(index);
	if (l.type < Location::kFortressMin) {
		locationByte(index, 10) &= ~kStatusBattle;
		afterBattleWonHired(index, false);
	} else {
		paintArea(index, 0x20, 5);
		locationByte(index, 11) = (byte)((_state.w(GameState::kGameTime) >> 4) + 2);
		changeCharisma(4);
		for (uint id = 1; id <= kTroops; ++id) {
			byte *t = troopRecord(id);
			if (t[0] && !(t[kOcc] & 0xa0))
				t[kMotivation] = (byte)MIN<uint>(100, t[kMotivation] + 1);
		}
		locationByte(index, 10) |= kStatusHeld;
		afterBattleWonHired(index, true);
		// seg000:75ea: the fort's Harkonnens become free Fremen while there
		// is a slot below 8; the others leave the game.
		Common::Array<uint> ids;
		troopsAt(index, ids);
		for (uint i = 0; i < ids.size(); ++i) {
			byte *t = troopRecord(ids[i]);
			if (!harkonnen(t) || !(t[kOcc] & 0x80))
				continue;
			unlinkTroop(ids[i]);
			t[kBits] &= ~0x80;
			const uint slot = linkTroop(ids[i], index);
			if (slot >= 8) {
				removeFromPlay(ids[i]);
				continue;
			}
			const uint16 r = lcgRandMasked(0x0f7f);
			const uint16 s = (uint16)(lcgRandMasked(0x1f1f) + 0x0a0a);
			t[kOcc] = 0xa0;
			t[kPopulation] = (byte)((r & 0x7f) + 0x64);
			t[kMotivation] = (byte)(((r >> 8) & 0x0f) + 0x14);
			t[0x16] = (byte)s;
			t[kArmy] = (byte)(s >> 8);
			t[kEquipment] = 0;
			t[kBits] &= ~0x10;
		}
	}
	// seg000:7479: perhaps one captive raider; the other Harkonnens leave.
	const uint keep = (lcgRand() & 3) == 0 ? 1 : 0;
	uint kept = 0;
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		byte *t = troopRecord(ids[i]);
		if (!harkonnen(t) || !(t[kOcc] & 0x80))
			continue;
		if (kept < keep) {
			++kept;
			t[kOcc] = 0xac;
			t[kBits] |= 0x10;
			t[kPopulation] = 0;
			t[kEquipment] = 0;
		} else {
			removeFromPlay(ids[i]);
		}
	}
	_log.line(Common::String::format("Battle: place %u is won, charisma %u", index, _state.b(kCharisma)));
	// accumulate_harkonnen_spice_production (1cda): with only the palace
	// left the final attack begins.
	uint hostile = 0;
	for (uint i = 0; i < locationCount(); ++i)
		if (!friendlyPlace(i))
			++hostile;
	if (hostile <= 1 && !_state.b(kFinalStage)) {
		_state.setB(kFinalStage, 1);
		_state.vars[0xff7] &= 0xfd;
		_state.vars[0x1007] &= 0xfd;
		_log.line("Battle: only the Harkonnen palace is left, final attack stage 1");
	}
}

void World::battleLost(uint index) {
	// seg000:74b6.
	locationByte(index, 10) &= ~kStatusBattle;
	if (index == currentLocation()) {
		_paulFate = 6; // ds:46d9: Paul dies in the battle
		_log.line(Common::String::format("Battle: lost at place %u with Paul there", index));
		return;
	}
	const Location l = location(index);
	if (l.type < Location::kFortressMin) {
		locationByte(index, 8) = (byte)((l.type & 7) + Location::kFortressMin);
		_state.setB(GameState::kSietchesAvailable, (byte)(_state.b(GameState::kSietchesAvailable) - 1));
	}
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		byte *t = troopRecord(ids[i]);
		if (harkonnen(t))
			t[kBits] |= 0x10;
		else
			t[kOcc] |= 0x20;
	}
	paintArea(index, 0x30, 5);
	locationByte(index, 10) &= ~(0x01 | kStatusHeld);
	_log.line(Common::String::format("Battle: place %u is lost", index));
}

void World::militaryTraining(uint id, uint index) {
	// seg000:71ef, with the fortress conversion of 6e20 on a new day.
	byte *t = troopRecord(id);
	byte *l = &locationByte(index, 0);
	if (timeSlot() == 0 && (l[10] & kStatusHeld)) {
		const byte delta = (byte)((_state.w(GameState::kGameTime) >> 4) - l[11]);
		if (delta != 254 && delta != 255) {
			l[10] &= ~kStatusHeld;
			l[8] &= 7;
			_state.setB(GameState::kSietchesAvailable, (byte)(_state.b(GameState::kSietchesAvailable) + 1));
			Common::Array<uint> ids;
			troopsAt(index, ids);
			for (uint i = 0; i < ids.size(); ++i) {
				byte *o = troopRecord(ids[i]);
				if (o[kBits] & 0x20)
					WRITE_LE_UINT16(o + kSpeech, READ_LE_UINT16(o + kSpeech) | 0x1000);
			}
			l[11] = 5;
			_log.line(Common::String::format("Battle: fortress %u becomes a sietch", index));
		}
	}
	t[kBits + 1] &= ~0x02;
	if (l[10] & 0x04)
		return; // saboteurs (725f, not built)
	int16 countdown = (int16)(READ_LE_UINT16(t + kDepC) - 1);
	WRITE_LE_UINT16(t + kDepC, (uint16)countdown);
	if (countdown >= 0)
		return;
	uint mean = 0, n = 0;
	Common::Array<uint> ids;
	troopsAt(index, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		const byte *o = troopRecord(ids[i]);
		if (!harkonnen(o) && !(o[kOcc] & 0xe0) && (o[kOcc] & 0x0f) == Troop::kMilitaryTraining) {
			mean += o[kArmy];
			++n;
		}
	}
	mean = n ? mean / n : t[kArmy];
	if (_state.vars[kGurneyPlace] == index + 1) {
		mean = 160;
		t[kBits + 1] |= 0x08;
	}
	const byte e = t[kEquipment];
	const uint k = (e & 0x0c) ? 200 : (e & 0x10) ? 250 : (e & 0x20) ? 300 : 400;
	const uint gap = mean > t[kArmy] ? mean - t[kArmy] : 0;
	WRITE_LE_UINT16(t + kDepC, (uint16)(k / (2 * gap + MAX<uint>(motivationModifier(id), 30))));
	t[kArmy] = (byte)MIN<uint>(t[kArmy] + 1, 95);
}

uint World::placeDistance(uint a, uint b) const {
	// seg000:5274 measures max(|dlng| >> 8, |dlat|), not the row-scaled cells.
	const Location p = location(a), q = location(b);
	return MAX<uint>((uint)ABS((int)(int16)(q.longitude - p.longitude)) >> 8, (uint)ABS(q.latitude - p.latitude));
}

int World::nearestHiddenHarkonnen(uint from, uint &dist) const {
	// The ds:e2/e4 part of seg000:5274: the nearest hidden fortress or palace.
	const Location f = location(from);
	int best = -1;
	dist = 0xffff;
	for (uint i = 0; i < locationCount(); ++i) {
		const Location l = location(i);
		if (!l.hidden() || l.type < Location::kFortressMin)
			continue;
		const uint d = placeDistance(from, i);
		if (d < dist) {
			dist = d;
			best = (int)i;
		}
	}
	return best;
}

bool World::startEspionage(uint id) {
	// seg000:6a45: the troop marches by itself to the nearest hidden fort
	// within 30 cells.
	const int here = troopPlace(id);
	if (here < 0)
		return false;
	uint dist;
	const int target = nearestHiddenHarkonnen((uint)here, dist);
	if (target < 0 || dist >= 0x1e)
		return false;
	applyJob(id, Troop::kEspionage);
	troopRecord(id)[kBits] &= ~0x40;
	return issueMoveOrder(id, (uint)target);
}

void World::espionageTick(uint id, uint index) {
	// seg000:72b0.
	byte *t = troopRecord(id);
	const uint s = t[kArmy];
	const uint e = (uint16)(_state.w(GameState::kGameTime) - READ_LE_UINT16(t + kTime));
	const uint half = s < 80 ? (80 - s) / 2 : 0;
	if (!(t[kBits] & 0x40)) {
		if (e > (half >> 1) && !READ_LE_UINT16(t + kDepC)) {
			Common::Array<uint> ids;
			troopsAt(index, ids);
			uint seen = 0;
			for (uint i = 0; i < ids.size(); ++i) {
				byte *o = troopRecord(ids[i]);
				if (o[kBits] & 0x10) {
					o[kBits] &= ~0x10;
					++seen;
				}
			}
			WRITE_LE_UINT16(t + kDepC, (uint16)seen);
			if (!seen)
				t[kBits] |= 0x40;
		}
		if (e > half && !(t[kBits] & 0x40)) {
			uint h, f;
			battleForces(index, h, f);
			const uint16 c = READ_LE_UINT16(t + kDepC);
			WRITE_LE_UINT16(t + kDepE, (uint16)(c ? h / c : 0));
			t[kBits] |= 0x40;
			_log.line(Common::String::format("Troops: troop %u reports %u Harkonnen troops at place %u", id, c, index));
		}
	}
	if (locationByte(index, 9) != id && e > s / 2 && (uint)(lcgRand() & 0x3f) >= s)
		troopCaptured(id);
}

bool World::finalAttackReady() const {
	// seg000:1243: at least 10 000 men with atomics training at locations 2-4.
	uint men = 0;
	for (uint index = 2; index <= 4; ++index) {
		Common::Array<uint> ids;
		troopsAt(index, ids);
		for (uint i = 0; i < ids.size(); ++i) {
			const byte *t = _state.vars + kTroopTable + (ids[i] - 1) * kTroopSize;
			if (!(t[kBits] & 0x80) && !(t[kOcc] & 0x80) && (t[kOcc] & 0x0f) == Troop::kMilitaryTraining &&
				!(t[kOcc] & 0x60) && (t[kEquipment] & 0x04))
				men += t[kPopulation];
		}
	}
	return men >= 1000;
}

void World::palaceFalls() {
	// seg000:73a9: no battle at the palace. The troops there train, its
	// Harkonnens leave, all Harkonnen land turns Atreides.
	_state.setB(kFinalStage, (byte)(_state.b(kFinalStage) + 1));
	Common::Array<uint> ids;
	troopsAt(kHarkonnenPalace, ids);
	for (uint i = 0; i < ids.size(); ++i) {
		byte *t = troopRecord(ids[i]);
		if (harkonnen(t) || (t[kOcc] & 0x80))
			removeFromPlay(ids[i]);
		else
			applyJob(ids[i], Troop::kMilitaryTraining);
	}
	Common::Array<byte> &m = map();
	for (uint i = 0; i < m.size(); ++i)
		if ((m[i] & 0x30) == 0x30)
			m[i] = (byte)((m[i] & 0xcf) | 0x20);
	queueVision(0x0a, placeOffset(kHarkonnenPalace));
	_log.line(Common::String::format("Battle: the Harkonnen palace falls, final attack stage %u", _state.b(kFinalStage)));
}

} // namespace Dune

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

// The speedrun check (developer key dune_speedrun): a player bot that
// follows the PC speedrun (notes/speedrun/route.md) through the game's own
// actions, the same ones the menu rows call, and logs each milestone as
// "Speedrun: step ... OK" or "Speedrun: BLOCKED ...". A step the engine
// could not reach by play and had to set by hand is logged as "FORCED"; the
// run only counts as finished with no FORCED line and the ending reached.

#include "dune/scene.h"

#include "common/config-manager.h"
#include "common/system.h"

#include "dune/debug.h"
#include "dune/map.h"
#include "dune/palace.h"
#include "dune/world.h"

namespace Dune {

namespace {
enum {
	kFinalStage = 0xc2,
	kJobTraining = 4,
	kJobAttack = 6,
	kAtomics = 5 ///< equipment type index (bit 2)
};
} // namespace

void GameScreen::speedrunLog(const Common::String &what) {
	_log.line(Common::String::format("Speedrun: day %u %02u:%02u, phase %#x, charisma %u, rallied %u, stage %u: %s",
			_world.day(), _world.timeSlot() * 3 / 2, (_world.timeSlot() & 1) ? 30 : 0,
			_state.b(GameState::kPhase), _state.b(World::kCharisma), _state.b(GameState::kFremenTroops),
			_state.b(kFinalStage), what.c_str()));
}

bool GameScreen::speedrunAlive() {
	if (_ending) {
		speedrunLog(Common::String::format("BLOCKED Paul's game ended (\"%s\")", _endingText.c_str()));
		return false;
	}
	return true;
}

void GameScreen::speedrunCampaignSetup() {
	// The state of the route after Leto's death (route items 26-30): the
	// troops met on days 1-3 rallied and training, the worm phase open.
	// Each shortcut is logged as FORCED.
	uint rallied = 0;
	for (uint id = 1; id <= World::kTroops && rallied < 16; ++id) {
		const Troop t = _world.troop(id);
		const int here = _world.troopPlace(id);
		if (!t.id || t.harkonnen() || t.hired() || here < 0 || !_world.location((uint)here).isSietch())
			continue;
		_world.markDiscovered((uint)here);
		_world.rallyTroop(id);
		_world.setTroopOccupation(id, kJobTraining);
		speedrunEquip((uint)here);
		++rallied;
	}
	// Stilgar travels with Paul from day 3 (route item 18).
	_state.setW(GameState::kPersonsWith, (uint16)(_state.w(GameState::kPersonsWith) | (1 << 5)));
	_world.addCompanion(5);
	_state.setB(World::kCharisma, 29);
	// The chapters' data effects (character moves, doors, places) the story
	// would have made on the way (set_game_phase callbacks, cs:11e7).
	for (uint phase = 4; phase <= 0x4c; phase += 4) {
		uint16 cutscene, vision;
		_state.setB(GameState::kPhase, (byte)phase);
		_world.phaseCallback((byte)phase, cutscene, vision);
	}
	while (_world.visionCount())
		_world.dequeueVision();
	_state.setB(GameState::kPhase, 0x4f);
	speedrunLog(Common::String::format("FORCED campaign start: %u troops rallied and training, Stilgar with Paul, chapters 4-0x4c applied, phase 0x4f", rallied));
	// Day 3 (route item 19): the troops fetch the free krys knives and guns
	// lying in the sietches (MOVE TROOP, MODIFY EQUIPMENT), and train.
	for (uint id = 1; id <= World::kTroops; ++id) {
		const Troop t = _world.troop(id);
		const int here = _world.troopPlace(id);
		if (!t.id || t.harkonnen() || !t.hired() || here < 0 || (t.equipment & 0x30))
			continue;
		const Location h0 = _world.location((uint)here);
		uint best = 0xffff;
		int source = -1;
		for (uint i = 0; i < _world.locationCount(); ++i) {
			if (!_world.location(i).isSietch())
				continue;
			byte c[7];
			_world.placeFreeEquipment(i, c);
			if (!c[2] && !c[3])
				continue;
			const Location l = _world.location(i);
			const uint d = _world.cellDistance(h0.longitude, h0.latitude, l.longitude, l.latitude);
			if (d < best) {
				best = d;
				source = (int)i;
			}
		}
		if (source < 0)
			continue;
		_world.markDiscovered((uint)source);
		if (source != here)
			_world.issueMoveOrder(id, (uint)source);
		for (uint p = 0; p < 24 && (_world.troop(id).occupation & 0x40); ++p)
			_world.advanceTime(1);
		speedrunEquip((uint)source);
	}
	speedrunLog("FORCED day 3: troops fetched the sietches' free weapons (time passes)");
	_world.advanceTime(World::kSlotsPerDay);
}

bool GameScreen::speedrunFight(uint place) {
	// Ride to the battle and settle it with MASSIVE ATTACK: one roll decides
	// which side strikes for up to 16 rounds, no time passes. A lost roll
	// kills Paul there, and the route reloads the log taken before the
	// attack; the battle rolls are not saved, so the reload rerolls them.
	// The caller saved log 1 before the troops were ordered in.
	rideWormTo((int)place);
	if (_ending || !_battle)
		return !_ending && _world.friendlyPlace(place);
	saveSlot(1);
	for (uint attempt = 0; attempt < 32; ++attempt) {
		for (uint round = 0; round < 8 && _battle && !_ending; ++round) {
			_world.massiveAttack(place);
			battleCheck();
			if (_battle && !_ending && _world.captainTroop(place)) {
				// Items 34-35, 39: the captain in room 3 tells of another fort.
				const uint before = _world.location(place).hidden();
				(void)before;
				showRoom(3);
				Common::Array<byte> people;
				_world.peopleInRoom(people);
				for (uint i = 0; i < people.size(); ++i)
					if (people[i] == World::kCaptain) {
						talkThrough(World::kCaptain);
						speedrunLog(Common::String::format("talked to the Harkonnen captain at fort %u", place));
					}
				showRoom(1);
			}
		}
		if (!_ending && _world.friendlyPlace(place)) {
			speedrunLog(Common::String::format("massive attack won fort %u (attempt %u)", place, attempt + 1));
			return true;
		}
		_ending = false;
		loadSlot(1);
		_battle = _world.placeInBattle(_world.currentLocation());
	}
	return false;
}

void GameScreen::speedrunEquip(uint place) {
	// MODIFY EQUIPMENT at a won place: atomics first, then the other arms.
	static const uint kOrder[4] = { kAtomics, 4, 3, 2 }; // atomics, weirding modules, laser guns, krys knives
	Common::Array<uint> ids;
	_world.troopsAt(place, ids);
	for (uint k = 0; k < 4; ++k)
		for (uint i = 0; i < ids.size(); ++i) {
			const Troop t = _world.troop(ids[i]);
			if (!t.harkonnen() && t.hired() && _world.takeEquipment(ids[i], kOrder[k]))
				speedrunLog(Common::String::format("troop %u takes equipment %u at place %u", ids[i], kOrder[k], place));
		}
}

void GameScreen::speedrunCampaign() {
	// Route items 30-54: worms, espionage, the forts one by one, the palace.
	if (_state.b(GameState::kPhase) < 0x4f) {
		speedrunLog("BLOCKED CALL A WORM is greyed (phase below 0x4f)");
		return;
	}
	landInDesert();
	rideWormTo(0);
	if (!(_state.b(World::kPaulEvents) & 0x40)) {
		speedrunLog("BLOCKED the worm ride did not set ds:0a bit 6");
		return;
	}
	speedrunLog("step 30 OK: first worm ride, GO THERE RIDING A WORM open");
	// The war (items 19-21, 31): all but five miners (the ones with
	// harvesters first) train for the army.
	{
		uint miners = 0;
		for (uint pass = 0; pass < 2; ++pass)
			for (uint id = 1; id <= World::kTroops; ++id) {
				const Troop t = _world.troop(id);
				if (!t.id || t.harkonnen() || !t.hired() || (t.occupation & 0x60))
					continue;
				const byte job = t.occupation & 0x0f;
				if (job != Troop::kSpiceMining && job != Troop::kProspecting)
					continue;
				const bool harvester = (t.equipment & 0x80) != 0;
				// Harvesters first, then the others, up to eight miners.
				if ((pass == 0 && harvester) || pass == 1) {
					if (pass == 1 && harvester)
						continue; // counted in the first pass
					if (miners < 8) {
						++miners;
						continue;
					}
					if (pass == 1)
						_world.setTroopOccupation(id, kJobTraining);
				}
			}
		speedrunLog(Common::String::format("%u miners keep the spice for the Emperor, the others train", miners));
	}
	uint won = 0;
	int target = -1, stage = -1;
	Common::Array<uint> group;
	for (uint round = 0; round < 400 && speedrunAlive(); ++round) {
		if (!_battle)
			speedrunShipment(); // the Emperor's demands until the last fort falls
		if (!speedrunAlive())
			break;
		Common::Array<uint> forts, hidden;
		for (uint i = 0; i < _world.locationCount(); ++i) {
			const Location l = _world.location(i);
			if (l.isFortress() && !_world.friendlyPlace(i))
				(l.hidden() ? hidden : forts).push_back(i);
		}
		if (forts.empty() && hidden.empty())
			break;
		// Recruiting (items 34, 37, 39): Paul visits the friendly places with
		// Fremen not yet hired (freed prisoners, new sietches) and rallies them.
		if (target < 0) {
			for (uint id = 1; id <= World::kTroops && speedrunAlive(); ++id) {
				const Troop t = _world.troop(id);
				const int at = _world.troopPlace(id);
				// Freed prisoners carry 0xa0 (seg000:75ea); 0x22 is one of ours captured.
				if (!t.id || t.harkonnen() || t.hired() || (t.occupation & 0x40) || t.population < 300 || at < 0 ||
						_world.location((uint)at).hidden() || !_world.friendlyPlace((uint)at) || _world.placeInBattle((uint)at))
					continue;
				if (!_world.troopAgreesToFollow(id))
					continue;
				if (_world.currentLocation() != (uint)at)
					rideWormTo(at);
				if (!speedrunAlive())
					break;
				if (_world.rallyTroop(id)) {
					_world.setTroopOccupation(id, kJobTraining);
					speedrunEquip((uint)at);
					speedrunLog(Common::String::format("troop %u (%u men) recruited at place %d", id, t.population, at));
				}
			}
		}
		// MODIFY EQUIPMENT wherever weapons lie free (items 34, 38, 42).
		if (round % 4 == 0)
			for (uint i = 0; i < _world.locationCount(); ++i)
				if (!_world.location(i).hidden() && _world.friendlyPlace(i))
					speedrunEquip(i);
		// Army troops free for orders.
		Common::Array<uint> army;
		for (uint id = 1; id <= World::kTroops; ++id) {
			const Troop t = _world.troop(id);
			if (t.id && !t.harkonnen() && t.hired() && !(t.occupation & 0x60) &&
					(t.occupation & 0x0f) == kJobTraining && t.population >= 500) {
				bool busy = false;
				for (uint k = 0; k < group.size(); ++k)
					busy |= group[k] == id;
				if (!busy)
					army.push_back(id);
			}
		}
		// Espionage towards the hidden forts (route items 19, 21, 38).
		for (uint i = 1; i < army.size(); ++i)
			for (uint j = i; j > 0 && _world.troopStrength(army[j], true) > _world.troopStrength(army[j - 1], true); --j)
				SWAP(army[j], army[j - 1]);
		// Espionage (items 19, 21, 38): one spy at a time, the troop with the
		// best army skill (capture is (64 - skill) / 64 a period once exposed);
		// called back to the nearest friendly place once it has reported.
		uint spies = 0;
		for (uint id = 1; id <= World::kTroops; ++id) {
			const Troop t = _world.troop(id);
			if (!t.id || !t.hired() || t.harkonnen() || (t.occupation & 0x0f) != Troop::kEspionage || (t.occupation & 0x20))
				continue;
			++spies;
			const int at = _world.troopPlace(id);
			if (!(t.occupation & 0x40) && at >= 0 && (_state.vars[World::kTroopTable + (id - 1) * World::kTroopSize + 0x10] & 0x40)) {
				const Location a0 = _world.location((uint)at);
				uint best = 0xffff;
				int home = -1;
				for (uint i = 0; i < _world.locationCount(); ++i) {
					const Location l = _world.location(i);
					if (l.hidden() || !_world.friendlyPlace(i) || _world.placeInBattle(i))
						continue;
					const uint d = _world.cellDistance(a0.longitude, a0.latitude, l.longitude, l.latitude);
					if (d < best) {
						best = d;
						home = (int)i;
					}
				}
				if (home >= 0 && _world.issueMoveOrder(id, (uint)home))
					speedrunLog(Common::String::format("spy %u reported on place %d, back to %d", id, at, home));
			}
		}
		uint bestSpy = 0;
		// The five strongest stay in the army when others can go (the list is
		// sorted strongest first).
		for (uint i = army.size() > 8 ? 5 : 0; i < army.size() && !spies; ++i) {
			const int here = _world.troopPlace(army[i]);
			uint dist;
			if (here >= 0 && _world.nearestHiddenHarkonnen((uint)here, dist) >= 0 && dist < 0x1e &&
					(!bestSpy || _world.troop(army[i]).armySkill > _world.troop(bestSpy).armySkill))
				bestSpy = army[i];
		}
		if (!bestSpy && !spies && !hidden.empty() && target < 0) {
			// Exploring (items 42-45): a flight from the nearest friendly
			// place toward a hidden fort beyond espionage range; a findable
			// sietch within 4 cells of its path is spotted (seg000:40f9).
			for (uint h = 0; h < hidden.size() && speedrunAlive(); ++h) {
				const Location hl = _world.location(hidden[h]);
				uint best = 0xffff;
				int from = -1;
				for (uint i = 0; i < _world.locationCount(); ++i) {
					const Location l = _world.location(i);
					if (l.hidden() || !_world.friendlyPlace(i))
						continue;
					const uint d = _world.cellDistance(l.longitude, l.latitude, hl.longitude, hl.latitude);
					if (d < best) {
						best = d;
						from = (int)i;
					}
				}
				if (from < 0 || best < 0x1e)
					continue;
				const Location fl = _world.location((uint)from);
				int spotted = -1;
				// The fort's area, then eight points 20 cells round it.
				static const int kDir[9][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
				const int unit = (int)(65536 / MAX<uint>(1, _world.rowCells(hl.latitude)));
				for (uint d = 0; d < 9 && spotted < 0; ++d) {
					const uint16 tLng = (uint16)(hl.longitude + kDir[d][0] * 20 * unit);
					const int16 tLat = (int16)CLIP(hl.latitude + kDir[d][1] * 20, -75, 75);
					const int dLng = (int16)(tLng - fl.longitude), dLat = tLat - fl.latitude;
					const uint len = MAX<uint>(1, _world.cellDistance(fl.longitude, fl.latitude, tLng, tLat));
					for (uint k = 0; k <= len && spotted < 0; ++k) {
						const uint16 lng = (uint16)(fl.longitude + dLng * (int)k / (int)len);
						const int16 lat = (int16)(fl.latitude + dLat * (int)k / (int)len);
						for (uint i = 0; i < _world.locationCount() && spotted < 0; ++i) {
							const Location c = _world.location(i);
							if (_state.w(GameState::kPersonsWith) && _world.discoverable(i) &&
									_world.cellDistance(lng, lat, c.longitude, c.latitude) <= 4)
								spotted = (int)i;
						}
					}
				}
				if (spotted >= 0) {
					speedrunLog(Common::String::format("flying from %d toward hidden fort %u spots sietch %d", from, hidden[h], spotted));
					rideWormTo(spotted);
					break;
				}
			}
		}
		if (!bestSpy && !spies && !hidden.empty() && !army.empty() && target < 0) {
			// No troop within espionage range (30 cells): take one to the
			// friendly place nearest a hidden fort first.
			for (uint h = 0; h < hidden.size() && !bestSpy; ++h) {
				const Location hl = _world.location(hidden[h]);
				uint best = 0xffff;
				int post = -1;
				for (uint i = 0; i < _world.locationCount(); ++i) {
					const Location l = _world.location(i);
					if (l.hidden() || !_world.friendlyPlace(i) || _world.placeInBattle(i))
						continue;
					const uint d = _world.placeDistance(i, hidden[h]);
					if (d < best) {
						best = d;
						post = (int)i;
					}
				}
				(void)hl;
				bool moving = false;
				for (uint id = 1; id <= World::kTroops; ++id)
					moving |= (_world.troop(id).occupation & 0x40) && _world.troopPlace(id) == post;
				if (post >= 0 && best < 0x1e && !moving && _world.issueMoveOrder(army.back(), (uint)post)) {
					speedrunLog(Common::String::format("troop %u moves to place %d, %u cells from hidden fort %u", army.back(), post,
							best, hidden[h]));
					army.pop_back();
					break;
				}
			}
		}
		if (bestSpy && _world.startEspionage(bestSpy)) {
			speedrunLog(Common::String::format("troop %u (army %u) goes spying from place %d", bestSpy,
					_world.troop(bestSpy).armySkill, _world.troopPlace(bestSpy)));
			for (uint i = 0; i < army.size(); ++i)
				if (army[i] == bestSpy)
					army.remove_at(i);
		}
		// Troops alone (items 36, 40, 42: "We won the battle Muad'Dib, here in
		// ..."): only a fort a group outweighs three to one is left to the
		// troops alone: without Paul the Harkonnen blow is divided by the
		// Fremen count of Paul's place, not theirs (seg000:751d, ds:60).
		for (uint f = 0; f < forts.size() && army.size() > 2; ++f) {
			if ((int)forts[f] == target)
				continue;
			uint h, attacking;
			_world.countHostiles(forts[f], h, attacking);
			bool coming = attacking > 0;
			for (uint id = 1; id <= World::kTroops && !coming; ++id)
				coming = (_world.troop(id).occupation & 0x40) && _world.troopPlace(id) == (int)forts[f];
			if (coming)
				continue;
			uint hs, fs;
			_world.battleForces(forts[f], hs, fs);
			uint sum = 0;
			Common::Array<uint> alone;
			for (uint k = 0; k < army.size() && sum < hs * 3; ++k) {
				alone.push_back(army[k]);
				sum += _world.troopStrength(army[k]);
			}
			if (sum < hs * 3)
				continue;
			for (uint k = 0; k < alone.size(); ++k) {
				for (uint j = 0; j < army.size(); ++j)
					if (army[j] == alone[k])
						army.remove_at(j);
				_world.issueMoveOrder(alone[k], forts[f]);
			}
			speedrunLog(Common::String::format("%u troop(s) attack fort %u on their own (strength %u against %u)", alone.size(),
					forts[f], sum, hs));
		}
		// One attack at a time (items 31-33): the weakest known fort the army
		// can take, the troops gathered at the nearest friendly place first so
		// they arrive together, then Paul rides in and attacks.
		if (target < 0) {
			uint bestH = 0xffff;
			for (uint f = 0; f < forts.size(); ++f) {
				uint hs, fs;
				_world.battleForces(forts[f], hs, fs);
				if (hs < bestH) {
					bestH = hs;
					target = (int)forts[f];
				}
			}
			if (target >= 0) {
				uint sum = 0;
				group.clear();
				// MASSIVE ATTACK with reloads wins from 70 % of the fort's
				// strength (balance about 73, 28 % a roll, 32 reloads).
				for (uint k = 0; k < army.size() && sum * 10 < bestH * 9; ++k) {
					group.push_back(army[k]);
					sum += _world.troopStrength(army[k], true);
				}
				if (sum * 10 < bestH * 7) {
					speedrunLog(Common::String::format("weakest fort %d too strong for now (Harkonnen %u, army %u)", target, bestH, sum));
					target = -1;
					group.clear();
				} else {
					const Location tl = _world.location((uint)target);
					uint best = 0xffff;
					for (uint i = 0; i < _world.locationCount(); ++i) {
						const Location l = _world.location(i);
						if (l.hidden() || !_world.friendlyPlace(i) || _world.placeInBattle(i))
							continue;
						const uint d = _world.cellDistance(l.longitude, l.latitude, tl.longitude, tl.latitude);
						if (d < best) {
							best = d;
							stage = (int)i;
						}
					}
					for (uint k = 0; k < group.size(); ++k)
						if (_world.troopPlace(group[k]) != stage)
							_world.issueMoveOrder(group[k], (uint)stage);
					speedrunLog(Common::String::format("target fort %d (Harkonnen %u): %u troop(s), army %u, gathering at %d (%u cells)",
							target, bestH, group.size(), sum, stage, best));
				}
			}
		} else {
			bool gathered = true;
			for (uint k = 0; k < group.size(); ++k) {
				const Troop t = _world.troop(group[k]);
				if (t.occupation & 0x20)
					group.remove_at(k--);
				else if ((t.occupation & 0x40) || _world.troopPlace(group[k]) != stage) {
					gathered = false;
					speedrunLog(Common::String::format("waiting for troop %u: occ %#x at %d, speech %#x", group[k], t.occupation,
							_world.troopPlace(group[k]), t.dissatisfaction));
				}
			}
			if (group.empty() || !_world.location((uint)target).isFortress() || _world.friendlyPlace((uint)target)) {
				target = -1;
			} else if (gathered) {
				// Paul waits with them, then follows them in: the troops must
				// not fight long without him (his +30 motivation, and the
				// Harkonnen blow is shared among the troops at his place).
				if (_world.currentLocation() != (uint)stage)
					rideWormTo(stage);
				saveSlot(0); // before the troops go in: a failed attack comes back here
				for (uint k = 0; k < group.size(); ++k)
					_world.issueMoveOrder(group[k], (uint)target);
				for (uint p = 0; p < 16 && speedrunAlive(); ++p) {
					bool moving = false;
					for (uint k = 0; k < group.size(); ++k)
						moving |= (_world.troop(group[k]).occupation & 0x40) != 0;
					if (!moving)
						break;
					passTime(1);
				}
				uint hs, fs;
				const byte balance = _world.battleForces((uint)target, hs, fs);
				speedrunLog(Common::String::format("Paul rides to the battle at fort %d (Harkonnen %u, Fremen %u, balance %u)",
						target, hs, fs, balance));
				if (speedrunFight((uint)target)) {
					++won;
					speedrunLog(Common::String::format("step 33 OK: fort %d taken (%u so far)", target, won));
					speedrunEquip((uint)target);
				} else {
					speedrunLog(Common::String::format("fort %d still holds after the reloads", target));
					_ending = false;
					loadSlot(0); // back to before the troops went in
					_battle = false;
					passTime(2); // the rolls differ after the reload anyway
				}
				target = -1;
			}
		}
		if (_mode != kRoom && !_ending)
			showRoom(_world.room());
		for (uint p = 0; p < 4 && speedrunAlive(); ++p)
			passTime(1);
	}
	if (!speedrunAlive())
		return;
	uint left = 0;
	for (uint i = 0; i < _world.locationCount(); ++i)
		if (_world.location(i).isFortress() && !_world.friendlyPlace(i))
			++left;
	for (uint i = 0; left && i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (!l.isFortress() || _world.friendlyPlace(i))
			continue;
		uint best = 0xffff;
		int near = -1;
		for (uint j = 0; j < _world.locationCount(); ++j) {
			const Location o = _world.location(j);
			if (o.hidden() || !_world.friendlyPlace(j))
				continue;
			const uint d = _world.cellDistance(o.longitude, o.latitude, l.longitude, l.latitude);
			if (d < best) {
				best = d;
				near = (int)j;
			}
		}
		uint hs, fs;
		_world.battleForces(i, hs, fs);
		speedrunLog(Common::String::format("left: fort %u %s%s at %u/%d, Harkonnen %u, nearest friendly place %d at %u cells", i,
				_sentences ? _world.locationName(i, *_sentences).c_str() : "?",
				l.hidden() ? " (hidden)" : "", l.longitude, l.latitude, hs, near, best));
	}
	for (uint i = 0; left && i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (l.hidden() && l.latitude < -20)
			speedrunLog(Common::String::format("hidden place %u %s type %#x at %u/%d, findable from phase %#x%s", i,
					_sentences ? _world.locationName(i, *_sentences).c_str() : "?", l.type,
					l.longitude, l.latitude, l.discoverPhase, _world.discoverable(i) ? " (findable now)" : ""));
	}
	if (left) {
		speedrunLog(Common::String::format("BLOCKED %u fortress(es) left after the campaign", left));
		return;
	}
	speedrunLog(Common::String::format("step 46 OK: no more Harkonnen fortresses (%u won by the bot)", won));
	speedrunFinalAttack();
}

void GameScreen::speedrunTravel(uint place) {
	// The worm once it has been ridden (phase 0x50, ds:0a bit 6), the
	// ornithopter before.
	if (_state.b(World::kPaulEvents) & 0x40)
		rideWormTo((int)place);
	else
		travelTo(place);
	// The Emperor does not wait: a pending demand is served between trips.
	if (!_speedrunShipping && !_ending && (_state.b(World::kShipmentFlags) & 0x10))
		speedrunShipment();
}

void GameScreen::speedrunConverse() {
	// Play the talk (and any scene it starts) to its end; questions get ACCEPT.
	for (uint guard = 0; guard < 120; ++guard) {
		if (_ending)
			return;
		if (_talkBargain) {
			// ACCEPT Duncan's spice deals and Stilgar's launch of the final
			// attack; REFUSE the rest (the Water of Life: the route never drinks).
			const bool yes = _talkWho == 3 || _state.b(kFinalStage) == 5;
			answerQuestion(yes ? 1 : 2);
			continue;
		}
		if (_sceneActive) {
			if (_mode == kTalk)
				advanceConversation();
			else
				sceneStep();
			continue;
		}
		if (talking() || _talkRecruit) {
			advanceConversation();
			continue;
		}
		if (inConversation()) {
			endConversation(); // a scene an event asked for may follow
			continue;
		}
		if (_pendingScene && maybeStartScene())
			continue;
		break;
	}
	if (inConversation())
		endConversation();
}

void GameScreen::speedrunWait() {
	// The room waits: the idle messages and visions come (seg000:2b2a).
	for (uint k = 0; k < 4 && !_ending; ++k) {
		if (_mode != kRoom)
			break;
		checkIdle(_idleStart + 600000);
		speedrunConverse();
	}
}

void GameScreen::speedrunTalkHere(Common::Array<uint> &newTroops) {
	// Everybody in the room: characters talk; the Fremen of an unrallied troop
	// hear WORK FOR ME; a chief tells what he knows.
	speedrunConverse(); // an entry line
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	for (uint i = 0; i < people.size() && !_ending; ++i) {
		const uint who = people[i];
		if (who == World::kFremen && _speedrunRecruited && _state.b(GameState::kPhase) < 3) {
			continue; // one new troop a round early on: Leto's stillsuit line wants exactly two
		} else if (who == World::kFremen) {
			const uint troop = _world.localTroop(false);
			startConversation(World::kFremen);
			speedrunConverse();
			if (troop && !_world.troop(troop).hired()) {
				startConversation(World::kFremen);
				for (uint guard = 0; talking() && guard < 40; ++guard)
					advanceConversation();
				workForMe();
				speedrunConverse();
				if (_world.troop(troop).hired()) {
					_speedrunRecruited = true;
					newTroops.push_back(troop);
					speedrunLog(Common::String::format("troop %u (%u men) works for Paul at place %u", troop,
							_world.troop(troop).population, _world.currentLocation()));
				}
			}
		} else if (who >= World::kFremenChief) {
			startConversation(who);
			speedrunConverse();
		} else if (who != 3) {
			// Companions too: many lines depend on the room they are said in.
			// Duncan is only seen for the shipments: his closing line without
			// an agreement counts against the Emperor's patience (24a3).
			startConversation(who);
			speedrunConverse();
		}
	}
}

void GameScreen::speedrunVisitPlace(uint place, Common::Array<uint> &newTroops) {
	if (_world.currentLocation() != place || _desert)
		speedrunTravel(place);
	if (_ending || _world.currentLocation() != place)
		return;
	const uint rooms = _rooms.size();
	for (uint room = 1; room <= rooms && !_ending; ++room) {
		if (_world.placeType() == Location::kPalace && (palaceRoom(room).code & 0x80))
			continue;
		if (room == 1)
			showRoom(room);
		else
			enterRoom(room); // as walking in: the entry lines speak (ds:23 = 5)
		speedrunTalkHere(newTroops);
		if (_world.placeType() == Location::kPalace && room == 8 && _world.sightingCount()) {
			// The COMM room: the messages (seg000:290b).
			for (int i = (int)_world.sightingCount() - 1; i >= 0; --i) {
				byte variant = 0;
				const byte person = _world.viewSighting((uint)i, variant);
				_state.setB(0x24, variant);
				_state.setB(0xe9, person);
				presentLine(person, person, 4, 0, kTalkComm);
				speedrunConverse();
			}
		}
	}
	showRoom(1);
}

void GameScreen::speedrunCompanions(const uint *want, uint count) {
	// Keep the wanted companions (two at most); tell the others to stay.
	for (uint c = 0; c < 16; ++c) {
		if (!((_state.w(GameState::kPersonsWith) >> c) & 1))
			continue;
		bool keep = false;
		for (uint k = 0; k < count; ++k)
			keep |= want[k] == c;
		if (keep)
			continue;
		// The palace household (Jessica, Thufir, Duncan) is taken home first:
		// the chapters move their records inside the palace (seg000:116a).
		if ((c >= 1 && c <= 3) && (_world.currentLocation() != 0 || _desert)) {
			speedrunTravel(0);
			showRoom(1);
		}
		speedrunCompanion(c, false);
	}
	for (uint k = 0; k < count && !_ending; ++k) {
		if ((_state.w(GameState::kPersonsWith) >> want[k]) & 1)
			continue;
		const Character ch = _world.character(want[k]);
		if (ch.locationPlusOne == 0xff && ch.placeType != Location::kPalace)
			continue;
		const uint at = ch.locationPlusOne != 0xff ? ch.locationPlusOne - 1u : 0;
		if (at < _world.locationCount() && _world.location(at).hidden()) {
			// Following the story's hint ("meet somebody", "the village on
			// your map"): a flight from the nearest known place toward it,
			// with the spotting rule on the way (seg000:40f9).
			if (!_world.discoverable(at))
				continue;
			const Location target = _world.location(at);
			uint best = 0xffff;
			int from = -1;
			for (uint i = 0; i < _world.locationCount(); ++i) {
				const Location l = _world.location(i);
				if (l.hidden() || !_world.friendlyPlace(i))
					continue;
				const uint d = _world.cellDistance(l.longitude, l.latitude, target.longitude, target.latitude);
				if (d < best) {
					best = d;
					from = (int)i;
				}
			}
			const int spotted = from >= 0 ? speedrunSpot((uint)from, target.longitude, target.latitude) : -1;
			if (spotted < 0)
				continue;
			speedrunLog(Common::String::format("following the hint to character %u: from %d spots place %d", want[k], from, spotted));
			speedrunTravel((uint)from);
			speedrunTravel((uint)spotted);
		}
		if (speedrunMeet(want[k]))
			speedrunCompanion(want[k], true);
	}
}

int GameScreen::speedrunSpot(uint from, uint16 tLng, int16 tLat) {
	// A flight from a place toward a point: the first findable place within 4
	// cells of the path, spotted by a companion (seg000:40f9, ds:10).
	if (!_state.w(GameState::kPersonsWith))
		return -1;
	const Location fl = _world.location(from);
	const int dLng = (int16)(tLng - fl.longitude), dLat = tLat - fl.latitude;
	const uint len = MAX<uint>(1, _world.cellDistance(fl.longitude, fl.latitude, tLng, tLat));
	for (uint k = 0; k <= len; ++k) {
		const uint16 lng = (uint16)(fl.longitude + dLng * (int)k / (int)len);
		const int16 lat = (int16)(fl.latitude + dLat * (int)k / (int)len);
		for (uint i = 0; i < _world.locationCount(); ++i) {
			const Location c = _world.location(i);
			if (_world.discoverable(i) && _world.cellDistance(lng, lat, c.longitude, c.latitude) <= 4)
				return (int)i;
		}
	}
	return -1;
}

bool GameScreen::speedrunExplore() {
	// Flights out in the eight directions from each known friendly place,
	// 30 cells, until a companion spots a findable place (route items 5, 9, 18).
	static const int kDir[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
	for (uint from = 0; from < _world.locationCount(); ++from) {
		const Location l = _world.location(from);
		if (l.hidden() || !_world.friendlyPlace(from))
			continue;
		const int unit = (int)(65536 / MAX<uint>(1, _world.rowCells(l.latitude)));
		for (uint d = 0; d < 8; ++d) {
			const uint16 tLng = (uint16)(l.longitude + kDir[d][0] * 30 * unit);
			const int16 tLat = (int16)CLIP(l.latitude + kDir[d][1] * 30, -75, 75);
			const int spotted = speedrunSpot(from, tLng, tLat);
			if (spotted < 0)
				continue;
			speedrunLog(Common::String::format("flying from %u %s spots place %d", from,
					kDir[d][1] > 0 ? (kDir[d][0] ? "diagonally" : "south/north") : "east/west", spotted));
			if (_world.currentLocation() != from)
				speedrunTravel(from);
			speedrunTravel((uint)spotted);
			return true;
		}
	}
	return false;
}

void GameScreen::speedrunShipment() {
	// The Emperor's demand (route items 10, 13): Duncan's offer is accepted,
	// then Paul walks into the COMM room with him and the spice goes
	// (seg000:135ad).
	if (!(_state.b(World::kShipmentFlags) & 0x10) || _state.b(World::kShipmentPaused))
		return;
	// Wait for the stock to cover the demand while the Emperor still waits
	// (reminders on days 1-3, the end on the fourth, seg000:20a4).
	// The reminder table gives a good payer one day (class 2: the end on the
	// second day), so: today if the stock covers it, tomorrow at the latest.
	if (_state.w(World::kSpiceStock) < _state.w(World::kDemand) && _world.daysSinceDemand() < 1)
		return;
	if (_speedrunShipping)
		return;
	_speedrunShipping = true;
	// The Emperor's message on the COMM first (his demand is then seen).
	if (_world.currentLocation() != 0 || _desert)
		speedrunTravel(0);
	enterRoom(8);
	speedrunConverse();
	for (int i = (int)_world.sightingCount() - 1; i >= 0; --i) {
		if (_world.sighting((uint)i) & 0x80)
			continue; // already seen
		byte variant = 0;
		const byte person = _world.viewSighting((uint)i, variant);
		_state.setB(0x24, variant);
		_state.setB(0xe9, person);
		presentLine(person, person, 4, 0, kTalkComm);
		speedrunConverse();
	}
	if (!speedrunMeet(3)) {
		_speedrunShipping = false;
		return;
	}
	startConversation(3);
	speedrunConverse();
	const uint16 agreed = _state.w(World::kAgreed);
	if (!agreed) {
		speedrunLog("shipment: no agreement with Duncan");
		_speedrunShipping = false;
		return;
	}
	const uint16 before = _state.w(World::kSpiceStock);
	if (speedrunCompanion(3, true)) {
		if (_world.currentLocation() != 0)
			speedrunTravel(0);
		enterRoom(8);
		speedrunConverse();
		speedrunCompanion(3, false);
	}
	speedrunLog(Common::String::format("shipment: %u kg agreed, stock %u -> %u kg", agreed * 10, before * 10,
			_state.w(World::kSpiceStock) * 10));
	_speedrunShipping = false;
}

void GameScreen::speedrunStory() {
	// Route items 1-29, from a new game to the worm phase (0x4f): a greedy
	// explorer with the route's tactics (companions by chapter, the desert
	// vision alone, troops to spice first and then to the army).
	Common::Array<uint> troops;
	byte last = 0xff;
	uint idle = 0, lastTroops = 0;
	for (uint round = 0; round < 200 && speedrunAlive(); ++round) {
		const byte phase = _state.b(GameState::kPhase);
		if (phase >= 0x4f)
			break;
		_speedrunRecruited = false;
		if (phase != last) {
			speedrunLog(Common::String::format("story phase %#x", phase));
			last = phase;
			idle = 0;
		} else if (troops.size() != lastTroops) {
			idle = 0;
			lastTroops = troops.size();
		} else if (++idle > 0 && speedrunExplore()) {
			idle = 0;
		} else if (idle > 3) {
			for (uint c = 0; c < 16; ++c) {
				const Character ch = _world.character(c);
				const int at = ch.locationPlusOne != 0xff ? (int)ch.locationPlusOne - 1 : -1;
				speedrunLog(Common::String::format("character %u: room %u kind %#x place %d%s%s", c, ch.room, ch.placeType, at,
						at >= 0 && _world.location((uint)at).hidden() ? " (hidden" : "",
						at >= 0 && _world.location((uint)at).hidden() ? Common::String::format(", findable from %#x)",
								_world.location((uint)at).discoverPhase).c_str() : ""));
			}
			speedrunLog(Common::String::format("BLOCKED the story stays at phase %#x", phase));
			return;
		}
		speedrunShipment();
		// Companions by chapter (route items 4, 12, 17-18, 25).
		static const uint kGurneyJessica[2] = { 4, 1 }, kJessicaHarah[2] = { 1, 8 },
				kStilgarJessica[2] = { 5, 1 }, kStilgarChani[2] = { 5, 7 };
		if (phase == 0x14) {
			// The vision (item 12): everybody stays, Paul walks into the desert
			// alone and waits for the evening.
			speedrunCompanions(nullptr, 0);
			landInDesert();
			const uint slot = _world.timeSlot();
			passTime(slot < 12 ? 12 - slot : 1);
			_idleStart = _system->getMillis() - 5000;
			speedrunWait();
			speedrunConverse();
			continue;
		} else if (phase < 0x10) {
			speedrunCompanions(kGurneyJessica, 2);
		} else if (phase < 0x14) {
			static const uint kHarahGurney[2] = { 8, 4 }; // Harah joins (route item 11)
			speedrunCompanions(kHarahGurney, 2);
		} else if (phase < 0x20) {
			speedrunCompanions(kJessicaHarah, 2); // the hidden door of room 7 (item 7)
		} else if (phase < 0x26) {
			static const uint kThufirHarah[2] = { 2, 8 }; // the trapped door, the armory (item 16)
			speedrunCompanions(kThufirHarah, 2);
		} else if (phase < 0x2c) {
			static const uint kHarahStilgar[2] = { 8, 5 }; // Harah leads to Stilgar (items 17-18)
			speedrunCompanions(kHarahStilgar, 2);
		} else if (phase < 0x30) {
			// Gurney stays with a training troop to teach it arms (his
			// topic-6 line, the next chapter; route items 19-21, 35).
			static const uint kStilgarGurney[2] = { 5, 4 };
			speedrunCompanions(kStilgarGurney, 2);
			if ((_state.w(GameState::kPersonsWith) >> 4) & 1) {
				for (uint k = 0; k < troops.size(); ++k) {
					const Troop t = _world.troop(troops[k]);
					const int at = _world.troopPlace(troops[k]);
					if (t.hired() && !(t.occupation & 0x60) && (t.occupation & 0x0f) == kJobTraining && at > 0) {
						speedrunTravel((uint)at);
						showRoom(1);
						speedrunCompanion(4, false);
						break;
					}
				}
			}
		} else if (phase >= 0x3c && phase < 0x40) {
			// Harah goes home to Tuono-Timin (route item 22): she comes along,
			// and there she stays (her topic-6 line, the next chapter).
			static const uint kHarahStilgar[2] = { 8, 5 };
			speedrunCompanions(kHarahStilgar, 2);
			for (uint i = 0; i < _world.locationCount(); ++i) {
				const Location l = _world.location(i);
				if (l.firstName != 3 || l.lastName != 4)
					continue;
				if ((_state.w(GameState::kPersonsWith) >> 8) & 1) {
					speedrunTravel(i);
					showRoom(1);
					speedrunCompanion(8, false);
				}
				break;
			}
		} else if (phase < 0x40) {
			speedrunCompanions(kStilgarJessica, 2);
		} else {
			speedrunCompanions(kStilgarChani, 2);
			if (phase < 0x48 && ((_state.w(GameState::kPersonsWith) >> 7) & 1)) {
				// Chani in the desert under the evening sky (route item 25):
				// her line there opens the next chapter.
				landInDesert();
				const uint slot = _world.timeSlot();
				passTime(slot < 12 ? 12 - slot : 1);
				updateRoomVars();
				startConversation(7);
				speedrunConverse();
				continue;
			}
		}
		// The palace, then every known sietch.
		speedrunVisitPlace(0, troops);
		// Where the characters wait first (their lines move the story), then
		// the other known places, from a point that moves on each round.
		Common::Array<uint> order;
		for (uint c = 0; c < 9; ++c) {
			const Character ch = _world.character(c);
			if (ch.locationPlusOne == 0xff || ch.locationPlusOne == 0 || ((_state.w(GameState::kPersonsWith) >> c) & 1))
				continue;
			const uint at = ch.locationPlusOne - 1u;
			bool listed = false;
			for (uint k = 0; k < order.size(); ++k)
				listed |= order[k] == at;
			if (!listed && at && at < _world.locationCount() && !_world.location(at).hidden())
				order.push_back(at);
		}
		const uint n = _world.locationCount();
		for (uint k = 0; k < n; ++k)
			order.push_back((k + round * 7) % n);
		for (uint j = 0; j < order.size() && speedrunAlive(); ++j) {
			const uint i = order[j];
			const Location l = _world.location(i);
			if (l.hidden() || i == 0 || !_world.friendlyPlace(i) || l.type == Location::kPalace)
				continue;
			// Only where something can happen: someone to meet, Fremen to
			// hire, the smugglers, or any place in the early chapters.
			bool worth = phase < 0x14 || l.type == Location::kVillageMin;
			for (uint c = 0; c < 9 && !worth; ++c)
				worth = _world.character(c).locationPlusOne == i + 1;
			Common::Array<uint> here;
			_world.troopsAt(i, here);
			for (uint k = 0; k < here.size() && !worth; ++k)
				worth = !_world.troop(here[k]).harkonnen() && !_world.troop(here[k]).hired();
			if (!worth)
				continue;
			speedrunVisitPlace(i, troops);
			if (_state.b(GameState::kPhase) != phase || (_speedrunRecruited && phase < 3))
				break; // back to the palace with the news
		}
		// The troops' chiefs through the map's contact (their topic-2 lines:
		// "a great Fremen, his name is Stilgar", item 18), ASK FOR MORE
		// INFORMATION until they have nothing more to say.
		if (_state.b(GameState::kPhase) == phase && !_ending) {
			for (uint k = 0; k < troops.size() && _state.b(GameState::kPhase) == phase; ++k) {
				const Troop t = _world.troop(troops[k]);
				if (!t.hired() || (t.occupation & 0x40))
					continue;
				openMap(MapScreen::kFlat, false);
				openTroop(troops[k], true);
				for (uint g = 0; g < 8 && nextTroopLine(); ++g)
					;
				leaveMap();
			}
		}
		// Jobs (items 11, 19-21): spice for the Emperor's demands until the
		// war chapters; then the new troops train, three miners stay on spice.
		for (uint k = 0; k < troops.size(); ++k) {
			const Troop t = _world.troop(troops[k]);
			if ((t.occupation & 0x0f) == Troop::kWaitingForOrders)
				_world.setTroopOccupation(troops[k], (phase < 0x40 || k < 3) ? Troop::kSpiceMining : kJobTraining);
			// A spice troop prospects its place first (seg000:70cc), then mines
			// it (6fe5: only a prospected place that is not exhausted yields).
			const byte job = _world.troop(troops[k]).occupation & 0x0f;
			const int at = _world.troopPlace(troops[k]);
			if (at < 0 || (job != Troop::kSpiceMining && job != Troop::kProspecting) || (t.occupation & 0x60))
				continue;
			// A harvester multiplies the harvest by four (seg000:708a): take a
			// free one here (MODIFY EQUIPMENT), or go where one lies unused.
			if (!(t.equipment & 0x80) && !_world.takeEquipment(troops[k], 0) && !(t.occupation & 0x40)) {
				for (uint i = 0; i < _world.locationCount(); ++i) {
					byte c[7];
					_world.placeFreeEquipment(i, c);
					if (c[0] && !_world.location(i).hidden() && _world.friendlyPlace(i) && (int)i != at &&
							(_world.location(i).status & 0x40) && _world.location(i).spiceDensity) {
						if (_world.issueMoveOrder(troops[k], i))
							speedrunLog(Common::String::format("troop %u goes to fetch the harvester at place %u", troops[k], i));
						break;
					}
				}
				continue;
			}
			const Location l = _world.location((uint)at);
			const bool prospected = (l.status & 0x40) != 0, exhausted = (l.status & 0x01) != 0;
			if (!prospected && job != Troop::kProspecting)
				_world.setTroopOccupation(troops[k], Troop::kProspecting);
			else if (prospected && !exhausted && l.spiceDensity && job != Troop::kSpiceMining)
				_world.setTroopOccupation(troops[k], Troop::kSpiceMining);
		}
		if (_mode == kRoom)
			speedrunWait();
	}
	if (_state.b(GameState::kPhase) >= 0x4f)
		speedrunLog("step 29 OK: the worm phase (0x4f), CALL A WORM open");
}



bool GameScreen::speedrunMeet(uint who) {
	// Go where the character is (its record: room, place kind, location + 1).
	if ((_state.w(GameState::kPersonsWith) >> who) & 1)
		return true;
	const Character c = _world.character(who);
	int place = c.locationPlusOne != 0xff ? (int)c.locationPlusOne - 1 : (c.placeType == Location::kPalace ? 0 : -1);
	if (place < 0 || place >= (int)_world.locationCount()) {
		speedrunLog(Common::String::format("character %u is nowhere to be found (room %u, kind %#x, place %u)", who, c.room,
				c.placeType, c.locationPlusOne));
		return false;
	}
	if (_world.currentLocation() != (uint)place || _desert)
		speedrunTravel((uint)place);
	if (_ending)
		return false;
	showRoom(c.room ? c.room : 1);
	speedrunConverse(); // an entry line
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	for (uint i = 0; i < people.size(); ++i)
		if (people[i] == who)
			return true;
	const byte *r = _state.vars + World::kCharacterTable + who * World::kCharacterSize;
	speedrunLog(Common::String::format("character %u is not in room %u of place %d (record %02x %02x %02x %02x %02x, ds:4-7 %02x %02x %02x %02x)",
			who, c.room, place, r[0], r[1], r[2], r[3], r[4], _state.b(4), _state.b(5), _state.b(6), _state.b(7)));
	return false;
}

bool GameScreen::speedrunCompanion(uint who, bool come) {
	// COME WITH ME / STAY HERE, as the talk's row does.
	const bool with = (_state.w(GameState::kPersonsWith) >> who) & 1;
	if (with == come)
		return true;
	startConversation(who);
	for (uint guard = 0; talking() && guard < 40; ++guard)
		advanceConversation(); // the lines first: their events fire as they end
	companionVerb();
	speedrunConverse();
	const bool now = (_state.w(GameState::kPersonsWith) >> who) & 1;
	speedrunLog(Common::String::format("%s to character %u: %s", come ? "COME WITH ME" : "STAY HERE", who,
			now == come ? "done" : "refused"));
	return now == come;
}

void GameScreen::speedrunFinalAttack() {
	// Route items 46-54, as the dialogue data asks (notes/speedrun):
	// stage 1 (last fort) -> Thufir COME WITH ME (his topic-5 line, action
	// 0x0e) -> 2 -> Thufir STAY HERE at a sietch by the palace (topic 6,
	// 0x0e) -> 3 -> Jessica, Thufir, Gurney, Stilgar and Chani in that room:
	// Gurney's entry line starts the council scene (cs:12db), Thufir's line
	// in it (0x0e) -> 4 -> 10 000 men with atomics round the palace and any
	// line of Thufir -> 5 -> Stilgar's question, ACCEPT (action 9) -> 6 and
	// the march -> the palace falls -> 7.
	auto stage = [&]() { return _state.b(kFinalStage); };
	auto force = [&](byte to, const char *why) {
		if (stage() < to) {
			speedrunLog(Common::String::format("FORCED final attack stage %u -> %u (%s)", stage(), to, why));
			_state.setB(kFinalStage, to);
		}
	};
	const uint kThufir = 2, kJessica = 1, kGurney = 4, kStilgar = 5, kChani = 7;
	// A sietch by the palace (locations 2-4, taken and converted).
	int council = -1;
	for (uint index = 2; index <= 4 && council < 0; ++index)
		if (_world.friendlyPlace(index))
			council = (int)index;
	if (council < 0) {
		speedrunLog("BLOCKED none of the places by the palace is ours");
		return;
	}
	// Stilgar talks of Thufir (item 46).
	if (speedrunMeet(kStilgar))
		talkThrough(kStilgar);
	// Stage 2: Thufir comes along (item 47).
	if (stage() == 1 && speedrunMeet(kThufir))
		speedrunCompanion(kThufir, true);
	if (stage() == 2)
		speedrunLog("step 47 OK: Thufir joins, stage 2");
	else
		force(2, "Thufir did not come");
	// Stage 3: Thufir stays at the sietch by the palace.
	rideWormTo(council);
	if (_ending)
		return;
	showRoom(1);
	if (!((_state.w(GameState::kPersonsWith) >> kThufir) & 1)) {
		_state.setW(GameState::kPersonsWith, (uint16)(_state.w(GameState::kPersonsWith) | (1 << kThufir)));
		_world.addCompanion(kThufir);
	}
	speedrunCompanion(kThufir, false);
	if (stage() == 3)
		speedrunLog(Common::String::format("step 49 OK: Thufir stays at place %d, stage 3", council));
	else
		force(3, "Thufir's STAY HERE did not raise it");
	// Jessica and Gurney wait there too; Stilgar and Chani come with Paul.
	const uint parked[2] = { kJessica, kGurney };
	for (uint k = 0; k < 2 && !_ending; ++k) {
		if (!speedrunMeet(parked[k]) || !speedrunCompanion(parked[k], true))
			continue;
		rideWormTo(council);
		showRoom(1);
		speedrunCompanion(parked[k], false);
	}
	if (!((_state.w(GameState::kPersonsWith) >> kStilgar) & 1) && speedrunMeet(kStilgar))
		speedrunCompanion(kStilgar, true);
	if (speedrunMeet(kChani))
		speedrunCompanion(kChani, true);
	rideWormTo(council); // the arrival's entry scan: "Hey! here we are."
	speedrunConverse();
	if (stage() == 4)
		speedrunLog("step 51 OK: the war council, stage 4");
	else
		force(4, Common::String::format("the council did not start, persons in room %#x", _state.w(GameState::kPersonsInRoom)).c_str());
	// The 10 000 men with atomics (item 52).
	for (uint id = 1; id <= World::kTroops; ++id) {
		const Troop t = _world.troop(id);
		const int here = _world.troopPlace(id);
		if (!t.id || t.harkonnen() || !t.hired() || (t.occupation & 0x60) || !(t.equipment & 0x04) || here < 0)
			continue;
		if (here < 2 || here > 4)
			_world.issueMoveOrder(id, 2 + id % 3);
	}
	for (uint p = 0; p < 96 && !_world.finalAttackReady() && speedrunAlive(); ++p) {
		passTime(1);
		for (uint index = 2; index <= 4; ++index) {
			speedrunEquip(index);
			Common::Array<uint> ids;
			_world.troopsAt(index, ids);
			for (uint i = 0; i < ids.size(); ++i) {
				const Troop t = _world.troop(ids[i]);
				if (t.hired() && !t.harkonnen() && !(t.occupation & 0x60) && (t.occupation & 0x0f) != kJobTraining)
					_world.setTroopOccupation(ids[i], kJobTraining);
			}
		}
	}
	if (!_world.finalAttackReady()) {
		uint total = 0;
		for (uint id = 1; id <= World::kTroops; ++id) {
			const Troop t = _world.troop(id);
			if (t.id && (t.equipment & 0x04))
				speedrunLog(Common::String::format("atomics: troop %u %s at %d occ %#x pop %u", id,
						t.harkonnen() ? "Harkonnen" : "Fremen", _world.troopPlace(id), t.occupation, t.population));
		}
		for (uint i = 0; i < _world.locationCount(); ++i) {
			byte c[7];
			_world.placeFreeEquipment(i, c);
			if (c[5]) {
				total += c[5];
				speedrunLog(Common::String::format("atomics: %u free at place %u", c[5], i));
			}
		}
		speedrunLog(Common::String::format("BLOCKED fewer than 10 000 men with atomics round the palace (%u atomics lying free)", total));
		return;
	}
	if (speedrunMeet(kThufir))
		talkThrough(kThufir);
	if (stage() == 5)
		speedrunLog("step 52 OK: Thufir: enough men round the palace, stage 5");
	else
		force(5, "Thufir's line did not raise it");
	// Stilgar: "Do you want to launch the attack now?" ACCEPT (item 53).
	if (speedrunMeet(kStilgar)) {
		startConversation(kStilgar);
		speedrunConverse();
	}
	if (stage() == 6) {
		speedrunLog("step 53 OK: Stilgar launches the attack, stage 6");
	} else {
		force(6, "Stilgar's question did not come");
		Common::Array<uint> marching;
		_world.finalAttackTroops(marching);
		_state.setB(kFinalStage, 6);
	}
	for (uint p = 0; p < 96 && stage() < 7 && speedrunAlive(); ++p)
		passTime(1);
	if (stage() < 7) {
		speedrunLog("BLOCKED the palace did not fall");
		return;
	}
	speedrunLog("step 54 OK: the palace falls, the shield is down, stage 7");
	rideWormTo(1);
	if (!speedrunAlive())
		return;
	showRoom(2);
	for (uint guard = 0; _sceneActive && guard < 200; ++guard) {
		if (_mode == kTalk)
			advanceConversation();
		else
			sceneStep();
	}
	if (_state.b(GameState::kPhase) >= 0xc8)
		speedrunLog("step 56 OK: the Emperor's throne room, the end");
	else
		speedrunLog("BLOCKED the ending did not start in the Baron's hall");
}

void GameScreen::speedrun(const Common::String &part) {
	if (!loadDialogue())
		return;
	speedrunLog(Common::String::format("start (%s, %s)", part.c_str(), _world.floppy() ? "floppy" : "CD"));
	const uint16 shot = _panel.findCommand("You know what?", true);
	for (uint k = 0; shot != 0xffff && k < 4; ++k)
		speedrunLog(Common::String::format("ending text %u: \"%s\"", shot + k, _panel.commandString(shot + k).c_str()));
	if (part == "campaign")
		speedrunCampaignSetup();
	else
		speedrunStory();
	if (speedrunAlive())
		speedrunCampaign();
	speedrunLog("end");
}

} // namespace Dune

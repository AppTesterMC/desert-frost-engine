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
// could not reach by play and had to set by hand is logged as "FORCED".
// This remains an assisted engine-logic test: campaign recruitment, movement,
// equipment, contact opening and battle retries do not all use player input.
// A completed run is not proof that the game is completable through its UI.

#include "dune/scene.h"

#include "common/algorithm.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/hashmap.h"
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
	// Watched runs show each milestone on screen too.
	if (ConfMan.hasKey("dune_speedrun_watch") &&
			(what.contains("OK") || what.contains("BLOCKED") || what.contains("FORCED") || what.contains("taken") ||
			 what.contains("story phase") || what.contains("recruited") || what.contains("works for")))
		showStatus(Common::String::format("Day %u: %s", _world.day(), what.c_str()).c_str());
	_log.line(Common::String::format("Speedrun: day %u %02u:%02u, phase %#x, charisma %u, rallied %u, stage %u: %s",
			_world.day(), _world.timeSlot() * 3 / 2, (_world.timeSlot() & 1) ? 30 : 0,
			_state.b(GameState::kPhase), _state.b(World::kCharisma), _state.b(GameState::kFremenTroops),
			_state.b(kFinalStage), what.c_str()));
}

void GameScreen::speedrunPause(uint millis) {
	// Watched runs (dune_speedrun_watch): the screen stays up for a moment
	// and the window keeps answering; closing it stops the run.
	if (!ConfMan.hasKey("dune_speedrun_watch") || _quitRequested)
		return;
	const uint scale = MAX(1, ConfMan.getInt("dune_speedrun_watch"));
	if (isRecording()) {
		// Headless recording: the screen held for the pause, no waiting.
		_system->updateScreen();
		recordFrame(_system, millis * scale / 100);
		return;
	}
	const uint32 until = _system->getMillis() + millis * scale / 100;
	while (_system->getMillis() < until && !_quitRequested) {
		Common::Event event;
		while (_system->getEventManager()->pollEvent(event))
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				_quitRequested = true;
		_system->updateScreen();
		_system->delayMillis(10);
	}
}

bool GameScreen::speedrunAlive() {
	if (_quitRequested)
		return false;
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
	// The seeded late-game fixture still needs the prospectors' initial
	// lesson at its normal chapter. Its first specialization dispatches
	// event 3 by phase; doing that at 0x50 wrongly starts the war council
	// and consumes its lines. Use the ordinary occupation/lesson controls.
	_state.setB(GameState::kPhase, 5);
	_world.setTroopOccupation(World::kProspectorTroop, Troop::kWaitingForOrders);
	speedrunOrders(World::kProspectorTroop, Troop::kSpiceMining, false, -2);
	// Stilgar travels with Paul from day 3 (route item 18).
	_world.setTravelling(5, true);
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
	speedrunLog(Common::String::format("FORCED campaign start: %u troops rallied, troop 3 prospecting and others training, Stilgar with Paul, chapters 4-0x4c applied, phase 0x4f", rallied));
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
			speedrunPause(900);
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
					// The Emperor keeps demanding through the war (the shipments
					// pause only when the Harkonnen palace alone is left), so every
					// spice troop from before Stilgar stays on spice; the new
					// troops train (the user's route, 2026-09-28).
					if (miners < World::kTroops) {
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
		// A raid can catch Paul while he waits at the gathering site. Fight
		// that battle through the same attack/save-reload route before waiting.
		if (_battle && !speedrunFight(_world.currentLocation())) {
			speedrunLog("BLOCKED the raid at Paul's gathering site could not be won");
			return;
		}
		if (!_battle) {
			speedrunShipment(); // the Emperor's demands until the last fort falls
			speedrunSpice();    // the miners keep up with them
			speedrunKeepInTouch();
			speedrunGarrison(&group); // the soldiers not in the attack keep out of the raids' reach
			// The Emperor's demands grow (16-18 t by the second month): when
			// the stock falls under 25 t with fewer than twelve miners, a free
			// soldier becomes a miner, as a player would (the campaign's own
			// army still takes the forts).
			if (_world.spiceStock() < 25000) {
				uint miners = 0, spare = 0;
				for (uint id = 1; id <= World::kTroops; ++id) {
					const Troop t = _world.troop(id);
					if (!t.id || t.harkonnen() || !t.hired() || (t.occupation & 0x60))
						continue;
					if ((t.occupation & 0x0f) == Troop::kSpiceMining)
						++miners;
					else if ((t.occupation & 0x0f) == kJobTraining && !spare) {
						bool inGroup = false;
						for (uint k = 0; k < group.size(); ++k)
							inGroup = inGroup || group[k] == id;
						if (!inGroup)
							spare = id;
					}
				}
				if (miners < 12 && spare) {
					speedrunLog(Common::String::format("spice stock %u kg: troop %u turns to spice mining", _world.spiceStock(), spare));
					speedrunOrders(spare, Troop::kSpiceMining, true, -2);
				}
			}
			// The prospectors, caught in a raid, keep occupation 6 after the
			// battle is won (75e3 clears only the captured bit), and their
			// place stays "won" every period (739e -> 7429), which puts the
			// final attack back to stage 1 (7493): a player gives them their
			// job back.
			const Troop p = _world.troop(World::kProspectorTroop);
			const int pAt = _world.troopPlace(World::kProspectorTroop);
			if (p.hired() && (p.occupation & 0x2f) == 6 && pAt >= 0 && !(_world.location((uint)pAt).status & 0x02)) {
				speedrunLog("the prospectors go back to prospecting after a battle");
				speedrunOrders(World::kProspectorTroop, Troop::kSpiceMining, false, -2);
			}
		}
		// A Harkonnen raid on one of our sietches (CD 1f64): the nearest free
		// soldiers march there to fight (83fd sets them fighting on arrival).
		for (uint i = 0; i < _world.locationCount() && speedrunAlive(); ++i) {
			const Location l = _world.location(i);
			if (!l.isSietch() || !(l.status & 0x02) || (int)i == target)
				continue;
			uint hs, fs;
			_world.battleForces(i, hs, fs);
			if (!hs || fs >= hs + hs / 4)
				continue;
			uint sum = fs;
			for (uint pass = 0; pass < World::kTroops && sum < hs + hs / 4; ++pass) {
				uint best = 0, bestDistance = 0xffff;
				for (uint id = 1; id <= World::kTroops; ++id) {
					const Troop t = _world.troop(id);
					if (!t.id || t.harkonnen() || !t.hired() || (t.occupation & 0x70) || (t.occupation & 0x0f) != kJobTraining)
						continue;
					bool busy = false;
					for (uint k = 0; k < group.size(); ++k)
						busy = busy || group[k] == id;
					const int at = _world.troopPlace(id);
					if (busy || at < 0 || at == (int)i)
						continue;
					const Location a = _world.location((uint)at);
					const uint d = _world.cellDistance(a.longitude, a.latitude, l.longitude, l.latitude);
					if (d < bestDistance) {
						bestDistance = d;
						best = id;
					}
				}
				if (!best || !_world.issueMoveOrder(best, i))
					break;
				sum += _world.troopStrength(best);
				speedrunLog(Common::String::format("troop %u marches to defend sietch %u (Harkonnen %u, Fremen %u)", best, i, hs, fs));
			}
		}
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
		// With no target and no known fort left, the last attack's group is
		// free again: no new target would clear it, and its troops (the
		// whole army) would stay "busy" for good, with no spy sent (the CD
		// full run stalled so after its eighth fort, 2026-09-29).
		if (target < 0 && forts.empty())
			group.clear();
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
		if (round % 16 == 0)
			speedrunLog(Common::String::format("war: %u known fort(s), %u hidden, %u army troop(s) free, %u spy, best spy %u, target %d",
					forts.size(), hidden.size(), army.size(), spies, bestSpy, target));
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
				// A fort its raiders left empty (CD 1f64) still needs one troop to walk in.
				for (uint k = 0; k < army.size() && (group.empty() || sum * 10 < bestH * 9); ++k) {
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
					// Not a sietch another fortress could raid while the
					// group gathers (World::raidSource), when there is a choice.
					for (uint pass = 0; pass < 2 && best == 0xffff; ++pass)
						for (uint i = 0; i < _world.locationCount(); ++i) {
							const Location l = _world.location(i);
							if (l.hidden() || !_world.friendlyPlace(i) || _world.placeInBattle(i))
								continue;
							if (pass == 0 && l.isSietch()) {
								if (_world.raidSource(i) >= 0)
									continue; // the target itself raids too

							}
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
		for (uint p = 0; p < 4 && speedrunAlive() && !_battle; ++p)
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

void GameScreen::speedrunKeepInTouch() {
	// The daily decay (CD 6e4e): a troop not contacted for more than 8 days
	// loses a motivation point a day and, below 5, sulks. A player keeps in
	// touch: every troop not contacted for a week gets a map contact
	// (closing it writes the day into byte 0x14, 7b89).
	const byte day = (byte)(_state.w(GameState::kGameTime) >> 4);
	for (uint id = 1; id <= World::kTroops && !_ending; ++id) {
		const Troop t = _world.troop(id);
		if (!t.id || t.harkonnen() || !t.hired() || (t.occupation & 0x20))
			continue;
		if ((byte)(day - _world.troopByteForTest(id, 0x14)) < 7)
			continue;
		if (speedrunOpenOrders(id))
			speedrunCloseOrders();
	}
}

void GameScreen::speedrunGarrison(const Common::Array<uint> *busy) {
	// The Harkonnen raids (World::harkonnenRaid, CD 1f64) come from a
	// fortress within 30 cells of a sietch with Fremen troops (2047-2070).
	// Early on no troop of ours can stand against a fortress, so a player
	// keeps the soldiers out of reach: from phase 0x2c on, a soldier at an
	// exposed sietch marches to the nearest safe one (MOVE TROOP); the
	// miners prefer safe fields (speedrunSpiceFields).
	if (_state.b(GameState::kPhase) < 0x2c)
		return;
	for (uint id = 1; id <= World::kTroops; ++id) {
		const Troop t = _world.troop(id);
		const byte job = t.occupation & 0x0f;
		if (!t.id || t.harkonnen() || !t.hired() || id == World::kProspectorTroop || (t.occupation & 0x60) ||
				job != kJobTraining)
			continue;
		bool inGroup = false;
		for (uint k = 0; busy && k < busy->size(); ++k)
			inGroup = inGroup || (*busy)[k] == id;
		const int at = _world.troopPlace(id);
		if (inGroup || at < 0 || !_world.location((uint)at).isSietch())
			continue;
		const int source = _world.raidSource((uint)at);
		if (source < 0)
			continue;
		// In the campaign (busy set) the troops wait by hidden forts on
		// purpose, to find them: only a known fortress sends them away.
		if (busy && _world.location((uint)source).hidden())
			continue;
		// Only when the fortress could win: the sietch's Fremen against its garrison.
		uint hs, fs, ht, ft;
		_world.battleForces((uint)source, hs, fs);
		_world.battleForces((uint)at, ht, ft);
		if (ft >= hs + hs / 5)
			continue;
		const Location here = _world.location((uint)at);
		int best = -1;
		uint bestDistance = 0xffff;
		for (uint i = 0; i < _world.locationCount(); ++i) {
			const Location l = _world.location(i);
			if ((int)i == at || !l.isSietch() || l.hidden() || !_world.friendlyPlace(i) || (l.status & 0x02) ||
					_world.wouldQuarrel(id, i) || _world.raidSource(i) >= 0)
				continue;
			const uint d = _world.cellDistance(here.longitude, here.latitude, l.longitude, l.latitude);
			if (d < bestDistance) {
				bestDistance = d;
				best = (int)i;
			}
		}
		if (best < 0)
			continue;
		speedrunLog(Common::String::format("troop %u leaves sietch %d, in reach of a Harkonnen fortress (place %d), for sietch %d",
				id, at, _world.raidSource((uint)at), best));
		speedrunOrders(id, -1, false, best);
	}
}

void GameScreen::speedrunTravel(uint place) {
	// A player does not land where Harkonnens hold the place and no Fremen
	// fight for it: a sietch lost to a raid (CD 1f64) is a fortress now.
	if (place < _world.locationCount()) {
		uint harkonnen = 0, attacking = 0;
		_world.countHostiles(place, harkonnen, attacking);
		const Location l = _world.location(place);
		if (!_world.friendlyPlace(place) && harkonnen && !attacking && !(l.status & 2)) {
			speedrunLog(Common::String::format("does not land at place %u: Harkonnens hold it", place));
			return;
		}
		if (l.type < Location::kFortressMin && (l.status & 2)) {
			// Nor at a sietch the Harkonnens are raiding: it may fall
			// during the flight.
			speedrunLog(Common::String::format("does not land at place %u: the Harkonnens are raiding it", place));
			return;
		}
	}
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
		if (_talkBargain && _talkWho == World::kSmuggler) {
			// The route buys nothing from the smugglers: a REFUSE only brings
			// the next offer (their talk starts again, loc_194b9), so the
			// bot stops talking at the offer.
			break;
		}
		if (_talkBargain) {
			// ACCEPT Duncan's spice deals and Stilgar's launch of the final
			// attack; REFUSE the rest (the Water of Life: the route never drinks).
			const bool yes = _talkWho == 3 || _state.b(kFinalStage) == 5;
			answerQuestion(yes ? 1 : 2);
			continue;
		}
		if (_sceneActive) {
			speedrunPause(1400);
			if (_mode == kTalk)
				advanceConversation();
			else
				sceneStep();
			continue;
		}
		if (talking() || _talkRecruit) {
			speedrunPause(1400); // read the page
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
					// Until Stilgar (phase 0x2c) every new troop goes to spice at
					// once (SPECIALIZE IN SPICE) with a harvester if one lies free
					// here; otherwise the jobs loop sends it to fetch one. After
					// Stilgar the new troops train for the army.
					// The orders go at once, through the chief standing here:
					// GIVE ORDERS TO TROOP, the occupation, MODIFY EQUIPMENT.
					if (_state.b(GameState::kPhase) < 0x2c)
						speedrunOrders(troop, Troop::kSpiceMining, true, -2);
					else
						speedrunOrders(troop, kJobTraining, false, -2); // Stilgar is with Paul: the army
				}
			}
		} else if (who >= World::kFremenChief) {
			// The chief once a chapter: what he knows (the orders go through him too).
			if (!speedrunOnce(Common::String::format("chief %u %u %#x", who, _world.currentLocation(), _state.b(GameState::kPhase))))
				continue;
			startConversation(who);
			speedrunConverse();
		} else if (who != 3) {
			// A companion talks once a chapter, and in the palace rooms whose
			// lines the story reads (the hidden doors of rooms 7 and 11, the
			// walkway, the COMM room); somebody met here, once a chapter and
			// party. Duncan is only seen for the shipments: his closing line
			// without an agreement counts against the Emperor's patience (24a3).
			const uint16 with = _state.w(GameState::kPersonsWith);
			const bool companion = (with >> who) & 1;
			const uint room = _world.room();
			const bool special = _world.placeType() == Location::kPalace && room != 1 && room != 10;
			const Common::String key = companion
					? Common::String::format("with %u %#x %u", who, _state.b(GameState::kPhase), special ? room : 0)
					: Common::String::format("meet %u %#x %u %u %#x %u", who, _state.b(GameState::kPhase), _world.currentLocation(), room, with,
							_state.b(World::kUnread));
			if (!speedrunOnce(key))
				continue;
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
	const bool palace = _world.placeType() == Location::kPalace;
	const uint16 with = _state.w(GameState::kPersonsWith);
	for (uint room = 1; room <= rooms && !_ending; ++room) {
		if (palace && (palaceRoom(room).code & 0x80))
			continue;
		// Only rooms with a reason, once a chapter and party: in the palace
		// where somebody lives, or, with companions, the rooms whose lines the
		// story reads; a sietch's rooms once until something changes there.
		if (palace && room != 1) {
			bool someone = false;
			for (uint c = 0; c < 12 && !someone; ++c) {
				const Character ch = _world.character(c);
				const bool home = ch.locationPlusOne == 1 || (ch.locationPlusOne == 0xff && ch.placeType == Location::kPalace);
				someone = home && ch.room == room && !((with >> c) & 1);
			}
			// With companions every room once: their entry lines find the
			// hidden doors (Jessica in the equipment room, ch03; the corridor
			// and room 11, ch13).
			const bool reason = someone || with || (room == 8 && _world.sightingCount());
			if (!reason)
				continue;
		}
		if (!speedrunOnce(Common::String::format("room %u %u %#x %#x %u", place, room, _state.b(GameState::kPhase), with,
				_state.b(World::kCharisma) / 4)))
			continue;
		++_speedrunRoundVisits;
		if (room == 1)
			showRoom(room);
		else
			enterRoom(room); // as walking in: the entry lines speak (ds:23 = 5)
		speedrunPause(700);
		if (_world.placeType() == Location::kPalace && room == 8 && _world.sightingCount()) {
			// The COMM room: the messages first (seg000:290b); Thufir's first
			// answer there is "View the message before anything else."
			for (int i = (int)_world.sightingCount() - 1; i >= 0; --i) {
				byte variant = 0;
				const byte person = _world.viewSighting((uint)i, variant);
				_state.setB(0x24, variant);
				_state.setB(0xe9, person);
				presentLine(person, person, 4, 0, kTalkComm);
				speedrunConverse();
			}
		}
		speedrunTalkHere(newTroops);
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
		_speedrunRoundVisits = 0;
		if (phase != last) {
			speedrunLog(Common::String::format("story phase %#x", phase));
			last = phase;
			idle = 0;
		} else if (troops.size() != lastTroops) {
			idle = 0;
			lastTroops = troops.size();
		} else if (++idle > 0 && speedrunExplore()) {
			idle = 0;
		} else if (idle > 6) { // more rounds: a round now visits only what changed
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
			// A sietch again only when something changed: the chapter, the
			// party, Paul's charisma (who will follow him), the troops there.
			uint hiredHere = 0;
			for (uint k = 0; k < here.size(); ++k)
				hiredHere += _world.troop(here[k]).hired() ? 1 : 0;
			if (!speedrunOnce(Common::String::format("place %u %#x %#x %u %u %u", i, phase, _state.w(GameState::kPersonsWith),
					_state.b(World::kCharisma) / 4, hiredHere, (uint)here.size())))
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
				if (!speedrunOnce(Common::String::format("contact %u %#x", troops[k], phase)))
					continue;
				openMap(MapScreen::kFlat, false);
				openTroop(troops[k], true);
				for (uint g = 0; g < 8 && nextTroopLine(); ++g)
					;
				leaveMap();
			}
		}
		// Jobs (items 11, 19-21): spice for the Emperor's demands until Stilgar
		// joins (phase 0x2c); then the new troops train, three miners stay on
		// spice (the user's route, 2026-09-28).
		for (uint k = 0; k < troops.size(); ++k) {
			const Troop t = _world.troop(troops[k]);
			if ((t.occupation & 0x0f) == Troop::kWaitingForOrders && !(t.occupation & 0x40))
				speedrunOrders(troops[k], (phase < 0x2c || k < 3) ? Troop::kSpiceMining : kJobTraining, true, -2);
		}
		speedrunSpice();
		speedrunGarrison();
		speedrunKeepInTouch();
		if (_mode == kRoom)
			speedrunWait();
		if (!_speedrunRoundVisits && _state.b(GameState::kPhase) == phase && !_speedrunDone.empty()) {
			// Nothing new anywhere: look again everywhere (the old sweep),
			// rather than call the story blocked.
			speedrunLog("nothing new on the route: every place again");
			_speedrunDone.clear();
		}
	}
	if (_state.b(GameState::kPhase) >= 0x4f)
		speedrunLog("step 29 OK: the worm phase (0x4f), CALL A WORM open");
}



bool GameScreen::speedrunOnce(const Common::String &key) {
	for (uint i = 0; i < _speedrunDone.size(); ++i)
		if (_speedrunDone[i] == key)
			return false;
	_speedrunDone.push_back(key);
	return true;
}

bool GameScreen::speedrunClickRow(RowAction action, int argument, const char *what) {
	// A command row clicked where it stands (rows at y 163 + 8 i, panel.h
	// kCommandTop), through the same event path as the player's click.
	for (uint i = 0; i < Panel::kCommandRows; ++i) {
		if (_rowActions[i] != action || (argument >= 0 && _rowArguments[i] != argument))
			continue;
		if (_panel.rowDisabled(i))
			return false; // A disabled row is not a successful command.
		speedrunPause(900); // the watcher reads the menu first
		Common::Event event;
		event.type = Common::EVENT_LBUTTONDOWN;
		event.mouse = Common::Point(160, 163 + 8 * (int)i);
		_system->warpMouse(event.mouse.x, event.mouse.y);
		handleEvent(event);
		if (what)
			speedrunLog(Common::String::format("orders: %s", what));
		return true;
	}
	return false;
}

bool GameScreen::speedrunOpenOrders(uint id) {
	// In the troop's sietch: its chief, " TALK TO ME " then GIVE ORDERS TO
	// TROOP (seg000:5a03: the chief of the k-th hired troop there).
	const int at = _world.troopPlace(id);
	if (_mode == kRoom && !_desert && at >= 0 && (uint)at == _world.currentLocation() &&
			!(_world.troop(id).occupation & 0x40)) {
		Common::Array<uint> ids;
		_world.troopsAt((uint)at, ids);
		uint k = 0;
		bool found = false;
		for (uint i = 0; i < ids.size() && !found; ++i) {
			const Troop t = _world.troop(ids[i]);
			if (t.harkonnen() || !t.hired())
				continue;
			if (ids[i] == id)
				found = true;
			else
				++k;
		}
		Common::Array<byte> people;
		_world.peopleInRoom(people);
		bool chief = false;
		for (uint i = 0; i < people.size(); ++i)
			chief |= found && people[i] == World::kFremenChief + k;
		if (chief) {
			startConversation(World::kFremenChief + k);
			if (speedrunClickRow(kRowGiveOrders, -1, Common::String::format("the chief of troop %u, GIVE ORDERS TO TROOP", id).c_str()) &&
					_mode == kTroop && _troopId == id)
				return true;
			if (inConversation())
				endConversation();
		}
	}
	// Elsewhere: the map, and the troop's popup through its contact.
	if (inConversation())
		endConversation();
	openMap(MapScreen::kFlat, false);
	_map->setCaption(false);
	speedrunPause(700);
	openTroop(id, true);
	_troopFromRoom = false;
	speedrunLog(Common::String::format("orders: troop %u contacted from the map", id));
	return _mode == kTroop;
}

void GameScreen::speedrunCloseOrders() {
	// NO MORE ORDERS, back to the room.
	if (_mode == kTroop) {
		if (_troopChoosing) {
			_troopChoosing = false;
			drawTroop();
		}
		if (!speedrunClickRow(kRowTroopDone, -1, nullptr))
			_mode = kMap;
	}
	if (_mode == kMap || _mode == kTroop)
		leaveMap();
	_mode = kRoom;
	_troopFromRoom = false;
}

void GameScreen::speedrunOrders(uint id, int job, bool harvester, int moveTo, const uint *queue, uint count) {
	if (_ending || !_world.troop(id).hired())
		return;
	auto current = [&]() { return (int)(_world.troop(id).occupation & 0x0f); };
	// The prospectors' SPECIALIZE IN SPICE makes them prospect (kRowSetOccupation).
	const int wanted = (job == Troop::kSpiceMining && id == World::kProspectorTroop && current() == Troop::kWaitingForOrders)
			? (int)Troop::kProspecting : job;
	byte counts[7];
	const int at = _world.troopPlace(id);
	if (at >= 0)
		_world.placeFreeEquipment((uint)at, counts);
	bool takeHarvester = harvester && at >= 0 && counts[0] && !(_world.troop(id).equipment & 0x80) &&
			!(_world.troop(id).occupation & 0x40);
	bool change = wanted >= 0 && current() != wanted;
	if ((change || takeHarvester) &&
			(READ_LE_UINT16(_state.vars + World::kTroopTable + (id - 1) * World::kTroopSize + 0x10) & 0x200)) {
		// A troop repairing a damaged harvester (saboteurs or a worm,
		// World::harvesterEvents) refuses a new occupation ("We have to
		// repair our equipment before doing anything else!") and MODIFY
		// EQUIPMENT is greyed, until its slot the next day: as a player
		// would, the bot waits and orders it again after the repair. A move
		// is still taken.
		speedrunLog(Common::String::format("troop %u is repairing its harvester: the occupation and equipment orders wait", id));
		change = takeHarvester = false;
	}
	if (!change && !takeHarvester && moveTo < -1)
		return;
	const bool open = speedrunOpenOrders(id);
	static const char *const kJobs[16] = { "SPECIALIZE IN SPICE", "Spice Prospecting", "waiting", "", "SPECIALIZE IN ARMY",
		"ESPIONAGE", "", "", "SPECIALIZE IN ECOLOGY", "", "", "", "", "", "", "" };
	if (change) {
		bool done = false;
		if (open && speedrunClickRow(kRowTroopOccupation, -1, Common::String::format("troop %u, %s TROOP OCCUPATION", id,
				current() == Troop::kWaitingForOrders ? "SELECT" : "CHANGE").c_str())) {
			const int row = (current() == Troop::kWaitingForOrders && wanted == Troop::kProspecting) ? (int)Troop::kSpiceMining : wanted;
			if (speedrunClickRow(kRowSetOccupation, row, Common::String::format("troop %u, %s", id,
					wanted == Troop::kProspecting && current() != Troop::kWaitingForOrders ? "Spice Prospecting" :
					wanted == Troop::kSpiceMining && current() != Troop::kWaitingForOrders ? "Spice Mining" : kJobs[wanted & 15]).c_str())) {
				// The prospectors' map lesson runs over the popup: " Continue..." to its end.
				for (uint guard = 0; _troopScene && guard < 8; ++guard)
					speedrunClickRow(kRowContinue, -1, nullptr);
				done = current() == wanted;
			} else if (_troopChoosing) {
				_troopChoosing = false;
				drawTroop();
			}
		}
		if (!done && current() != wanted) {
			// Missing/disabled rows and refusal lines are gameplay rules.
			// Keep the accepted state and retry later if it becomes possible.
			speedrunLog(Common::String::format("orders: troop %u defers %s (no enabled row, or refused)", id, kJobs[wanted & 15]));
		}
	}
	if (takeHarvester) {
		if (open && _mode == kTroop && speedrunClickRow(kRowEquipment, -1, Common::String::format("troop %u, MODIFY EQUIPMENT", id).c_str())) {
			const Common::Rect item = _equipRects[1][0];
			if (!item.isEmpty()) {
				Common::Event event;
				event.type = Common::EVENT_LBUTTONDOWN;
				event.mouse = Common::Point((item.left + item.right) / 2, (item.top + item.bottom) / 2);
				handleEvent(event); // MODIFY EQUIPMENT's actual item hit test.
			}
			speedrunLog(Common::String::format("orders: troop %u %s the harvester lying at place %d", id,
					(_world.troop(id).equipment & 0x80) ? "takes" : "could not take", at));
			speedrunClickRow(kRowEquipDone, -1, nullptr);
		} else {
			speedrunLog(Common::String::format("orders: troop %u defers the harvester (no enabled equipment row)", id));
		}
	}
	if (moveTo >= -1) {
		bool picked = false;
		if (open && _mode == kTroop && speedrunClickRow(kRowMoveTroop, -1, Common::String::format("troop %u, MOVE TROOP", id).c_str()) &&
				_troopPicking) {
			speedrunPause(1200); // the density popup and the caption
			if (moveTo == -1 && queue) {
				_pickCount = 0;
				_pickQueue[0] = _pickQueue[1] = _pickQueue[2] = 0;
				for (uint k = 0; k < count && k < 3; ++k)
					_pickQueue[_pickCount++] = World::placeOffset(queue[k]);
				drawTroop();
				speedrunPause(1200);
				endTroopPick(-3);
			} else {
				endTroopPick(moveTo);
			}
			picked = true;
			speedrunLog(Common::String::format("orders: troop %u picks %s", id,
					moveTo >= 0 ? Common::String::format("place %d", moveTo).c_str() : "its three sietches"));
		}
		if (!picked) {
			speedrunLog(Common::String::format("orders: troop %u defers movement (no enabled move row)", id));
		}
	}
	speedrunCloseOrders();
}

void GameScreen::speedrunSpice() {
	// Every spice troop of ours, the biggest first (the harvest grows with
	// the men, seg000:708a).
	Common::Array<uint> ids;
	for (uint id = 1; id <= World::kTroops; ++id) {
		const Troop t = _world.troop(id);
		const byte job = t.occupation & 0x0f;
		if (t.id && t.hired() && !t.harkonnen() && (job == Troop::kSpiceMining || job == Troop::kProspecting))
			ids.push_back(id);
	}
	Common::sort(ids.begin(), ids.end(), [&](uint a, uint b) { return _world.troop(a).population > _world.troop(b).population; });
	{
		// The spice troops once a day, for the logs.
		static uint lastDay = 0;
		if (_world.day() != lastDay) {
			lastDay = _world.day();
			Common::String line;
			for (uint k = 0; k < ids.size(); ++k) {
				const Troop t = _world.troop(ids[k]);
				const int at = _world.troopPlace(ids[k]);
				const Location l = _world.location(at < 0 ? 0 : (uint)at);
				line += Common::String::format(" %u@%d(occ %#x d%#x s%#x m%u%s)", ids[k], at, t.occupation, l.spiceDensity, l.status,
						t.motivation, (t.equipment & 0x80) ? " H" : "");
			}
			speedrunLog("spice troops:" + line);
		}
	}
	// The harvesters lying free (MODIFY EQUIPMENT takes one where the troop
	// stands, seg000:7cbb), and those already claimed by a troop on its way.
	Common::HashMap<uint, int> spare;
	for (uint i = 0; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (l.hidden() || !_world.friendlyPlace(i) || (l.status & 0x02))
			continue;
		byte c[7];
		_world.placeFreeEquipment(i, c);
		if (c[0])
			spare[i] = c[0];
	}
	for (uint k = 0; k < ids.size(); ++k) {
		const Troop t = _world.troop(ids[k]);
		const int to = _world.troopPlace(ids[k]);
		if ((t.occupation & 0x40) && !(t.equipment & 0x80) && to >= 0 && spare.contains((uint)to))
			spare[(uint)to]--;
	}
	for (uint k = 0; k < ids.size(); ++k) {
		const uint id = ids[k];
		const Troop t = _world.troop(id);
		const int at = _world.troopPlace(id);
		if (at < 0 || (t.occupation & 0x60) || id == World::kProspectorTroop)
			continue; // marching, not ours to order, or not a miner
		// A harvester multiplies the harvest by four (seg000:708a): take one
		// here, or march to the nearest one lying free, however far.
		if (!(t.equipment & 0x80)) {
			byte here[7];
			_world.placeFreeEquipment((uint)at, here);
			if (here[0]) {
				speedrunOrders(id, -1, true, -2);
				if (spare.contains((uint)at))
					spare[(uint)at]--;
			} else {
				int best = -1;
				uint bestDistance = 0xffff;
				for (Common::HashMap<uint, int>::const_iterator it = spare.begin(); it != spare.end(); ++it) {
					if (it->_value <= 0 || (int)it->_key == at || _world.wouldQuarrel(id, it->_key))
						continue; // north and south quarrel (World::fremenQuarrel)
					const uint d = _world.placeDistance((uint)at, it->_key);
					if (d < bestDistance) {
						bestDistance = d;
						best = (int)it->_key;
					}
				}
				if (best >= 0 && id != World::kProspectorTroop && t.motivation >= 8) {
					speedrunOrders(id, -1, false, best); // MOVE TROOP to the harvester
					spare[(uint)best]--;
					speedrunLog(Common::String::format("troop %u (%u men) goes to fetch the harvester at place %d", id,
							t.population, best));
					continue;
				}
			}
		}
		// Ordinary miners cannot become prospectors from the class menu
		// (CD 69b3, ds:216e; floppy 774d, ds:27d4). Troop 3's existing
		// queue prospects the fields; miners wait or move to another field.
	}
	speedrunSpiceFields(ids);
}

void GameScreen::speedrunSpiceFields(const Common::Array<uint> &troops) {
	// The Emperor's demands grow by about 1500 kg each time and two short
	// shipments in a row end the game (floppy sub_4748), so the spice has to
	// come from the rich fields, as a player does: the harvest is the field's
	// density times the troop (seg000:708a).
	// A sietch a Harkonnen raid can reach (speedrunGarrison) ranks below
	// every safe field once the raids can come; it is used when nothing
	// else is left.
	Common::Array<bool> exposed;
	exposed.resize(_world.locationCount());
	for (uint i = 0; i < _world.locationCount(); ++i)
		// A conquered fortress turns into a sietch two days later (CD
		// 6e28-6e47, floppy 7b90-7baf): anticipate the coming raid exposure.
		exposed[i] = _state.b(GameState::kPhase) >= 0x2c && _world.raidSource(i) >= 0;
	auto richness = [&](uint i) { return (uint)(_world.location(i).spiceDensity & 0xf0) + (exposed[i] ? 0 : 0x100); };
	// Any place of ours with a field: sietches, and the forts and villages
	// taken from the Harkonnens.
	auto usable = [&](uint i) {
		const Location l = _world.location(i);
		return !l.hidden() && _world.friendlyPlace(i) && !(l.status & 0x02) && l.type != Location::kPalace;
	};
	// The prospectors (troop 3, MOVE TROOP, seg000:8064): the three richest
	// sietches not yet prospected, once their queue is done.
	const Troop p = _world.troop(World::kProspectorTroop);
	if (p.hired() && (p.occupation & 0x0f) == Troop::kProspecting && !(p.occupation & 0x40) &&
			!_world.prospectorDestination(0) && p.motivation >= 12) {
		Common::Array<uint> fields;
		for (uint i = 0; i < _world.locationCount(); ++i)
			if (usable(i) && !(_world.location(i).status & 0x40) && (_world.location(i).spiceDensity & 0xf0))
				fields.push_back(i);
		Common::sort(fields.begin(), fields.end(), [&](uint a, uint b) { return richness(a) > richness(b); });
		if (!fields.empty()) {
			Common::String list;
			uint queue[3], count = 0;
			for (uint k = 0; k < 3 && k < fields.size(); ++k) {
				queue[count++] = fields[k];
				list += Common::String::format(" %u", fields[k]);
			}
			speedrunLog(Common::String::format("the prospectors go to prospect places%s", list.c_str()));
			speedrunOrders(World::kProspectorTroop, -1, false, -1, queue, count); // MOVE TROOP, three sietches
		} else {
			// An idle prospecting troop need not wait beside a hostile fort.
			// Its ordinary destination picker also accepts a prospected sietch.
			const int at = _world.troopPlace(World::kProspectorTroop);
			uint nearest = 0xffff;
			int refuge = -1;
			for (uint i = 0; at >= 0 && exposed[(uint)at] && i < _world.locationCount(); ++i) {
				if (!usable(i) || exposed[i] || !_world.location(i).isSietch())
					continue;
				const uint distance = _world.placeDistance((uint)at, i);
				if (distance < nearest) {
					nearest = distance;
					refuge = (int)i;
				}
			}
			if (refuge >= 0) {
				const uint queue[1] = { (uint)refuge };
				speedrunLog(Common::String::format("idle prospectors leave exposed place %d for safe place %d", at, refuge));
				speedrunOrders(World::kProspectorTroop, -1, false, -1, queue, 1);
			}
		}
	}
	// The miners: to the richest prospected field that is not exhausted, three
	// per field, when it beats theirs by three density steps (a march costs
	// three motivation, seg000:6f93).
	Common::Array<uint> fields;
	for (uint i = 0; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (usable(i) && (l.status & 0x40) && !(l.status & 0x01) && (l.spiceDensity & 0xf0))
			fields.push_back(i);
	}
	Common::sort(fields.begin(), fields.end(), [&](uint a, uint b) { return richness(a) > richness(b); });
	Common::HashMap<uint, uint> assigned;
	for (uint k = 0; k < troops.size(); ++k) {
		const Troop t = _world.troop(troops[k]);
		const int at = _world.troopPlace(troops[k]);
		if (at >= 0 && (t.occupation & 0x0f) == Troop::kSpiceMining)
			assigned[(uint)at]++;
	}
	for (uint k = 0; k < troops.size(); ++k) {
		const uint id = troops[k];
		const Troop t = _world.troop(id);
		if (id == World::kProspectorTroop || !t.hired() || (t.occupation & 0x0f) != Troop::kSpiceMining ||
				(t.occupation & 0x40))
			continue;
		const int at = _world.troopPlace(id);
		if (at < 0)
			continue;
		// A miner whose field is spent (or who stopped) moves on at any
		// motivation: to a prospected field in work; otherwise it waits for troop 3.
		const Location spent = _world.location((uint)at);
		// The harvest reads the density's high nibble (seg000:708a): below 0x10
		// a field yields nothing though it is not flagged exhausted. A march
		// costs three motivation and below five the troop sulks and sits out
		// (seg000:6f93, the 0x20 refusal skipped at 6c92), so only a troop
		// with motivation to spare moves.
		const bool stuck = (spent.status & 0x01) || spent.spiceDensity < 0x10 || (t.occupation & Troop::kStopped);
		if (stuck && t.motivation < 8)
			continue;
		if (stuck) {
			bool moved = false;
			for (uint f = 0; f < fields.size() && !moved; ++f) {
				const uint i = fields[f];
				if ((int)i == at || assigned[i] >= 3 || _world.wouldQuarrel(id, i))
					continue;
				speedrunLog(Common::String::format("troop %u leaves spent place %d to mine place %u (density %#x)", id, at, i,
						_world.location(i).spiceDensity));
				speedrunOrders(id, -1, false, (int)i);
				if (_world.troop(id).occupation & 0x40) {
					assigned[i]++;
					moved = true;
				}
			}

			continue;
		}
		if (t.motivation < 12)
			continue;
		// Only a prospected, unexhausted field can supply the miner (6b96).
		const Location here = _world.location((uint)at);
		const uint mine = (here.status & 0x41) == 0x40 && (here.spiceDensity & 0xf0) ? richness((uint)at) : 0;
		for (uint f = 0; f < fields.size(); ++f) {
			const uint i = fields[f];
			if ((int)i == at || richness(i) < mine + 0x30 || assigned[i] >= 3 || _world.wouldQuarrel(id, i))
				continue; // no field where the north and the south would meet
			speedrunLog(Common::String::format("troop %u marches to mine place %u (density %#x, was %#x)", id, i,
					_world.location(i).spiceDensity, here.spiceDensity));
			speedrunOrders(id, -1, false, (int)i);
			if (_world.troop(id).occupation & 0x40) {
				assigned[i]++;
				if (assigned[(uint)at])
					assigned[(uint)at]--;
			}
			break;
		}
	}
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
	// The landing may have moved him (CD 2170: Stilgar and the later
	// characters wander the palace's rooms): read the record again.
	const Character now = _world.character(who);
	showRoom(now.room ? now.room : 1);
	speedrunConverse(); // an entry line
	Common::Array<byte> people;
	_world.peopleInRoom(people);
	for (uint i = 0; i < people.size(); ++i)
		if (people[i] == who)
			return true;
	const byte *r = _state.vars + World::kCharacterTable + who * World::kCharacterSize;
	speedrunLog(Common::String::format("character %u is not in room %u of place %d (record %02x %02x %02x %02x %02x, ds:4-7 %02x %02x %02x %02x)",
			who, now.room, place, r[0], r[1], r[2], r[3], r[4], _state.b(4), _state.b(5), _state.b(6), _state.b(7)));
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
	// One already converted to a sietch first: a fort that is still one
	// (no troop training there on a new day, 6e20) keeps Jessica out.
	int council = -1;
	for (uint index = 2; index <= 4 && council < 0; ++index)
		if (_world.friendlyPlace(index) && _world.location(index).type < Location::kFortressMin)
			council = (int)index;
	for (uint index = 2; index <= 4 && council < 0; ++index)
		if (_world.friendlyPlace(index))
			council = (int)index;
	if (council < 0) {
		speedrunLog("BLOCKED none of the places by the palace is ours");
		return;
	}
	// A fort taken today is a sietch only from the next day (floppy sub_9A58);
	// until then Jessica refuses it ("Oh no Paul! I don't like this place").
	for (uint days = 0; days < 3 && !_ending && _world.location((uint)council).type >= Location::kFortressMin; ++days) {
		speedrunLog(Common::String::format("waiting for the next day: place %d is still a fort", council));
		passTime(World::kSlotsPerDay - _world.timeSlot());
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
	// The allies wait in room 2: whoever waits in room 1 of a sietch
	// walks further in whenever Paul lands elsewhere (CD 2170), so a
	// player parks them there.
	rideWormTo(council);
	if (_ending)
		return;
	enterRoom(2);
	if (!((_state.w(GameState::kPersonsWith) >> kThufir) & 1)) {
		_world.setTravelling(kThufir, true);
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
		enterRoom(2);
		// A refusal (her topic-6 answer) can give way once her pending lines
		// are said: talk, then ask again (three times at most).
		for (uint tries = 0; tries < 3 && !_ending && !speedrunCompanion(parked[k], false); ++tries) {
			startConversation(parked[k]);
			speedrunConverse();
			speedrunWait();
		}
	}
	if (!((_state.w(GameState::kPersonsWith) >> kStilgar) & 1) && speedrunMeet(kStilgar))
		speedrunCompanion(kStilgar, true);
	if (speedrunMeet(kChani))
		speedrunCompanion(kChani, true);
	rideWormTo(council);
	enterRoom(2); // the room's entry scan: "Hey! here we are."
	speedrunConverse();
	if (stage() == 4)
		speedrunLog("step 51 OK: the war council, stage 4");
	else
		force(4, Common::String::format("the council did not start, persons in room %#x", _state.w(GameState::kPersonsInRoom)).c_str());
	// The 10 000 men with atomics (item 52). A troop freed after its
	// capture stands apologizing (occupation 0x22, 75af) and cannot march;
	// a new job writes the whole occupation byte (6aea) and frees it, as a
	// player's CHANGE TROOP OCCUPATION does.
	for (uint id = 1; id <= World::kTroops; ++id) {
		const Troop c = _world.troop(id);
		const int at = _world.troopPlace(id);
		if (c.id && !c.harkonnen() && c.hired() && (c.occupation & 0x20) && (c.equipment & 0x04) && at >= 0 &&
				_world.friendlyPlace((uint)at) && !_world.placeInBattle((uint)at)) {
			_world.setTroopOccupation(id, kJobTraining);
			speedrunLog(Common::String::format("troop %u (atomics, occupation %#x at %d) back to army training", id, c.occupation, at));
		}
	}
	for (uint id = 1; id <= World::kTroops; ++id) {
		const Troop t = _world.troop(id);
		const int here = _world.troopPlace(id);
		if (!t.id || t.harkonnen() || !t.hired() || (t.occupation & 0x60) || !(t.equipment & 0x04) || here < 0)
			continue;
		if (here < 2 || here > 4)
			_world.issueMoveOrder(id, 2 + id % 3);
	}
	// Atomics lying free, including around the palace: a store there
	// still needs a troop to carry it. As a player short of men would,
	// the biggest troop without atomics marches there, takes them (MODIFY
	// EQUIPMENT) and comes back.
	Common::HashMap<uint, uint> fetch; // troop -> the place of the atomics
	for (uint i = 0; i < _world.locationCount(); ++i) {
		if (!_world.friendlyPlace(i) || _world.placeInBattle(i))
			continue;
		byte c[7];
		_world.placeFreeEquipment(i, c);
		for (uint n = 0; n < c[5]; ++n) {
			uint best = 0;
			for (uint id = 1; id <= World::kTroops; ++id) {
				const Troop t = _world.troop(id);
				if (!t.id || t.harkonnen() || !t.hired() || (t.occupation & 0x60) || (t.equipment & 0x04) ||
						_world.troopPlace(id) < 0 || fetch.contains(id) || id == World::kProspectorTroop)
					continue;
				if (!best || t.population > _world.troop(best).population)
					best = id;
			}
			if (!best)
				break;
			fetch[best] = i;
			if (_world.troopPlace(best) != (int)i)
				_world.issueMoveOrder(best, i);
			speedrunLog(Common::String::format("troop %u (%u men) goes to fetch the atomics lying at place %u", best,
					_world.troop(best).population, i));
		}
	}
	for (uint p = 0; p < 96 && !_world.finalAttackReady() && speedrunAlive(); ++p) {
		passTime(1);
		for (Common::HashMap<uint, uint>::iterator it = fetch.begin(); it != fetch.end(); ++it) {
			const Troop t = _world.troop(it->_key);
			if (it->_value == 0xffff || (t.occupation & 0x40) || _world.troopPlace(it->_key) != (int)it->_value)
				continue;
			// MODIFY EQUIPMENT for the fetcher itself (the others there would
			// take them first in speedrunEquip's order).
			if (_world.takeEquipment(it->_key, kAtomics))
				speedrunLog(Common::String::format("troop %u takes equipment %u at place %u", it->_key, (uint)kAtomics, it->_value));
			if (_world.troop(it->_key).equipment & 0x04) {
				_world.issueMoveOrder(it->_key, 2 + it->_key % 3);
				speedrunLog(Common::String::format("troop %u has the atomics and marches back to place %u", it->_key, 2 + it->_key % 3));
			}
			it->_value = 0xffff;
		}
		for (uint index = 2; index <= 4; ++index) {
			speedrunEquip(index);
			Common::Array<uint> ids;
			_world.troopsAt(index, ids);
			for (uint i = 0; i < ids.size(); ++i) {
				const Troop t = _world.troop(ids[i]);
				// A troop repairing its harvester refuses the new job until the
				// repair (World::mineSpice, 705c); the bot asks again later.
				const bool repairing = (READ_LE_UINT16(_state.vars + World::kTroopTable + (ids[i] - 1) * World::kTroopSize + 0x10) & 0x200) != 0;
				if (t.hired() && !t.harkonnen() && !(t.occupation & 0x60) && (t.occupation & 0x0f) != kJobTraining && !repairing)
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
	speedrunLog("coverage engine-assisted-v1: direct campaign actions and battle save/reload retries; player-UI completion unverified");
	if (part == "orders") {
		// Deliberately seeded regression states, not campaign progression.
		// Test the bot against the same rows and reaction gate as a player.
		const uint id = 2;
		speedrunLog("order-check setup: seeded troop 2 and phase/reaction bits; not a playthrough");
		_world.rallyTroop(id);
		_world.setTroopOccupation(id, Troop::kWaitingForOrders);
		_state.setB(GameState::kPhase, 1);
		speedrunOrders(id, Troop::kSpiceMining, false, -2);
		const bool selected = (_world.troop(id).occupation & 0x0f) == Troop::kSpiceMining;
		speedrunLog(Common::String::format("order-check %s enabled occupation is accepted", selected ? "PASS" : "FAIL"));

		const Troop beforeMove = _world.troop(id);
		speedrunOrders(id, -1, false, 12);
		const Troop afterMove = _world.troop(id);
		speedrunLog(Common::String::format("order-check %s phase-1 disabled move preserves occupation and destination",
				beforeMove.occupation == afterMove.occupation && beforeMove.location == afterMove.location ? "PASS" : "FAIL"));

		_state.setB(GameState::kPhase, 5);
		const byte beforeJob = _world.troop(id).occupation;
		speedrunOrders(id, Troop::kProspecting, false, -2);
		speedrunLog(Common::String::format("order-check %s absent prospecting row preserves ordinary miner",
				_world.troop(id).occupation == beforeJob ? "PASS" : "FAIL"));

		// The conversion/rest refusal reads troop bitfield_10 bit 5 in its
		// list-4 line (CD condition 627, Amiga/floppy condition 626).
		_state.setB(GameState::kPhase, 0x50);
		_world.setTroopOccupation(id, Troop::kMilitaryTraining);
		byte *record = _state.vars + World::kTroopTable + (id - 1) * World::kTroopSize;
		WRITE_LE_UINT16(record + 0x10, READ_LE_UINT16(record + 0x10) | 0x20);
		const byte beforeRefusal = _world.troop(id).occupation;
		speedrunOrders(id, Troop::kSpiceMining, false, -2);
		speedrunLog(Common::String::format("order-check %s refused occupation preserves soldier",
				_world.troop(id).occupation == beforeRefusal ? "PASS" : "FAIL"));
		// A seeded just-won fort beside a hostile garrison, and a weaker
		// safe field. Only the two sites are visible to the bot planner.
		int exposed = -1, safe = -1;
		for (uint i = 2; i < _world.locationCount(); ++i) {
			if (!_world.location(i).isSietch())
				continue;
			if (_world.raidSource(i) >= 0 && exposed < 0)
				exposed = (int)i;
			else if (_world.raidSource(i) < 0 && safe < 0)
				safe = (int)i;
		}
		if (exposed < 0 || safe < 0) {
			speedrunLog("order-check FAIL field-safety fixture needs exposed and safe sietches");
		} else {
			for (uint i = 2; i < _world.locationCount(); ++i)
				_world.setLocationStatus(i, (byte)(_world.location(i).status | 0x80));
			for (uint n = 1; n <= World::kTroops; ++n) {
				const int at = _world.troopPlace(n);
				if (n != id && n != World::kProspectorTroop && (at == exposed || at == safe))
					_world.placeTroopForTest(n, 0, Troop::kWaitingForOrders, 0);
			}
			byte *risk = _state.vars + World::placeOffset((uint)exposed);
			risk[8] = (byte)(Location::kFortressMin + (risk[8] & 7));
			risk[10] = 0x48; // friendly, prospected, awaiting fortress conversion
			risk[18] = 0xf0;
			byte *refuge = _state.vars + World::placeOffset((uint)safe);
			refuge[10] = 0x40;
			refuge[18] = 0x20;
			_world.placeTroopForTest(id, (uint)safe, Troop::kSpiceMining, 0);
			_world.placeTroopForTest(World::kProspectorTroop, (uint)exposed, Troop::kProspecting | Troop::kStopped, 0);
			_world.setTroopByteForTest(id, 0x15, 50);
			_world.setTroopByteForTest(World::kProspectorTroop, 0x15, 50);
			for (uint k = 0; k < 3; ++k)
				_world.setProspectorDestination(k, 0);
			speedrunLog(Common::String::format("order-check setup: held fort %d, safe field %d; not a playthrough", exposed, safe));
			Common::Array<uint> miners;
			miners.push_back(id);
			speedrunSpiceFields(miners);
			speedrunLog(Common::String::format("order-check %s miner avoids a fort's coming raid exposure",
					_world.troopPlace(id) == safe && !(_world.troop(id).occupation & 0x40) ? "PASS" : "FAIL"));
			speedrunLog(Common::String::format("order-check %s idle prospectors accept the safe destination queue",
					_world.troopPlace(World::kProspectorTroop) == safe && (_world.troop(World::kProspectorTroop).occupation & 0x40)
					? "PASS" : "FAIL"));
		}
		speedrunLog("end");
		return;
	}
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

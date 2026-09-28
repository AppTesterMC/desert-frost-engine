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

#ifndef ENGINES_DUNE_WORLD_H
#define ENGINES_DUNE_WORLD_H

#include "common/array.h"
#include "common/endian.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "dune/dialogue.h"

namespace Dune {

class Resource;
class SentenceBank;
class StartupLog;

/**
 * One of the 70 places of Arrakis: a 28-byte record at data-segment offset
 * 0x100 + 28 * index (madmoose's dune-rust crates/savegame names most
 * fields; the CD executable's sub_15274/sub_15344 read them).
 */
struct Location {
	enum {
		kSietchMax = 0x1f,
		kPalace = 0x20,
		kVillageMin = 0x21,
		kVillageMax = 0x27,
		kFortressMin = 0x28,
		kFortressMax = 0x2f,
		kHarkonnenPalace = 0x30,
		kRecordSize = 28,
		kTableOffset = 0x100
	};

	byte firstName, lastName; ///< COMMAND ids: first = command(firstName - 1), last = command(11 + lastName)
	uint16 longitude;         ///< 0..65535 around the planet
	int16 latitude;           ///< -75..75
	byte type;                ///< < 0x20 sietch (its room table), 0x20 palace, 0x21-0x27 village, 0x28-0x2f fortress, 0x30 Harkonnen palace
	byte troop;
	byte status;              ///< bit 7 hidden, bit 6 prospected, bit 1 battle, bit 0 exhausted (seg000:6b96)
	byte discoverPhase;       ///< story phase from which the place can be found (0xff never by itself)
	byte spiceField, spiceAmount, spiceDensity, harvesters, ornithopters, knives, guns, modules, atomics, bulbs, water;
	uint16 mapOffset;         ///< the place's MAP.HSQ cell (bytes 6-7, set by World::prepareNewGame)

	bool hidden() const { return (status & 0x80) != 0; }
	bool isSietch() const { return type <= kSietchMax; }
	bool isVillage() const { return type >= kVillageMin && type <= kVillageMax; }
	bool isFortress() const { return type >= kFortressMin && type <= kFortressMax; }
};

/** A room of a place: the room byte and its four exits (see palace.h). */
struct RoomRecord {
	byte code;
	byte exits[4];

	// The room byte minus one: low nibble SAL room, high nibble sheet slot
	// (World::sheetFor). The CD palace's greenhouse, 207, is SERRE room 14;
	// the floppy village, 129, VILG room 0.
	uint salRoom() const { return (uint)(code - 1) & 0x0f; }
	uint sheetSlot() const { return ((uint)(code - 1) >> 4) & 0x0f; }
};

/** One of the sixteen characters: 16 bytes at data-segment 0xfd8 + 16 * index. */
struct Character {
	byte room;            ///< 1-based room in the place's table
	byte placeType;
	byte locationPlusOne; ///< 1-based location index, 0xff when only the place type counts
	byte index;
	byte flags;           ///< bit 7 enemy, bit 5 met (as the saves show), bit 1 in the palace
};

/**
 * One of the 68 troops: 27 bytes at data-segment 0x8AA + 27 * (id - 1), the
 * same on both releases (field names after Lionel Debroux' odrade
 * troop.go; the CD executable's sub_13127 tells Fremen from Harkonnen by
 * byte 16 and "hired" by the occupation's bit 7).
 */
struct Troop {
	enum {
		kNotHired = 0x80,
		kSpiceMining = 0x00,     ///< Fremen occupations 0-11: COMMAND 36 + occupation (1-based)
		kProspecting = 0x01,
		kWaitingForOrders = 0x02,
		kMilitaryTraining = 0x04,
		kEspionage = 0x05,
		kIrrigation = 0x08,
		kWindTrap = 0x09,
		kBulbGrowing = 0x0a,
		kStopped = 0x10          ///< bit 4: the job cannot go on (sub_17085)
	};

	uint id;             ///< 1-based, 0 when the record is empty
	byte next;           ///< the next troop at the same place, 0 = last
	byte occupation;     ///< bit 7: not hired, bit 6 moving, bit 5 captured, bit 4 stopped; low nibble the job
	uint16 location;     ///< ds offset of its location record (+4)
	byte kind;           ///< byte 16: bit 7 Harkonnen
	byte dissatisfaction, motivation, spiceSkill, armySkill, ecologySkill, equipment;
	uint population;     ///< men (byte 26 x 10)

	bool harkonnen() const { return (kind & 0x80) != 0; }
	bool hired() const { return !(occupation & kNotHired); }
};

/**
 * The game world as the executable's data segment describes it: locations,
 * room tables, characters, the player's position. Everything lives in
 * GameState::vars so saves stay compatible; this class only interprets it.
 *
 * A new game starts from the executable's initial data segment, read from
 * the player's own DUNEPRG.EXE (floppy, LZEXE-packed) or DNCDPRG.EXE (CD)
 * and located by its first bytes. The floppy layout is 13 bytes longer from
 * about offset 0x1190 on (the name table and the room tables shift), which
 * findTables() handles by locating the palace room table.
 */
class World {
public:
	enum {
		kCharacters = 16,
		kCharacterTable = 0xfd8,
		kCharacterSize = 16,
		kPointerTableEntries = 0x34,
		kExitLeave = 252, ///< exits 252-254 leave the place
		kExitWalkOut = 0xfb, ///< exits 0xFB-0xFF walk out into the desert (floppy 422C)
		kTroopTable = 0x8aa,
		kTroopSize = 27,
		kProspectorTroop = 3,   ///< the prospectors (troops[2], ds:08e0)
		kTroops = 68,
		kFremen = 14,       ///< DIALOGUE/PERS group of a troop not hired yet
		kFremenChief = 15,  ///< and of a hired troop's chief
		kSpiceStock = 0xa0,     ///< word: the palace stock in 10 kg batches (seg000:701b)
		kCharisma = 0x29,
		kSlotsPerDay = 16,      ///< the time word counts sixteenths of a day (seg000:1ac5: day = time >> 4)
		/**
		 * Real milliseconds per time period: the PIT runs at 1193182 / 5957 Hz
		 * and the game clock advances every 12000 ticks (seg000:ef6a).
		 */
		kPeriodMillis = 59906,
		/**
		 * Real milliseconds per map cell in flight. travel_pump (seg000:4f0c)
		 * steps every 0x300 ticks of the 200 Hz counter (3.83 s), but the
		 * floppy recordings show about 0.64 s a cell over several flights
		 * (Palace -> Carthag-Tuek, 20 cells, about 12 s); the step's distance
		 * is not decoded yet, so the recordings' pace is used.
		 */
		kFlightStepMillis = 640
	};

	World(GameState &state, Resource &resources, StartupLog &log);

	/** Load the executable's initial data segment into the state (a new game). */
	bool loadInitialData();
	bool ready() const { return _tablesFound; }
	/**
	 * Sega CD: take the initial data from the game program (file 0 of the
	 * archive) instead of DUNE.EXE/DNCDPRG.EXE; segacd_world.cpp rebuilds the
	 * PC CD layout from it.
	 */
	void setSegaCdProgram(const Common::Array<byte> *program) { _segaCdProgram = program; }
	bool segaCd() const { return _segaCdProgram != nullptr; }

	uint locationCount() const;
	Location location(uint index) const;
	void setLocationStatus(uint index, byte status);
	/** Change the ornithopters on a place's pad (location byte 21), never below 0. */
	void setOrnithopters(uint index, int delta);
	Common::String locationName(uint index, const SentenceBank &sentences) const;

	uint currentLocation() const;
	byte placeType() const;
	uint room() const; ///< 1-based
	/** Move the player: position bytes, the name table (text codes 0x81/0x82) and the room. */
	void setPosition(uint locationIndex, uint room);

	bool roomTable(byte placeType, Common::Array<RoomRecord> &rooms) const;
	static const char *salFile(byte placeType);
	/** Sprite sheet for a room, from the release's slot table. */
	Common::String sheetFor(const RoomRecord &room) const;
	/** The CD's arrival video of a place kind; its last picture stays behind the exterior rooms. */
	static const char *arrivalVideo(byte placeType);
	/** The floppy release (DUNEPRG.EXE): loose files, SIET0/VILG/FORT exteriors, 13-byte longer layout. */
	bool floppy() const { return _floppy || _amiga; }
	/** The Amiga release: floppy-like rooms (pictures), CD data-segment layout. */
	bool amiga() const { return _amiga; }

	Character character(uint index) const;
	/** The executable's test (loc_136EE): the record's room, place type, 0x80 and location + 1 equal ds:4-7. */
	bool characterInRoom(uint index) const;
	/**
	 * Everybody the room shows, as DIALOGUE/PERS groups in ascending order:
	 * the characters whose record matches, plus the troops of a sietch in
	 * its room 2 (sub_13127): group 14 for a troop not hired yet, 15 for a
	 * hired troop's chief, once per troop.
	 */
	void peopleInRoom(Common::Array<byte> &people) const;
	enum { kCaptain = 12, kSmuggler = 13 }; ///< records ds:1098 and ds:10a8
	/** seg000:2318: the village's smugglers become the current ones (ds:10b4, 1c-20). */
	void stageSmugglers(uint index);
	/** A defeated Harkonnen troop at the place (the captain, seg000:316e), 0 none. */
	uint captainTroop(uint index) const;
	/** seg000:932e: before the captain speaks, stage the fort he knows of. */
	void prepareCaptain();
	/** Put a character's record where Paul is (a companion told to STAY HERE). */
	void settleCharacter(uint index);

	/**
	 * A CD data-segment offset in this release: the floppy's block is two
	 * bytes shorter from 0x1158 and thirteen longer from 0x11c0 (compared
	 * byte for byte with the CD's).
	 */
	uint ds(uint cdOffset) const {
		if (!_floppy || cdOffset < 0x1158)
			return cdOffset;
		return cdOffset < 0x11c0 ? cdOffset - 2 : cdOffset + _shift;
	}
	byte &var(uint cdOffset) { return _state.vars[ds(cdOffset)]; }
	/**
	 * The story's data effects of set_game_phase_and_trigger_callbacks
	 * (seg000:121f) for a chapter phase (a multiple of 4, 4..0x6c): the
	 * callbacks of the table at cs:11e7, transcribed with their character
	 * moves, doors, places and charisma. Returns the scripted scene (cs
	 * offset) or vision message they start, for the host to show (0 none).
	 */
	void phaseCallback(byte phase, uint16 &cutscene, uint16 &vision);
	/** Raise charisma by @p amount (seg000:6f78, capped at 200). */
	void addCharisma(uint amount);
	/**
	 * location_visibility_distance (ds:1176, init 1): how many map cells
	 * away Paul can reach troops. Below 2 the map offers GIVE ORDERS TO
	 * TROOP (the troop where Paul stands) instead of CONTACT FREMEN TROOPS.
	 */
	uint contactRange() const;
	/**
	 * Jessica's event 8 (seg000:a186): the first lesson takes the range from
	 * 1 to 30 with charisma + 10, each later one adds 20; with ds:0a bit 1
	 * set it is charisma + 40 and no limit. ds:d5 = 0x80 - range / 6 below
	 * 100, else 0. Returns the new range.
	 */
	uint raiseContactRange();
	/** Clear the hidden bit of the place whose record pointer is the word at @p cdOffset (seg000:a146). */
	void revealPointedPlace(uint cdOffset);

	Troop troop(uint id) const;
	/** The troops stationed at a place: its troop byte, then the chain of next ids. */
	void troopsAt(uint locationIndex, Common::Array<uint> &ids) const;
	/**
	 * The troop behind a Fremen person of the current place: group 14 the
	 * unrallied troop, 15 + k the k-th rallied one. 0 if none.
	 */
	uint troopForPerson(uint group) const;
	/**
	 * A troop's looks (character_id_to_sprite, seg000:913b): the head is
	 * FRM1-3 by id % 3 (also the PERS figure 14-16), the idle expression the
	 * rest of id / 3 folded below 15 (17 when the head is not FRM1).
	 */
	static uint fremenHead(uint troopId) { return troopId % 3; }
	static uint fremenExpression(uint troopId) {
		uint q = troopId / 3;
		const uint limit = (troopId % 3) ? 17 : 15;
		while (q >= limit)
			q -= limit;
		return q;
	}
	/** The first troop at the current place that is (@p hired) or is not hired yet; 0 if none. */
	uint localTroop(bool hired) const;
	/**
	 * COME WITH ME to a troop's leader (seg000:95c1): the charisma check.
	 * Passes when the Harkonnen population sum is below 1000, charisma is
	 * above 100, or (100 - charisma) / 4 <= the troop's motivation modifier.
	 */
	bool troopAgreesToFollow(uint id) const;
	/** troop_rally_troop (seg000:66ce): the troop waits for orders, charisma + 1. */
	bool rallyTroop(uint id);
	/** A new occupation (seg000:6acb): the job clocks restart; ecology without bulbs becomes bulb growing. */
	void setTroopOccupation(uint id, byte occupation);
	/** A troop's whole record (27 bytes), to take an order back (troop_apply_occupation_choice's refusal). */
	void saveTroopRecord(uint id, byte *record) const {
		if (id >= 1 && id <= kTroops)
			memcpy(record, _state.vars + kTroopTable + (id - 1) * kTroopSize, kTroopSize);
	}
	void restoreTroopRecord(uint id, const byte *record) {
		if (id >= 1 && id <= kTroops)
			memcpy(_state.vars + kTroopTable + (id - 1) * kTroopSize, record, kTroopSize);
	}
	/** Motivation modifier (seg000:6efd), which the harvest and the charisma check use. */
	uint motivationModifier(uint id) const;
	/** This period's harvest of a mining troop in kg (seg000:708a), 0 when it cannot mine. */
	uint harvestRate(uint id) const;

	/**
	 * The new-game pass of the executable (seg000:0169): snap each place to
	 * its map cell, store the cell's offset, its MAP2.HSQ spice field and the
	 * field's size, then settle every troop on its place (seg000:01e0).
	 * Verified against the original floppy's new-game save: all 70 places.
	 */
	bool prepareNewGame();

	/** Advance the clock by time periods, running each period's events (seg000:1b23). */
	void advanceTime(uint slots);
	uint day() const;         ///< 1-based
	uint timeSlot() const;    ///< 0-15
	uint spiceStock() const;  ///< kg
	/** First arrival at a hidden place (seg000:425b): it becomes known. */
	void markDiscovered(uint locationIndex);
	/** A phase the world asked for (Tuono-Harg found: 0x10), for the host to set; 0 none. */
	byte takeRequestedPhase() {
		const byte p = _requestedPhase;
		_requestedPhase = 0;
		return p;
	}
	/** Hidden sietches the story now lets the player find (discoverable_at_phase reached). */
	bool discoverable(uint locationIndex) const;
	/** Map cells between two places (seg000:7c8f: Chebyshev, longitude scaled by the row). */
	uint cellDistance(uint16 lng0, int16 lat0, uint16 lng1, int16 lat1) const;
	/** The row length in cells at a latitude (TABLAT), 0 before prepareNewGame. */
	uint rowCells(int latitude) const;
	/** The longitude units of one map cell at a latitude (ds:43C7): round(65536 / cells in the row). */
	uint unitsPerCell(int latitude) const;
	/**
	 * compass_angle (floppy 7DB4): the heading (256 a turn, 0 north,
	 * clockwise) from one position to another; false when they coincide.
	 */
	static bool compassAngle(uint16 fromLng, int16 fromLat, uint16 toLng, int16 toLat, byte &angle);
	/**
	 * travel_step_position without its heading update (floppy 7E87): one
	 * step along @p heading, the major axis a whole cell and the minor its
	 * share, the latitude's fraction kept in @p fraction (ds:11D9), at most
	 * one row a step; over a pole the heading turns round.
	 */
	void travelStep(uint16 &longitude, int16 &latitude, byte &fraction, byte &heading) const;

	const GameState &state() const { return _state; }
	GameState &mutableState() { return _state; }
	uint nameTableOffset() const { return GameState::kNameTable + _shift; }
	int layoutShift() const { return _shift; }
	/** Bytes of the data segment the original saves (4705 CD, 4718 floppy). */
	uint savedSize() const { return 0x1261 + _shift; }


	// ---- Ecology (ecology.cpp): the live map, vegetation and the green route ----
	enum {
		kMapCells = 50684,   ///< MAP.HSQ bytes: low nibble terrain, bits 4-5 stage, bit 6 a place
		kMapCentre = 0x62fc,
		kBulbPlace = 62      ///< the only place where ecology becomes bulb growing (seg000:6adc, record 0x7c8)
	};
	/** The live map (MAP.HSQ with its stage bits): 0x10 vegetation, 0x20 Atreides, 0x30 Harkonnen area. */
	Common::Array<byte> &map();
	/** A new game's map: MAP.HSQ as shipped, each place's cell marked with bit 6 (seg000:01a1). */
	void resetMap();
	/** Mark the places' cells again (bit 6) after a save's stage bits were applied. */
	void markPlaceCells();
	/** map_func (seg000:b58b): the map offset of a position; -1 off the map. */
	int mapCell(uint16 longitude, int16 latitude) const;
	/** The stage bits (0x30 mask) of the cell under a position. */
	byte cellStage(uint16 longitude, int16 latitude) const;
	/** One period of an ecology job (seg000:7693 irrigation, 7711 wind trap, 767d bulbs). */
	void runEcologyJob(uint id, uint index);
	/** The new day's ecology walk (seg000:63f0): water behind wind traps, the vegetation promotion. */
	void ecologyNewDay();
	/** seg000:6515: stamp the place's vegetation disc on the map. */
	void spreadVegetation(uint index);
	/** location_is_Atreides (seg000:5d36): a sietch or village, or a place held by the Atreides. */
	bool friendlyPlace(uint index) const;
	/** Harkonnen troops at a place, and Fremen troops attacking it (seg000:5082). */
	void countHostiles(uint index, uint &harkonnen, uint &attacking) const;
	/** compute_area_controlled_percentages (seg000:bfe3): ds:a2 Atreides, ds:a4 Harkonnen. */
	void computeAreas();
	/** A troop's own equipment (bit 7 harvester .. bit 1 bulbs) and the place's free stock (seg000:7f27). */
	void placeFreeEquipment(uint index, byte counts[7]) const;
	/** MODIFY EQUIPMENT: move one item of @p type (0 harvester .. 6 bulbs) between the troop and its place. */
	bool takeEquipment(uint id, uint type);
	bool giveEquipment(uint id, uint type);

	// ---- Story systems (story.cpp): spice shipments, COMM messages, visions ----
	enum StoryVar {
		kPaulEvents = 0x0a,      ///< bitfield_Paul_events: 0 had a vision, 1 drank the Water of Life, 4 met Stilgar
		kCurrentScene = 0x08,    ///< 1 outside a place, 0xff the desert
		kCurrentRoom = 0x0b,
		kArguing = 0x1a,         ///< menu choices made in this bargaining (ds:1a)
		kChoice = 0x9f,          ///< ACCEPT 1, REFUSE 2, ARGUE 3 (ds:9f)
		kOffers = 0xb4,          ///< four offered amounts (words, 10 kg batches)
		kDemand = 0xbc,          ///< the Emperor's demand (word, 10 kg batches)
		kFulfilment = 0xbe,      ///< last shipment / demand, 128 = exactly the demand
		kShipmentFlags = 0xbf,   ///< 0x80 armed, 0x10 demand pending, 0x20 demand seen, 0x08 short once, 0x40 generous
		kAgreed = 0xc0,          ///< the amount agreed with Duncan (word)
		kShipmentPaused = 0xc2,
		kShipments = 0xc3,       ///< demands so far
		kSightings = 0xc8,       ///< comm_sighting_count
		kUnread = 0xc9,
		kVisionType = 0xea       ///< vision_message_type_ds_ea
	};
	/** sub_12090: shipments start, the first demand is rolled today. Returns the COMM sighting to post. */
	uint16 armShipments();
	/**
	 * actions_time_in_day_3 (seg000:20a4): the demand's day, the reminders
	 * (COMM sightings from the Emperor, returned for posting), and the
	 * consequence on the fourth day (returns true).
	 */
	bool shipmentDay(uint16 &sighting);
	/** Days since the pending demand was made (the Emperor ends it on the fourth). */
	int daysSinceDemand() const;
	/** actions_time_in_day_8 (seg000:1dda): the vision reminder; false when not due. */
	bool shipmentReminderDue() const;
	/** Duncan's event 8 (seg000:2239): the offer ladder (22b1) and the smuggler bill he mentions (235f). */
	void duncanOffers();
	/** Duncan's event 9 (seg000:24ee): an accepted offer becomes the agreed amount. */
	void duncanAccept();
	/** Duncan's event 15 (seg000:24a3); returns the COMM sighting to post (0 none) and whether the talk ends. */
	uint16 duncanClosing(bool &endTalk);
	/** seg000:135ad: Paul enters the COMM room with Duncan after agreeing: the shipment goes. */
	bool shipmentReady() const;
	/** sub_12566: pay @p amount (10 kg batches), rate it, schedule the next demand. */
	void shipSpice(uint16 amount);
	/** A menu choice of the bargaining menu (seg000:241a, 2432, 2453) for Duncan (not the smugglers). */
	void bargainChoice(byte choice);

	/** comm_add_person_sighting (seg000:26da): (variant << 8) | person. Returns true when "a message has arrived" is queued. */
	void addSighting(uint16 sighting);
	uint sightingCount() const;
	uint16 sighting(uint index) const; ///< 0 = oldest
	/** Picking a message in the COMM viewer (seg000:290b): marks it seen, returns the person. */
	byte viewSighting(uint index, byte &variant);
	void dropOldestSighting();

	/** queue_vision_message (seg000:29ee/29f0): (sender << 8) | type, with an optional place pointer. */
	void queueVision(uint16 id, uint16 location = 0);
	uint visionCount() const;
	void vision(uint index, uint16 &id, uint16 &location) const;
	void dequeueVision();
	void purgeVisions(byte sender, uint16 location);
	void purgeArrivalVisions();
	/**
	 * A scripted scene's bytes, from its CD code offset (0x1321 the COMM room
	 * gathering, 0x12f8 the map lesson, ...), read from this release's
	 * executable. False when the executable was not found.
	 */
	bool sceneScript(uint16 cdOffset, Common::Array<byte> &bytes) const;
	/** The scene dialogue event 3 starts at the current phase (seg000:a1f7), as a CD offset. */
	uint16 phaseSceneScript() const;
	/** The Emperor's patience ran out (pending_room_screen_request 7, seg000:215f); cleared by reading. */
	bool takeEmperorEnding() {
		const bool e = _emperorEnding;
		_emperorEnding = false;
		return e;
	}
	/**
	 * Stilgar's event 8 (seg000:2ccf, run after his line through ds:227e):
	 * Paul has heard of the Water of Life; if he accepted (ds:9f = 1) he
	 * drinks it: 1 = he lives (charisma >= 100), 2 = he dies; 0 = no drink.
	 */
	uint stilgarWaterOfLife();
	/**
	 * Stilgar's event 9 (seg000:2d2c): the final attack. The shipments stop
	 * (ds:c2 + 1) and the army troops with atomics (occupation 4, equipment
	 * bit 2) are chosen; returns them. (Their march, seg000:84a6, is not built.)
	 */
	void finalAttackTroops(Common::Array<uint> &ids);
	/**
	 * prepare_location_data_for_condit (seg000:331e): stage a place for the
	 * dialogue conditions and placeholders: ds:11ce the place, ds:4d
	 * appearance, ds:4e its names ((first << 8) | last), ds:50 its region,
	 * ds:51 status, ds:52 spice density, ds:53 the free equipment mask,
	 * ds:54 water, ds:55-5b its stock, ds:5c/5e the Fremen troops' bits,
	 * ds:60-92 the troop counts (34a5), ds:94/96/9c the forces (33be), ds:f7
	 * the characters there (3385) and the nearest places with their compass
	 * points (5274).
	 */
	void stageLocationForConditions(uint index);
	/**
	 * The two companions shown in the panel (ui_hud_companion_1/2,
	 * ds:1152/1153; 0xff empty). Joining (seg000:9673) takes the first free
	 * slot; with both taken the first companion is sent home (returned) and
	 * the second moves up. Leaving (seg000:9655) closes the gap.
	 */
	int addCompanion(uint character);
	void removeCompanion(uint character);
	byte companion(uint slot) const { return _state.vars[ds(0x1152 + slot)]; }
	/** sub_11071: phase 0x14's first vision, when Paul waits alone in the desert. */
	void firstVision();

	// ---- Troop marches (troops.cpp) and battles (battle.cpp) ----
	/** Seed the two battle generators (not saved; dune_rng_seed pins them). */
	void seedRandom();
	/** rand (seg000:e3cc) and rand_masked (e3b7): LCGs outside the save, so a reload rerolls. */
	uint16 lcgRand();
	uint16 lcgRandMasked(uint16 mask);
	/** dune_fix_leto_loop: Leto is in no room after his death (phase 0x4c); off = as the original. */
	void setFixLetoLoop(bool on) { _fixLetoLoop = on; }
	bool fixLetoLoop() const { return _fixLetoLoop; }
	/**
	 * dune_fix_celimyn_tuek: the initial data gives Celimyn-Tuek (names 0x0c,
	 * 0x05) the discovery phase 0xff, which the phase compares (floppy
	 * seg000:6257, 6340, 7f56) never reach. On: the byte becomes 0x58 at new
	 * game and after a load (in memory; a save writes it). Off = as the original.
	 */
	void setFixCelimynTuek(bool on) { _fixCelimynTuek = on; }
	bool fixCelimynTuek() const { return _fixCelimynTuek; }
	/** Apply dune_fix_celimyn_tuek to the current state (no-op when off or already fixed). */
	void applyCelimynTuekFix();
	/** The place a troop stands at (or marches to), -1 if none. */
	int troopPlace(uint id) const;
	static uint16 placeOffset(uint index);
	/**
	 * troop_issue_move_order (seg000:84a6); false when refused. The
	 * prospectors ignore @p dest and march to the head of their queue
	 * (prospector_sync_destination_queue, 848f), false when it is empty.
	 */
	bool issueMoveOrder(uint id, uint dest);
	/**
	 * The prospectors' destinations (ARRAY_PTR_Location_prospector_destinations,
	 * CD ds:11d3, floppy 11e0): three place offsets, 0 ends the list.
	 */
	uint16 prospectorDestination(uint slot) const {
		return slot < 3 ? READ_LE_UINT16(_state.vars + ds(0x11d3) + 2 * slot) : 0;
	}
	void setProspectorDestination(uint slot, uint16 offset) {
		if (slot < 3)
			WRITE_LE_UINT16(_state.vars + ds(0x11d3) + 2 * slot, offset);
	}
	/** seg000:8347: drop the queue's head. */
	void shiftProspectorQueue();
	/** Place index of a place offset (0x100 + 28 i), -1 for none. */
	int placeIndex(uint16 offset) const;
	/**
	 * Would troop @p id at @p place make both halves meet there, the
	 * condition of the north/south quarrel (fremenQuarrel): the spice troops
	 * there (occupation & 0x2f == 0) and the troop itself if it mines, from
	 * the north and the south (troop byte 0x12 bit 7). Motivation is left out
	 * (it falls with time), so the answer is the cautious one.
	 */
	bool wouldQuarrel(uint id, uint place) const;
	/**
	 * Test setup (dune_story_setup=hemispheres): a northern and a southern
	 * Fremen troop hired as spice miners at one sietch away from Paul,
	 * motivation 30; returns false when no such pair exists.
	 */
	bool prepareQuarrelTest(uint &north, uint &south, uint &place);
	/** ESPIONAGE (seg000:6a45): march to the nearest hidden fort within 30 cells. */
	bool startEspionage(uint id);
	/** seg000:5274's distance between two places: max(|dlng| >> 8, |dlat|). */
	uint placeDistance(uint a, uint b) const;
	/** The nearest hidden fortress or palace (the ds:e2/e4 part of seg000:5274). */
	int nearestHiddenHarkonnen(uint from, uint &dist) const;
	/** location_has_battle (seg000:627e). */
	bool placeInBattle(uint index) const;
	/** seg000:33be: forces and balance (ds:94, 96, 9c); returns the balance. */
	byte battleForces(uint index, uint &harkonnen, uint &fremen);
	static byte battleBalance(uint harkonnen, uint fremen);
	uint troopStrength(uint id, bool withPaul = false);
	/** MASSIVE ATTACK (seg000:7317); true when the place is won. */
	bool massiveAttack(uint index);
	/** seg000:83fd: every hired troop at the place attacks. */
	void startAttack(uint index);
	/** seg000:1243: 10 000 men with atomics training at locations 2-4. */
	bool finalAttackReady() const;
	/** ds:46d9: a pending death (4 shot on arrival, 6 killed in battle); cleared by reading. */
	byte takePaulFate() {
		const byte f = _paulFate;
		_paulFate = 0;
		return f;
	}
	/** seg000:6f78 / 6fb0: charisma with the motivation spill. */
	void changeCharisma(int delta);

private:
	bool findTables();
	bool readExecutable(const char *name, Common::Array<byte> &image) const;

	GameState &_state;
	Resource &_resources;
	StartupLog &_log;
	uint _palaceTable, _pointerTable;
	int _shift;
	bool _tablesFound;
	bool _floppy;
	bool _amiga = false;
	bool loadAmigaInitialData();
	const Common::Array<byte> *_segaCdProgram = nullptr;
	bool loadSegaCdData();  ///< segacd_world.cpp
	bool _fixLetoLoop = false;
	bool _fixCelimynTuek = false;
	Common::Array<byte> _tablat;
	uint _harvestRemainder;
	byte _requestedPhase = 0;

	void runPeriod();
	void rollDemand(uint16 &sighting);
	void findSceneScripts(const Common::Array<byte> &image);
	Common::Array<byte> _code;  ///< the executable's image, for the scripted scenes
	int _scriptBase = -1;
	int _scriptDelta = 0;
	bool _emperorEnding = false;
	Common::Array<byte> _map;
	uint16 _ecologyLfsr = 1;
	void raiseSkill(uint id, uint skillClass, byte amount);
	uint16 _rngA = 1, _rngB = 1;
	byte _paulFate = 0;
	byte *troopRecord(uint id);
	void unlinkTroop(uint id);
	uint linkTroop(uint id, uint index);
	void registerEquipment(uint id, uint index, int sign, byte mask = 0xff);
	void removeFromPlay(uint id);
	void applyJob(uint id, byte job);
	/** The north/south quarrel on a new day (floppy sub_9A58 9A95, CD 6e20). */
	void fremenQuarrel(uint id, uint index);

	bool travelSubstep(uint id);
	void travelSubsteps(uint id, uint n);
	void troopTravelStep(uint id);
	void troopArrive(uint id);
	uint battleLoss(uint x, uint id);
	void troopCaptured(uint id);
	void harkonnenStrike(uint id, uint index, uint h);
	bool fremenStrike(uint id, uint index);
	void attackTick(uint id, uint index);
	void afterBattleWonHired(uint index, bool fortress);
	void battleWon(uint index);
	void battleLost(uint index);
	void militaryTraining(uint id, uint index);
	void espionageTick(uint id, uint index);
	void palaceFalls();
	void fortressTaken(uint index);
	void paintArea(uint index, byte stage, uint radius);
	template<typename F> void forDisc(uint16 longitude, int16 latitude, uint radius, int limit, F cell);
	bool loadTablat();
	void mineSpice(uint id, uint locationIndex);
	void prospect(uint id, uint locationIndex);
	void raiseSpiceSkill(uint id, byte amount);
	uint rollRandom(uint range); ///< the executable's rolling random word at ds:0 (a 16-bit LFSR here)
	uint randMasked(uint mask) { return rollRandom(0x10000) & mask; } ///< rand_masked (seg000:e3b7)
	uint16 word(uint cdOffset) const { return READ_LE_UINT16(&_state.vars[ds(cdOffset)]); }
	void setWord(uint cdOffset, uint16 value) { WRITE_LE_UINT16(&_state.vars[ds(cdOffset)], value); }
	byte &troopByte(uint id, uint offset) { return _state.vars[kTroopTable + (id - 1) * kTroopSize + offset]; }
	byte &locationByte(uint index, uint offset) {
		return _state.vars[Location::kTableOffset + index * Location::kRecordSize + offset];
	}
};

/** Unpack an LZEXE 0.91 compressed DOS executable (the floppy's DUNEPRG.EXE). */
bool unpackLzexe(const Common::Array<byte> &packed, Common::Array<byte> &unpacked);

} // namespace Dune

#endif // ENGINES_DUNE_WORLD_H

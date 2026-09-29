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
 * The story systems that run beside the dialogue: the Emperor's spice
 * shipments bargained with Duncan, the COMM-room message list and the
 * vision-message queue. Transcribed from the CD executable (DNCDPRG.EXE
 * 3.7) with capstone (scripts/dune_disasm.py); names from madmoose's
 * dune-chani database. See notes/research/gameplay-rules.md.
 */

#include "dune/world.h"

#include "common/util.h"

#include "dune/debug.h"

namespace Dune {

namespace {

enum {
	kEventDay = 0x118d,   ///< the shipment's day (get_ingame_day units: time >> 4)
	kUnpaid = 0x11bb,     ///< the Emperor was not paid: the consequence comes
	kShipArmed = 0x1158,  ///< 0xffff while an agreed amount waits to be shipped
	kSpentToday = 0x1172,
	kSightingList = 0x1179,
	kVisionQueue = 0x1190,
	kSmugglers = 0x10d8,  ///< six 17-byte records, 0xff after the last
	kSmugglerRecord = 17,
	kLastSmuggler = 0x113f,
	kCurrentSmuggler = 0x10b4,
	kSubstRegion = 0x11f7 ///< string_subst_id_table[6]
};

// The Emperor's reminders by days late (1-3) and the fulfilment class of
// the last shipment (0 generous, 1 close, 2 short): cs:2165, read through
// the xlat at seg000:214f with index days * 4 + class.
const byte kReminders[3][4] = { { 4, 5, 6, 0 }, { 5, 6, 0, 0 }, { 6, 0, 0, 0 } };

} // namespace

// sub_124d2: how the last shipment measured up, 0 (all) .. 5 (nothing).
static byte fulfilmentClass(byte fulfilment) {
	byte c = 0;
	static const byte kLimits[5] = { 1, 0x40, 0x80, 0x90, 0xff };
	for (uint i = 0; i < 5; ++i)
		if (fulfilment < kLimits[i])
			++c;
	return c;
}

uint16 World::armShipments() {
	// sub_12090: the event day is today and the demand is rolled at once.
	setWord(kEventDay, (uint16)(_state.w(GameState::kGameTime) >> 4));
	uint16 sighting = 0;
	rollDemand(sighting);
	return sighting;
}

bool World::shipmentDay(uint16 &sighting) {
	// seg000:20a4, in the executable's order.
	sighting = 0;
	const byte flags = _state.b(kShipmentFlags);
	if (!(flags & 0x80))
		return false;
	const uint16 today = (uint16)(_state.w(GameState::kGameTime) >> 4);
	const uint16 eventDay = word(kEventDay);
	if (_state.b(kShipmentPaused)) {
		if (today != eventDay)
			_state.setB(GameState::kDaysToShipment, (byte)(eventDay - today));
		return false;
	}
	if (flags & 0x10) {
		// A demand is pending: reminders on days 1-3 after, then the end.
		const uint16 late = (uint16)(today - eventDay);
		if (!late)
			return false;
		if (late >= 4) {
			_log.line("Shipments: the demand went unanswered for four days, the Emperor strikes");
			return true;
		}
		const byte c = MIN<byte>(fulfilmentClass(_state.b(kFulfilment)), 2);
		const byte reminder = kReminders[late - 1][c];
		if (!reminder) {
			_log.line(Common::String::format("Shipments: day %u of the demand, class %u: no more reminders, the Emperor strikes", late, c));
			return true;
		}
		sighting = (uint16)((reminder << 8) | 0x0b);
		return false;
	}
	if (var(kUnpaid)) {
		_log.line("Shipments: the Emperor was not paid (ds:11bb), he strikes");
		return true;
	}
	if (today != eventDay) {
		_state.setB(GameState::kDaysToShipment, (byte)(eventDay - today));
		return false;
	}
	rollDemand(sighting);
	return false;
}

void World::rollDemand(uint16 &sighting) {
	// seg000:20d2: the demand grows with each one and with the last shortfall.
	const byte count = _state.b(kShipments);
	_state.setB(kShipments, (byte)(count + 1));
	uint32 demand = 0xffff;
	const uint32 base = (uint32)count * 150 + 100;
	if (base <= 0xffff) {
		const uint32 product = base * (randMasked(0x3f) + 0xe0);
		if (product <= 0xffffff) {
			demand = product >> 8;
			const byte fulfilment = _state.b(kFulfilment);
			if (!(fulfilment & 0x80)) {
				// Below the demand last time: up to twice as much.
				const uint32 scaled = demand * (uint32)(0x100 + (byte)~(byte)(fulfilment << 1));
				demand = (scaled >> 8) > 0xffff ? 0xffff : scaled >> 8;
			}
		}
	}
	_state.setW(kDemand, (uint16)demand);
	_state.setB(GameState::kDaysToShipment, 0);
	_state.vars[kShipmentFlags] |= 0x90;
	sighting = (_state.b(kFulfilment) & 0x80) ? 0x20b : 0x30b;
	_log.line(Common::String::format("Shipments: demand %u kg (demand %u)", demand * 10, count + 1));
}

int World::daysSinceDemand() const {
	return (int)(_state.w(GameState::kGameTime) >> 4) - (int)word(kEventDay);
}

bool World::shipmentReminderDue() const {
	return (_state.b(kShipmentFlags) & 0x10) && !(_state.w(GameState::kPersonsWith) & 8) &&
		   _state.b(kCurrentRoom) != 8 && !_state.b(kShipmentPaused);
}

void World::duncanOffers() {
	// seg000:2239.
	_state.setB(kChoice, 0);
	_state.setW(0x20, 0);
	_state.setB(kArguing, 0);
	const uint16 stock = _state.w(kSpiceStock);
	if (stock)
		_state.setB(kChoice, 3);
	// sub_122b1: the four amounts Duncan can put forward.
	byte &flags = _state.vars[kShipmentFlags];
	flags &= 0xf9;
	const uint16 demand = _state.w(kDemand);
	const uint16 half = (uint16)(demand + (demand >> 1)), twice = (uint16)(demand * 2);
	const uint16 stockHalf = stock >> 1, stockThree = (uint16)((stock >> 2) + (stock >> 1));
	uint16 o[4];
	if (stock < demand) {
		o[0] = stock;
		o[1] = stockThree;
		o[2] = stockHalf;
		o[3] = (uint16)(stockThree - stockHalf);
	} else {
		o[0] = demand;
		o[1] = stock;
		if (stock < half) {
			o[2] = stockThree;
			o[3] = stockHalf;
			flags |= 2;
		} else if (stock < twice) {
			o[2] = stockThree;
			o[3] = half;
			flags |= 4;
		} else {
			o[2] = half;
			o[3] = twice;
			flags |= 6;
		}
	}
	for (uint i = 0; i < 4; ++i)
		_state.setW(kOffers + 2 * i, o[i]);
	// The smuggler with the oldest unpaid bill, else the next one with a
	// bill after the last named (seg000:2253).
	const byte today = (byte)(_state.w(GameState::kGameTime) >> 4);
	uint pick = 0;
	byte oldest = 0;
	for (uint p = kSmugglers; _state.vars[p] != 0xff && p < kSmugglers + 8 * kSmugglerRecord; p += kSmugglerRecord) {
		if (!READ_LE_UINT16(&_state.vars[p + 0x0e]) || (_state.vars[p + 2] & 0x60))
			continue;
		const byte age = (byte)(today - _state.vars[p + 0x10]);
		if (age > oldest) {
			oldest = age;
			pick = p;
		}
	}
	if (!pick) {
		const uint last = READ_LE_UINT16(&_state.vars[kLastSmuggler]);
		uint p = last;
		for (uint guard = 0; guard < 8 && p; ++guard) {
			p += kSmugglerRecord;
			if (p >= GameState::kSize || _state.vars[p] == 0xff)
				p = kSmugglers;
			if (READ_LE_UINT16(&_state.vars[p + 0x0e])) {
				pick = p;
				WRITE_LE_UINT16(&_state.vars[kLastSmuggler], (uint16)p);
				break;
			}
			if (p == last)
				break;
		}
	}
	if (pick) {
		// sub_1235f: stage the smuggler for the conditions.
		WRITE_LE_UINT16(&_state.vars[kCurrentSmuggler], (uint16)pick);
		_state.setB(0x1c, _state.vars[pick + 2]);
		const uint16 bill = READ_LE_UINT16(&_state.vars[pick + 0x0e]);
		_state.setW(0x20, bill);
		_state.setB(0x1f, bill ? (byte)(today - _state.vars[pick + 0x10]) : 0);
		_state.setB(0x1d, _state.vars[pick + 1]);
		setWord(kSubstRegion, _state.vars[pick]);
	}
	_log.line(Common::String::format("Shipments: Duncan offers %u/%u/%u/%u (demand %u, stock %u)", o[0], o[1], o[2], o[3],
			demand, stock));
}

void World::bargainChoice(byte choice) {
	// seg000:241a ACCEPT (1), 2432 REFUSE (2), 2453 ARGUE (3) for Duncan.
	_state.setB(kChoice, choice);
	_state.setB(kArguing, (byte)(_state.b(kArguing) + 1));
}

bool World::smugglerOffer() {
	// seg000:23a5-23d4, after event 8's phase, roll and date: the next item
	// after the current word that the smugglers still have, at most two
	// times round the goods on sale (ds:1141); its price is ds:9d.
	const uint16 p = READ_LE_UINT16(&_state.vars[kCurrentSmuggler]);
	if (p < kSmugglers || p + kSmugglerRecord > GameState::kSize)
		return false;
	const byte goods = var(0x1141);
	const uint16 names = _state.nameTable;
	byte item = (byte)(_state.w(names + 6) - _itemWords);
	uint turns = 2;
	for (;;) {
		++item;
		while (item >= goods) {
			item = (byte)(item - goods);
			if (--turns == 0 || !goods) {
				_log.line("Smugglers: nothing to trade");
				return false;
			}
		}
		if (_state.vars[p + 4 + item])
			break;
	}
	_state.setW(names + 6, (uint16)(_itemWords + item));
	_state.setB(0x9d, (byte)((_state.vars[p + 9 + item] & 0x7f) << 1));
	_log.line(Common::String::format("Smugglers: offer item %u for %u kg (%u in stock)", item, _state.b(0x9d) * 10,
			_state.vars[p + 4 + item]));
	return true;
}

void World::smugglerBuy(uint p) {
	// seg000:23e6: the price goes on the bill, the item into the stock of
	// the place Paul is at (ds:114e, +0x14 + item): someone must fetch it.
	_state.vars[p + 2] &= 0x9f;
	const byte price = _state.b(0x9d);
	_state.setB(0x9d, 0);
	_state.setW(0x20, (uint16)(_state.w(0x20) + price));
	const uint16 bill = (uint16)(READ_LE_UINT16(&_state.vars[p + 0x0e]) + price);
	WRITE_LE_UINT16(&_state.vars[p + 0x0e], bill);
	if (bill == price)
		++_state.vars[0x22];
	_state.vars[p + 0x10] = (byte)(_state.w(GameState::kGameTime) >> 4);
	const byte item = (byte)(_state.w(_state.nameTable + 6) - _itemWords);
	if (item < 5)
		--_state.vars[p + 4 + item];
	const uint place = currentLocation();
	uint count = 0;
	if (place < locationCount() && item < 7) {
		byte &c = _state.vars[Location::kTableOffset + place * Location::kRecordSize + 0x14 + item];
		count = ++c;
	}
	_log.line(Common::String::format("Smugglers: bought item %u for %u kg, bill %u kg, place %u count %u", item,
			price * 10, bill * 10, place, count));
}

void World::smugglerChoice(byte choice) {
	// seg000:241a ACCEPT, 2432 REFUSE, 2453 ARGUE with speaker 13; then the
	// common tail 2496: ds:9f, ds:1a + 1, and the talk goes on.
	const uint16 p = READ_LE_UINT16(&_state.vars[kCurrentSmuggler]);
	const bool valid = p >= kSmugglers && p + kSmugglerRecord <= GameState::kSize;
	if (valid && choice == 1) {
		smugglerBuy(p);
	} else if (valid && choice == 2) {
		if (!lcgRandMasked(7)) {
			_state.setB(0x9e, (byte)(_state.b(0x9e) | 0x10)); // "Eh! ... I'm losing money!"
			choice = 3;
			_log.line("Smugglers: REFUSE, losing money");
		} else {
			_state.setB(0x9d, 0); // he looks for something else
			_log.line("Smugglers: REFUSE");
		}
	} else if (valid && choice == 3) {
		const uint16 r = lcgRandMasked(3);
		if (!r) {
			_state.setB(0x9e, (byte)(_state.b(0x9e) | 0x10));
			_log.line("Smugglers: ARGUE, losing money");
		} else {
			_state.setB(0x9e, (byte)((_state.b(0x9e) + 1) & 3));
			const byte a = (byte)((r & 1) + _state.b(kArguing));
			if (a >= _state.vars[p + 1]) {
				_state.setB(0x9d, 0); // "Forget it!" (or "I never haggle")
				_log.line("Smugglers: ARGUE, forget it");
			} else {
				const byte price = _state.b(0x9d);
				_state.setB(0x9d, (byte)(price - (price >> 3))); // 23d5
				_log.line(Common::String::format("Smugglers: ARGUE, cut to %u kg", _state.b(0x9d) * 10));
			}
		}
	}
	_state.setB(kChoice, choice);
	_state.setB(kArguing, (byte)(_state.b(kArguing) + 1));
}

void World::smugglerRestock() {
	// seg000:1ca5: one timer-random word for the day, two bits per refill;
	// a visited record's sold-out goods whose price has bit 7 get 0-3.
	uint16 r = (uint16)rollRandom(0);
	uint refills = 0;
	for (uint p = kSmugglers; p + kSmugglerRecord <= GameState::kSize && _state.vars[p] < 0x14; p += kSmugglerRecord) {
		if (!(_state.vars[p + 2] & 8))
			continue;
		for (int i = 4; i >= 0; --i) {
			if (_state.vars[p + 4 + i] || !(_state.vars[p + 9 + i] & 0x80))
				continue;
			r = (uint16)((r << 2) | (r >> 14));
			_state.vars[p + 4 + i] = (byte)(r & 3);
			refills += _state.vars[p + 4 + i];
		}
	}
	if (refills)
		_log.line(Common::String::format("Smugglers: restocked %u item(s)", refills));
}

void World::duncanAccept(bool smugglerBill) {
	// seg000:24ee: an accepted offer is agreed; the shipment waits (ds:1158).
	const byte choice = _state.b(kChoice);
	if (smugglerBill) {
		// The menu of action 5 (ds:476d = 1) was about the bill of the
		// smuggler staged at ds:10b4 (2239).
		const uint16 p = READ_LE_UINT16(&_state.vars[kCurrentSmuggler]);
		if (p < kSmugglers || p + kSmugglerRecord > GameState::kSize)
			return;
		if (choice < 2) {
			// 2517: the whole bill leaves the stock and counts as spent today.
			const uint16 bill = READ_LE_UINT16(&_state.vars[p + 0x0e]);
			WRITE_LE_UINT16(&_state.vars[p + 0x0e], 0);
			--_state.vars[0x22];
			_state.setW(kSpiceStock, (uint16)(_state.w(kSpiceStock) - bill));
			setWord(kSpentToday, (uint16)(word(kSpentToday) + bill));
			_log.line(Common::String::format("Smugglers: bill paid, %u kg (stock %u kg)", bill * 10,
					_state.w(kSpiceStock) * 10));
		} else {
			// 2541 REFUSE: 0x40 ("are you still refusing to pay"); 252d ARGUE: 0x20.
			_state.vars[p + 2] = (byte)((_state.vars[p + 2] & 0x9f) | (choice == 2 ? 0x40 : 0x20));
			_log.line(Common::String::format("Smugglers: bill %s (flags %#x)", choice == 2 ? "refused" : "put off",
					_state.vars[p + 2]));
		}
		return;
	}
	if (choice >= 2)
		return;
	const uint index = (uint)(_state.b(kArguing) - 1) & 3;
	_state.setW(kAgreed, _state.w(kOffers + 2 * index));
	setWord(kShipArmed, 0xffff);
	_log.line(Common::String::format("Shipments: agreed on %u kg", _state.w(kAgreed) * 10));
}

uint16 World::duncanClosing(bool &endTalk) {
	// seg000:24a3.
	endTalk = false;
	if (_state.b(GameState::kPhase) < 0x10) {
		_state.vars[0xff7] |= 0x10;
		return 0;
	}
	endTalk = true;
	_state.setW(kAgreed, 0);
	_state.vars[kShipmentFlags] |= 1;
	const byte c = fulfilmentClass(_state.b(kFulfilment));
	if (c + 7 == 0x0c)
		++var(kUnpaid);
	return (uint16)(((c + 7) << 8) | 0x0b);
}

bool World::shipmentReady() const {
	return _state.b(kCurrentRoom) == 8 && (_state.w(kAgreed) & word(kShipArmed)) &&
		   (_state.w(GameState::kPersonsInRoom) & 8);
}

void World::shipSpice(uint16 amount) {
	// sub_12566 (the animation is the host's).
	const uint16 stock = _state.w(kSpiceStock);
	amount = MIN(amount, stock);
	_state.setW(kSpiceStock, (uint16)(stock - amount));
	setWord(kSpentToday, (uint16)(word(kSpentToday) + amount));
	const uint16 demand = MAX<uint16>(_state.w(kDemand), 1);
	uint32 ratio = ((uint32)amount << 8) / demand;
	if (ratio >= 0x200)
		ratio = 0x1ff;
	byte fulfilment = (byte)(ratio >> 1);
	if (!fulfilment)
		fulfilment = 1;
	_state.setB(kFulfilment, fulfilment);
	byte flags = 0x40;
	uint days = 7;
	if (fulfilment < 0xc0) {
		--days;
		if (fulfilment <= 0x80) {
			--days;
			flags = 0;
			if (fulfilment != 0x80) {
				--days;
				flags = 8;
				if (_state.b(kShipmentFlags) & 8)
					_state.setB(kFulfilment, 0); // short twice running
			}
		}
	}
	_state.setB(kShipmentFlags, (byte)(flags | 0x80));
	uint16 eventDay = (uint16)(word(kEventDay) + days);
	eventDay = (uint16)(eventDay + randMasked((_state.b(kShipments) >> 1) & 3));
	setWord(kEventDay, eventDay);
	_state.setB(GameState::kDaysToShipment, (byte)(eventDay - (_state.w(GameState::kGameTime) >> 4)));
	setWord(kShipArmed, 0);
	_log.line(Common::String::format("Shipments: shipped %u kg (%u%% of the demand), next in %u days", amount * 10,
			fulfilment * 100 / 128, _state.b(GameState::kDaysToShipment)));
}

void World::addSighting(uint16 sighting) {
	byte &count = _state.vars[kSightings];
	for (uint i = 0; i < count && i < 10; ++i)
		if (word(kSightingList + 2 * i) == sighting)
			return;
	if (count >= 10)
		dropOldestSighting();
	setWord(kSightingList + 2 * count, sighting);
	++count;
	++_state.vars[kUnread];
	if (_state.b(GameState::kPhase) >= 0x38 && _state.b(kCurrentRoom) != 8)
		queueVision(0x201);
	_log.line(Common::String::format("COMM: message from person %u (variant %u)", sighting & 0x3f, sighting >> 8));
}

void World::dropOldestSighting() {
	byte &count = _state.vars[kSightings];
	if (!count)
		return;
	--count;
	for (uint i = 0; i < 9; ++i)
		setWord(kSightingList + 2 * i, word(kSightingList + 2 * i + 2));
	setWord(kSightingList + 18, 0);
}

uint World::sightingCount() const {
	return MIN<uint>(_state.b(kSightings), 10);
}

uint16 World::sighting(uint index) const {
	return word(kSightingList + 2 * index);
}

byte World::viewSighting(uint index, byte &variant) {
	uint16 s = sighting(index);
	variant = (byte)(s >> 8);
	const byte person = s & 0x3f;
	if (!(s & 0x80)) {
		// A new one: seen now (seg000:293e).
		setWord(kSightingList + 2 * index, (uint16)(s | 0x80));
		if (_state.b(kUnread))
			--_state.vars[kUnread];
		if (person == 0x0b && (byte)(variant - 2) < 2)
			_state.vars[kShipmentFlags] |= 0x20; // the demand was read
	}
	return person;
}

void World::queueVision(uint16 id, uint16 location) {
	// seg000:29f0: nothing until Paul has had his first vision.
	if (!(_state.b(kPaulEvents) & 1))
		return;
	byte &count = var(kVisionQueue);
	for (uint i = 0; i < count; ++i)
		if (word(kVisionQueue + 1 + 4 * i) == id && word(kVisionQueue + 3 + 4 * i) == location)
			return;
	if (count >= 10)
		dequeueVision();
	setWord(kVisionQueue + 1 + 4 * count, id);
	setWord(kVisionQueue + 3 + 4 * count, location);
	++count;
	_log.line(Common::String::format("Vision: queued %#x", id));
}

uint World::visionCount() const {
	return MIN<uint>(_state.vars[ds(kVisionQueue)], 10);
}

void World::vision(uint index, uint16 &id, uint16 &location) const {
	id = word(kVisionQueue + 1 + 4 * index);
	location = word(kVisionQueue + 3 + 4 * index);
}

void World::dequeueVision() {
	byte &count = var(kVisionQueue);
	if (!count)
		return;
	--count;
	for (uint i = 0; i < 9; ++i) {
		setWord(kVisionQueue + 1 + 4 * i, word(kVisionQueue + 5 + 4 * i));
		setWord(kVisionQueue + 3 + 4 * i, word(kVisionQueue + 7 + 4 * i));
	}
	setWord(kVisionQueue + 37, 0);
	setWord(kVisionQueue + 39, 0);
}

void World::purgeVisions(byte sender, uint16 location) {
	// seg000:2a51.
	byte &count = var(kVisionQueue);
	uint kept = 0;
	for (uint i = 0; i < count; ++i) {
		const uint16 id = word(kVisionQueue + 1 + 4 * i), where = word(kVisionQueue + 3 + 4 * i);
		if ((id >> 8) == sender && (sender != 0x0f || where == location))
			continue;
		setWord(kVisionQueue + 1 + 4 * kept, id);
		setWord(kVisionQueue + 3 + 4 * kept, where);
		++kept;
	}
	for (uint i = kept; i < count; ++i) {
		setWord(kVisionQueue + 1 + 4 * i, 0);
		setWord(kVisionQueue + 3 + 4 * i, 0);
	}
	count = (byte)kept;
}

void World::purgeArrivalVisions() {
	// seg000:2a7f: the arrival notices go once the messages are read.
	byte &count = var(kVisionQueue);
	uint kept = 0;
	for (uint i = 0; i < count; ++i) {
		const uint16 id = word(kVisionQueue + 1 + 4 * i), where = word(kVisionQueue + 3 + 4 * i);
		if ((id & 0xff) == 1)
			continue;
		setWord(kVisionQueue + 1 + 4 * kept, id);
		setWord(kVisionQueue + 3 + 4 * kept, where);
		++kept;
	}
	for (uint i = kept; i < count; ++i) {
		setWord(kVisionQueue + 1 + 4 * i, 0);
		setWord(kVisionQueue + 3 + 4 * i, 0);
	}
	count = (byte)kept;
}

// sub_15133: the heading from (lng0, lat0) to (lng1, lat1), 0..255 with 0
// east... as the executable measures it (0x40 per quarter turn).
static byte heading(int lng0, int lat0, int lng1, int lat1) {
	int bx = lat1 - lat0; // the executable's bx: latitude difference
	int dx = (int16)(uint16)(lng1 - lng0); // 16-bit longitudes wrap round the planet
	if (bx < -0x80 || bx >= 0x80) {
		bx >>= 1;
		dx >>= 1;
	}
	bx = (int16)(bx << 8);
	dx = (int16)dx;
	const int ax = ABS(bx), cx = ABS(dx);
	if (cx >= ax) {
		if (cx < 1)
			return 0;
		const int q = (0x20 * bx) / dx;
		return (byte)(q + (dx < 0 ? 0xc0 : 0x40));
	}
	if (ax < 1)
		return 0;
	int q = (0x20 * dx) / bx;
	if (bx >= 0)
		q -= 0x80;
	return (byte)(-q);
}

void World::stageLocationForConditions(uint index) {
	if (index >= locationCount())
		return;
	const uint ptr = Location::kTableOffset + index * Location::kRecordSize;
	const byte *l = _state.vars + ptr;
	setWord(0x11ce, (uint16)ptr);
	_state.setW(0x4e, (uint16)((l[0] << 8) | l[1]));
	_state.setB(0x50, var(0x1141 + l[1]));
	_state.setB(0x51, l[10]);
	_state.setB(0x52, l[18]);
	_state.setB(0x54, l[27]);
	_state.setB(0x4d, l[8]);
	for (uint k = 0; k < 7; ++k)
		_state.setB(0x55 + k, l[20 + k]);

	// seg000:33be: the forces here: Harkonnen (ds:94) and Fremen (ds:96),
	// each troop's strength (seg000:342d), the Fremen bitfields ORed.
	Common::Array<uint> ids;
	troopsAt(index, ids);
	uint harkonnen = 0, fremen = 0;
	uint16 bits10 = 0, bits12 = 0;
	for (uint i = 0; i < ids.size(); ++i) {
		const byte *t = _state.vars + kTroopTable + (ids[i] - 1) * kTroopSize;
		if (t[3] & 0x20)
			continue;
		// seg000:342d: (2 x motivation + army skill) x men >> 4, doubled up per
		// weapon from the laser guns on (equipment bits 5..2), >> 8, at least 1.
		uint v = MIN<uint>(255, 2 * motivationModifier(ids[i]) + t[0x17]);
		uint32 ax = (v * t[26]) >> 4, dx = ax;
		byte eq = (byte)(t[25] << 2);
		bool overflow = false;
		for (uint k = 0; k < 4 && !overflow; ++k) {
			dx <<= 1;
			const bool has = eq & 0x80;
			eq <<= 1;
			if (has) {
				ax += dx;
				if (ax > 0xffff)
					overflow = true;
			}
		}
		uint strength = overflow ? 0xff : (ax >> 8) & 0xff;
		if (!strength && t[26] >= 1)
			strength = 1;
		if (t[16] & 0x80) {
			harkonnen += strength;
		} else {
			fremen += strength;
			bits10 |= READ_LE_UINT16(t + 0x10);
			bits12 |= READ_LE_UINT16(t + 0x12);
		}
	}
	_state.setW(0x94, (uint16)harkonnen);
	_state.setW(0x96, (uint16)fremen);
	_state.setW(0x5c, bits10);
	_state.setW(0x5e, bits12);
	{
		// seg000:33d9: the balance, signed, +-0xfc.
		uint big = fremen, small = harkonnen;
		const bool fremenAhead = fremen >= harkonnen;
		if (!fremenAhead)
			SWAP(big, small);
		uint r = 0xfc;
		if (small && (big >> 8) < small)
			r = MIN<uint>(0xfc, ((big << 8) / small) >> 1);
		_state.setB(0x9c, (byte)(fremenAhead ? r : (uint)(-(int)r)));
	}

	// seg000:34a5: the troop counts (ds:60-92).
	for (uint k = 0x60; k < 0x93; ++k)
		_state.setB(k, 0);
	for (uint i = 0; i < ids.size(); ++i) {
		const byte *t = _state.vars + kTroopTable + (ids[i] - 1) * kTroopSize;
		const byte occ = t[3];
		if (occ & 0x20)
			continue;
		// dx the block (0x61 settled, 0x7f moving); the side's counter is dx,
		// or dx - 1 for the Fremen; the job counters stay at dx + 1 + job for
		// both sides (seg000:34d9-3504), so ds:66 counts military training.
		const uint dx = (occ & 0x40) ? 0x7f : 0x61;
		uint base = dx;
		if (!(t[16] & 0x80)) {
			--base;
			if (occ == 0x80) {
				_state.setB(0x90, (byte)(_state.b(0x90) + 1));
				continue;
			}
		}
		_state.setB(base, (byte)(_state.b(base) + 1));
		byte job = occ & 0x0f;
		if ((occ & 3) == 3)
			job &= 0xfc;
		const uint slot = dx + job + 1;
		if (slot < 0x93)
			_state.setB(slot, (byte)(_state.b(slot) + 1));
		if (dx + job < 0x7f) {
			const uint region = 0x71 + (t[0x12] & 0x0f);
			_state.setB(region, (byte)(_state.b(region) + 1));
		}
	}
	_state.setB(0x91, (byte)(_state.b(0x60) + _state.b(0x7e)));
	_state.setB(0x92, (byte)(_state.b(0x61) + _state.b(0x7f)));

	// seg000:3360: the free equipment as a mask (bit 7 harvesters .. bit 1 bulbs).
	byte free[7];
	placeFreeEquipment(index, free);
	byte mask = 0;
	for (uint k = 0; k < 7; ++k)
		if (free[k])
			mask |= (byte)(0x80 >> k);
	_state.setB(0x53, mask);

	// seg000:3385: Gurney, Stilgar and Chani here (their records' place word
	// is (index + 1) << 8 | 0x80), unless this is where Paul stands.
	_state.setB(0xf7, 0);
	if (index != currentLocation()) {
		const uint16 here = (uint16)(((index + 1) << 8) | 0x80);
		static const uint kRecords[3] = { 0x1018, 0x1028, 0x1048 };
		for (uint k = 0; k < 3; ++k)
			if (READ_LE_UINT16(&_state.vars[kRecords[k] + 2]) == here)
				_state.setB(0xf7, (byte)(_state.b(0xf7) | (1 << (_state.vars[kRecords[k] + 0xe] & 7))));
	}

	// seg000:5274: the nearest place (ds:ca/cc), known sietch (d0/d2), hidden
	// sietch the story lets be found (d6/d8), held fortress (dc/de) and
	// hidden fortress (e2/e4), by max(|dlng| >> 8, |dlat|); then each one's
	// compass octant in the byte after its pointer, and ds:11fd the
	// compass-point phrase (0xda + octant) of the findable sietch.
	static const uint kSlots[5] = { 0xca, 0xd0, 0xd6, 0xdc, 0xe2 };
	for (uint k = 0; k < 5; ++k) {
		_state.setW(kSlots[k], 0xffff);
		_state.setW(kSlots[k] + 2, 0);
	}
	const int lng0 = READ_LE_UINT16(l + 2), lat0 = (int16)READ_LE_UINT16(l + 4);
	for (uint i = 0; i < locationCount(); ++i) {
		if (i == index)
			continue;
		const byte *o = _state.vars + Location::kTableOffset + i * Location::kRecordSize;
		const uint dlng = (uint)ABS((int)READ_LE_UINT16(o + 2) - lng0) >> 8;
		const uint dlat = (uint)ABS((int)(int16)READ_LE_UINT16(o + 4) - lat0);
		const uint d = MAX(dlng, dlat);
		uint slot;
		if (o[8] >= 0x28)
			slot = (o[10] & 0x80) ? 0xe2 : 0xdc;
		else if (!(o[10] & 0x80))
			slot = 0xd0;
		else if (_state.b(GameState::kPhase) >= o[11])
			slot = 0xd6;
		else
			continue;
		const uint16 p = (uint16)(Location::kTableOffset + i * Location::kRecordSize);
		if (d < _state.w(slot)) {
			_state.setW(slot, (uint16)d);
			_state.setW(slot + 2, p);
		}
		if (d < _state.w(0xca)) {
			_state.setW(0xca, (uint16)d);
			_state.setW(0xcc, p);
		}
	}
	for (uint k = 0; k < 5; ++k) {
		const uint16 p = _state.w(kSlots[k] + 2);
		if (p < Location::kTableOffset)
			continue;
		const byte *o = _state.vars + p;
		const byte h = heading(lng0, lat0, READ_LE_UINT16(o + 2), (int16)READ_LE_UINT16(o + 4));
		const byte octant = (byte)((((byte)(h + 0x10)) >> 5) & 7);
		_state.setB(kSlots[k] + 4, octant);
		if (kSlots[k] == 0xd6)
			setWord(0x11fd, (uint16)(0xda + octant));
	}
}

int World::addCompanion(uint character) {
	const byte id = _state.vars[kCharacterTable + character * kCharacterSize + 14];
	byte &a = var(0x1152), &b = var(0x1153);
	if (a == id || b == id)
		return -1;
	if (a == 0xff) {
		a = id;
		return -1;
	}
	if (b == 0xff) {
		b = id;
		return -1;
	}
	// Both taken: the first goes home (its ds:23 = id + 0x64 line, seg000:969e).
	const int dismissed = a;
	a = b;
	b = id;
	return dismissed;
}

void World::removeCompanion(uint character) {
	const byte id = _state.vars[kCharacterTable + character * kCharacterSize + 14];
	byte &a = var(0x1152), &b = var(0x1153);
	if (b == id) {
		b = 0xff;
	} else if (a == id) {
		a = b;
		b = 0xff;
	}
}

uint World::stilgarWaterOfLife() {
	_state.vars[kPaulEvents] |= 8;
	if (_state.b(kChoice) != 1)
		return 0;
	if (_state.b(kCharisma) < 0x64) {
		_log.line("Story: Paul drinks the Water of Life and dies");
		return 2;
	}
	_state.vars[kPaulEvents] |= 2;
	_state.setB(0xd5, 0xff);
	advanceTime(3);
	_log.line("Story: Paul drinks the Water of Life");
	return 1;
}

void World::finalAttackTroops(Common::Array<uint> &ids) {
	// seg000:2d2c / 2d62: the hired troops with atomics training at the
	// three places by the palace (locations 2-4) march on it.
	ids.clear();
	_state.setB(kShipmentPaused, (byte)(_state.b(kShipmentPaused) + 1));
	for (uint index = 2; index <= 4; ++index) {
		Common::Array<uint> here;
		troopsAt(index, here);
		for (uint i = 0; i < here.size(); ++i) {
			const byte *r = _state.vars + kTroopTable + (here[i] - 1) * kTroopSize;
			if (!(r[16] & 0x80) && r[3] == Troop::kMilitaryTraining && (r[25] & 4))
				ids.push_back(here[i]);
		}
	}
	for (uint i = 0; i < ids.size(); ++i)
		if (issueMoveOrder(ids[i], 1))
			troopTravelStep(ids[i]);
	_log.line(Common::String::format("Story: the final attack, %u troop(s) with atomics march on the palace", ids.size()));
}

void World::firstVision() {
	// sub_11071: phase 0x15; Leto sets off with his flag, Gurney's record
	// (0x1018) moves, Harah's (0xfe8) room becomes 10, ds:d5 = 0xff; the
	// shipments begin; Paul has had his vision; vision message 1 is queued.
	_state.setB(0xff, 0);
	_state.setB(GameState::kPhase, 0x15);
	_state.vars[0xfdb] = 1;
	WRITE_LE_UINT16(&_state.vars[0x1018], 0x200b);
	WRITE_LE_UINT16(&_state.vars[0x101a], 0x180);
	_state.vars[0xfe8] = 0x0a;
	_state.setB(0xd5, 0xff);
	const uint16 sighting = armShipments();
	if (sighting)
		addSighting(sighting);
	_state.vars[kPaulEvents] |= 1;
	queueVision(1);
	_log.line("Story: Paul's first vision (phase 0x15)");
}

} // namespace Dune

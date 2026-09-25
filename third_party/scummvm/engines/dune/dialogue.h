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

#ifndef ENGINES_DUNE_DIALOGUE_H
#define ENGINES_DUNE_DIALOGUE_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Dune {

class Resource;
class SentenceBank;
class StartupLog;

/**
 * The game's variables: the executable's data segment as the original saves
 * it (4705 bytes, the last block of a DUNE*.SAV file). The layout follows
 * madmoose's dune-rust (crates/savegame/src/data.rs) and the CD executable.
 * CONDIT expressions read it by byte offset, so it is kept as one block
 * with named offsets rather than as separate fields.
 */
struct GameState {
	enum {
		kSize = 0x1500,      ///< the saved block (0x1261, 0x126e on floppy) plus the constant tables after it
		kSavedSizeCd = 0x1261
	};

	enum Offset {
		kRandomBits = 0x00,
		kGameTime = 0x02,
		kLocationAndRoom = 0x04,
		kPersonsMet = 0x0e,       ///< one bit per character: talked to at least once
		kPersonsWith = 0x10,      ///< travelling with Paul
		kPersonsInRoom = 0x12,
		kPersonsTalkingTo = 0x14,
		kSietchesAvailable = 0x27,
		kFremenTroops = 0x28,     ///< troops working for the Atreides ("\x91(" in the phrases)
		kCharisma = 0x29,
		kPhase = 0x2a,            ///< story phase; dialogue actions 11 and 12 advance it
		kDaysToShipment = 0xcf,
		kHeadIndex = 0xe8,
		kNameTable = 0x11eb       ///< 16 sentence ids used by text codes 0x81-0x8F
	};

	GameState() { newGame(); }
	void newGame();

	/**
	 * Lines said in conversations and flagged for the book, as the
	 * executable records them (character << 11 | entry index); the book
	 * shows them under their topic with the speaker's header.
	 */
	Common::Array<uint16> notebook;
	/** Where this release keeps the name table (0x11EB on the CD, 13 bytes later on the floppy; World sets it). */
	uint16 nameTable = kNameTable;

	byte b(uint offset) const { return offset < kSize ? vars[offset] : 0; }
	uint16 w(uint offset) const;
	void setB(uint offset, byte value) {
		if (offset < kSize)
			vars[offset] = value;
	}
	void setW(uint offset, uint16 value);

	byte vars[kSize];
};

/**
 * CONDIT.HSQ: the expressions dialogue entries are gated on. An expression
 * is an operand followed by (operator, operand) pairs, ending in 0xFF;
 * operators with bit 7 set bind tighter and are resolved last, as in the
 * executable's sub_1A396. Operands: 0x01 n byte variable, 0x00-0x7F n word
 * variable, 0x80 v byte constant, 0x81+ lo hi word constant.
 */
class Conditions {
public:
	bool load(Resource &resources, StartupLog &log);
	bool loaded() const { return !_data.empty(); }
	uint count() const;
	/** Evaluate expression @p index (1-based like the executable; 0 is always true). */
	bool evaluate(uint index, const GameState &state) const;

private:
	bool operand(uint &position, const GameState &state, uint16 &value) const;
	static uint16 apply(byte op, uint16 left, uint16 right);

	Common::Array<byte> _data;
};

/**
 * DIALOGUE.HSQ: 17 characters x 8 lists of four-byte entries, each list
 * ending in 0xFFFF.
 *
 *   byte 0: bit 7 said, bit 6 repeatable, bits 4-5 group mask, bits 0-3 action
 *   byte 1 + top two bits of byte 2: condition number
 *   bits 2-5 of byte 2: flags (bits 2-3: the line goes into the notebook)
 *   low two bits of byte 2 + byte 3: sentence number, 1-based
 *
 * The table is mutated at run time (the said flags), which is why the
 * original saves it with the game.
 */
class Dialogue {
public:
	enum {
		kCharacters = 17,
		kListsPerCharacter = 8
	};

	struct Entry {
		uint offset;
		byte flags, flags2;
		uint condition;
		uint sentence; ///< 0-based sentence number in the phrase file

		bool said() const { return (flags & 0x80) != 0; }
		bool repeatable() const { return (flags & 0x40) != 0; }
		byte action() const { return flags & 0x0f; }
	};

	bool load(Resource &resources, StartupLog &log);
	bool loaded() const { return !_data.empty(); }
	/** Data offset of the list, or 0 when it does not exist. */
	uint listOffset(uint character, uint list) const;
	/** @return false at the 0xFFFF terminator or outside the data. */
	bool entryAt(uint offset, Entry &entry) const;
	void markSaid(uint offset);
	/** The whole table, said flags included (saved with the game). */
	const Common::Array<byte> &data() const { return _data; }
	void setData(const Common::Array<byte> &data);
	/** Entries from the sixth character on use PHRASEx2; the word at 0x60 is the split. */
	bool secondPhraseFile(uint offset) const { return offset >= _split; }

private:
	Common::Array<byte> _data;
	uint _split;
};

/**
 * One conversation, driven as the executable's sub_19F9E and sub_1A03F
 * drive it: walk the character's list for the first entry whose condition
 * holds, show its sentence page by page, then apply its action and mark it
 * said; when a list is exhausted continue with the next one while the list
 * number is not a multiple of four.
 */
class Conversation {
public:
	Conversation(SentenceBank &sentences, Dialogue &dialogue, Conditions &conditions, GameState &state,
				 StartupLog &log);

	/**
	 * Start with @p list. TALK TO ME walks lists 0-3 (loc_194A5); the verbs
	 * present one list only (COME WITH ME / WORK FOR ME list 5, STAY HERE 6,
	 * a troop chief's contact lines 2): @p oneList.
	 */
	void start(uint character, uint list = 0, byte mask = 0x80, bool oneList = false, bool single = false);
	bool active() const { return _active; }
	uint character() const { return _character; }
	/** The next page of text; false when the conversation is over. */
	bool next(Common::String &page, bool &newSentence);
	void stop() { _active = false; _paused = false; }
	/**
	 * Events 4 and 5 (seg000:a244, a248) open the bargaining menu (ds:1ffe:
	 * ARGUE, ACCEPT, REFUSE, WHAT ?) after their line: the conversation
	 * waits until the host resumes it with the choice made.
	 */
	bool paused() const { return _paused; }
	void resume() { _paused = false; }
	/** 0 Duncan and the Emperor's shipments, 1 a smuggler (ds:476d). */
	byte bargainParty() const { return _bargainParty; }
	/** Whether @p character's list @p list has a line to say now (no side effects). */
	bool hasLine(uint character, uint list, byte mask) const;
	/** sub_1a1e8: the conversation ends after the current line. */
	void endAfterLine() { _endAfter = true; }

	/**
	 * The story side of the spoken lines' events (sub_1A03F's table at
	 * cs:a107) that the conversation cannot carry out by itself: 3
	 * cutscenes, 11 phase + 1, 12 the next chapter. The host sets it.
	 */
	typedef void (*EventHandler)(void *context, byte event, bool wasSaid, uint speaker);
	void setEventHandler(EventHandler handler, void *context) {
		_eventHandler = handler;
		_eventContext = context;
	}
	/**
	 * The dialogue-interrupt gate (ds:47a5): a verb arms it before its line;
	 * a line with event 2 (the refusal, "stay here") drops it, event 1 keeps
	 * it. The verb succeeds when it is still armed afterwards.
	 */
	void armGate() { _gate = 0xff; }
	bool gateHeld() const { return _gate == 0xff; }
	/** Apply the line on screen's action now (a verb's answer, seg000:95e2-95f7). */
	void finishPending();

private:
	bool findEntry();
	void finishEntry();
	void applyAction(const Dialogue::Entry &entry, bool wasSaid);

	SentenceBank &_sentences;
	Dialogue &_dialogue;
	Conditions &_conditions;
	GameState &_state;
	StartupLog &_log;

	bool _active;
	uint _character, _list, _searchOffset;
	Dialogue::Entry _current;
	bool _pendingFinish, _endAfter, _oneList;
	bool _single = false, _paused = false, _answered = false;
	byte _bargainParty = 0;
	byte _gate = 0;
	EventHandler _eventHandler = nullptr;
	void *_eventContext = nullptr;
	byte _mask;
	Common::Array<Common::String> _pages;
	uint _pageIndex;
};

} // namespace Dune

#endif // ENGINES_DUNE_DIALOGUE_H

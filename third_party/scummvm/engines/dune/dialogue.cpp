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
 * The dialogue engine, transcribed from the CD executable's routines in
 * OpenRakis' annotated DNCDPRG_RECENT.ASM: sub_1A396 (condition evaluation,
 * also decoded by madmoose's dune-rust crates/condit), sub_19F9E (find and
 * show the next line), sub_1A03F (actions and the said flag) and the driver
 * at loc_19472. The floppy executable behaves the same with the same data
 * files.
 */

#include "dune/dialogue.h"

#include "common/endian.h"

#include "dune/debug.h"
#include "dune/resource.h"
#include "dune/text.h"

namespace Dune {

// ---- GameState ---------------------------------------------------------------

void GameState::newGame() {
	memset(vars, 0, sizeof(vars));
	notebook.clear();
	// The game opens in the throne room with Duke Leto present; everything
	// else starts at zero like a fresh data segment. Phase 0 selects Leto's
	// opening lines.
	setW(kPersonsInRoom, 1);
	// Byte 0xFC is the condition of lines that must always be available:
	// each character's self-introduction, the "Nothing to say" fallbacks and
	// the book's first two paragraphs. The original sets it before the game
	// starts; it is read here as a constant true.
	setB(0xfc, 1);
}

uint16 GameState::w(uint offset) const {
	return offset + 1 < kSize ? READ_LE_UINT16(vars + offset) : 0;
}

void GameState::setW(uint offset, uint16 value) {
	if (offset + 1 < kSize)
		WRITE_LE_UINT16(vars + offset, value);
}

// ---- Conditions --------------------------------------------------------------

bool Conditions::load(Resource &resources, StartupLog &log) {
	if (!resources.load("CONDIT.HSQ", _data) || _data.size() < 2) {
		log.line("Dialogue: CONDIT.HSQ missing");
		_data.clear();
		return false;
	}
	return true;
}

uint Conditions::count() const {
	return _data.size() >= 2 ? READ_LE_UINT16(_data.data()) / 2 : 0;
}

bool Conditions::operand(uint &position, const GameState &state, uint16 &value) const {
	if (position >= _data.size())
		return false;
	const byte kind = _data[position++];
	if (kind < 0x80) {
		if (position >= _data.size())
			return false;
		const byte index = _data[position++];
		value = kind == 1 ? state.b(index) : state.w(index);
	} else if (kind == 0x80) {
		if (position >= _data.size())
			return false;
		value = _data[position++];
	} else {
		if (position + 1 >= _data.size())
			return false;
		value = READ_LE_UINT16(_data.data() + position);
		position += 2;
	}
	return true;
}

uint16 Conditions::apply(byte op, uint16 left, uint16 right) {
	// off_1A376: comparisons answer 0xFFFF or 0; jb/ja are unsigned, jle/jge signed.
	switch (op & 0x1f) {
	case 0x00:
		return left == right ? 0xffff : 0;
	case 0x02:
		return left < right ? 0xffff : 0;
	case 0x04:
		return left > right ? 0xffff : 0;
	case 0x06:
		return left != right ? 0xffff : 0;
	case 0x08:
		return (int16)left <= (int16)right ? 0xffff : 0;
	case 0x0a:
		return (int16)left >= (int16)right ? 0xffff : 0;
	case 0x0c:
		return left + right;
	case 0x0e:
		return left - right;
	case 0x10:
		return left & right;
	case 0x12:
		return left | right;
	default:
		return 0;
	}
}

bool Conditions::evaluate(uint index, const GameState &state) const {
	if (!index)
		return true;
	if (index > count())
		return false;
	uint position = READ_LE_UINT16(_data.data() + (index - 1) * 2);
	uint16 left;
	if (!operand(position, state, left))
		return false;

	struct Frame {
		uint16 left;
		byte op;
	};
	Common::Array<Frame> stack;
	for (;;) {
		if (position >= _data.size())
			return false;
		const byte op = _data[position++];
		if (op == 0xff)
			break;
		if (op & 0x80) {
			// Binds tighter: keep what we have and start a new left side.
			Frame frame;
			frame.left = left;
			frame.op = op;
			stack.push_back(frame);
			if (!operand(position, state, left))
				return false;
		} else {
			uint16 right;
			if (!operand(position, state, right))
				return false;
			left = apply(op, left, right);
		}
	}
	// The stacked operations are resolved from the oldest one on
	// (loc_1A3CB), the final value being the last right-hand side.
	if (!stack.empty()) {
		uint16 accumulated = stack[0].left;
		for (uint i = 0; i < stack.size(); ++i) {
			const uint16 next = i + 1 < stack.size() ? stack[i + 1].left : left;
			accumulated = apply(stack[i].op, accumulated, next);
		}
		left = accumulated;
	}
	return left != 0;
}

// ---- Dialogue ----------------------------------------------------------------

bool Dialogue::load(Resource &resources, StartupLog &log) {
	_split = 0;
	if (!resources.load("DIALOGUE.HSQ", _data) || _data.size() < 0x62) {
		log.line("Dialogue: DIALOGUE.HSQ missing");
		_data.clear();
		return false;
	}
	_split = READ_LE_UINT16(_data.data() + 0x60);
	return true;
}

uint Dialogue::listOffset(uint character, uint list) const {
	const uint index = character * kListsPerCharacter + list;
	if (_data.size() < 2 || index >= READ_LE_UINT16(_data.data()) / 2)
		return 0;
	const uint offset = READ_LE_UINT16(_data.data() + index * 2);
	return offset + 1 < _data.size() ? offset : 0;
}

bool Dialogue::entryAt(uint offset, Entry &entry) const {
	if (!offset || offset + 4 > _data.size())
		return false;
	const byte *p = _data.data() + offset;
	if (p[0] == 0xff && p[1] == 0xff)
		return false;
	entry.offset = offset;
	entry.flags = p[0];
	entry.flags2 = p[2];
	entry.condition = ((uint)(p[2] >> 6) << 8) | p[1];
	const uint number = ((uint)(p[2] & 3) << 8) | p[3];
	entry.sentence = number ? number - 1 : 0;
	return true;
}

void Dialogue::setData(const Common::Array<byte> &data) {
	if (data.size() == _data.size())
		_data = data;
}

void Dialogue::markSaid(uint offset) {
	if (offset + 4 <= _data.size())
		_data[offset] |= 0x80;
}

// ---- Conversation ------------------------------------------------------------

Conversation::Conversation(SentenceBank &sentences, Dialogue &dialogue, Conditions &conditions, GameState &state,
		StartupLog &log) :
		_sentences(sentences), _dialogue(dialogue), _conditions(conditions), _state(state), _log(log),
		_active(false), _character(0), _list(0), _searchOffset(0), _pendingFinish(false), _endAfter(false), _oneList(false),
		_mask(0x80), _pageIndex(0) {
	memset(&_current, 0, sizeof(_current));
}

void Conversation::start(uint character, uint list, byte mask, bool oneList, bool single) {
	_active = true;
	_oneList = oneList;
	_single = single;
	_paused = false;
	_character = character;
	_list = list;
	_searchOffset = 0;
	_mask = mask;
	_pendingFinish = _endAfter = false;
	_pages.clear();
	_pageIndex = 0;
	// sub_193DF: the character now counts as met and as the one Paul talks to.
	const uint16 bit = character < 16 ? (uint16)(1 << character) : 0;
	_state.setW(GameState::kPersonsMet, _state.w(GameState::kPersonsMet) | bit);
	_state.setW(GameState::kPersonsTalkingTo, bit);
	_log.line(Common::String::format("Conversation: character %u, list %u, phase %u", character, list,
			_state.b(GameState::kPhase)));
}

bool Conversation::next(Common::String &page, bool &newSentence) {
	if (_paused)
		return false;
	while (_active) {
		if (_pageIndex < _pages.size()) {
			newSentence = _pageIndex == 0;
			page = _pages[_pageIndex++];
			return true;
		}
		if (_pendingFinish) {
			finishEntry();
			if (_paused)
				return false; // the host shows the bargaining menu
			if (_endAfter || _single)
				break;
		}
		if (!findEntry())
			break;
	}
	_active = false;
	_state.setW(GameState::kPersonsTalkingTo, 0);
	return false;
}

bool Conversation::hasLine(uint character, uint list, byte mask) const {
	uint offset = _dialogue.listOffset(character, list);
	Dialogue::Entry entry;
	while (_dialogue.entryAt(offset, entry)) {
		const bool skip = entry.said() && !entry.repeatable() && (entry.flags & mask) != 0;
		if (!skip && _conditions.evaluate(entry.condition, _state))
			return true;
		offset += 4;
	}
	return false;
}

bool Conversation::findEntry() {
	for (;;) {
		uint offset = _searchOffset ? _searchOffset : _dialogue.listOffset(_character, _list);
		Dialogue::Entry entry;
		while (_dialogue.entryAt(offset, entry)) {
			// loc_19FAB: a line already said is skipped unless it is
			// repeatable or outside the current mask.
			const bool skip = entry.said() && !entry.repeatable() && (entry.flags & _mask) != 0;
			if (!skip && _conditions.evaluate(entry.condition, _state)) {
				_current = entry;
				_searchOffset = offset + 4;
				_pendingFinish = true;
				const uint16 id = (uint16)((entry.sentence + 1) | SentenceBank::kPhraseFlag);
				const Common::String text = _sentences.text(id, _dialogue.secondPhraseFile(offset), _state);
				_pages.clear();
				_pageIndex = 0;
				Common::String current;
				for (uint i = 0; i < text.size(); ++i) {
					if ((byte)text[i] == SentenceBank::kPageBreak) {
						if (!current.empty())
							_pages.push_back(current);
						current.clear();
					} else {
						current += text[i];
					}
				}
				if (!current.empty())
					_pages.push_back(current);
				_log.line(Common::String::format("Dialogue: entry %u, condition %u, sentence %u, action %u, %u page(s)",
						offset, entry.condition, entry.sentence, entry.action(), _pages.size()));
				return true;
			}
			offset += 4;
		}
		// List exhausted: the executable goes on with the next list while
		// its number is not a multiple of four (loc_194A5).
		_searchOffset = 0;
		++_list;
		if (_oneList || (_list & 3) == 0 || _list >= Dialogue::kListsPerCharacter)
			return false;
	}
}

void Conversation::finishEntry() {
	_pendingFinish = false;
	const bool wasSaid = _current.said();
	applyAction(_current, wasSaid);
	_dialogue.markSaid(_current.offset);
	// sub_1A03F records lines flagged with a topic (bits 2-5 of the third
	// byte) for the book, once.
	if (((_current.flags2 >> 2) & 0x0f) && !wasSaid) {
		_state.notebook.push_back((uint16)((_character << 11) | (_current.offset / 4)));
		_log.line(Common::String::format("Dialogue: line recorded in the book (topic %u)", (_current.flags2 >> 2) & 0x0f));
	}
}

void Conversation::applyAction(const Dialogue::Entry &entry, bool wasSaid) {
	// The fifteen handlers dispatched from sub_1A03F through the table at
	// cs:0xA107 (madmoose's dune-chani names them).
	switch (entry.action()) {
	case 0:
		break;
	case 1:
		// 0xA1D0 follow me: the gate stays armed.
		_gate = 0xff;
		break;
	case 2:
		// 0xA1D6 stay here: the refusal drops the gate.
		_gate = 0;
		break;
	case 6:
		// 0xA1E8: the conversation ends after this line.
		_endAfter = true;
		break;
	case 7:
		// 0xA1DC: show the equipment in the map dialogue (the gate reads 0x80).
		_gate = 0x80;
		break;
	case 4: // 0xA244: bargain with Duncan (the Emperor's spice)
	case 5: // 0xA248: bargain with a smuggler
		_bargainParty = entry.action() == 4 ? 0 : 1;
		_state.setB(0x9f, 0);
		_paused = true;
		break;
	case 3:  // 0xA1F7: a scripted scene chosen by the phase
	case 8:  // 0xA125: the speaker's effect (Jessica's training, Duncan's shipment, ...)
	case 9:  // 0xA157: the speaker's second effect
	case 15: // 0xA172: the speaker's third effect
	case 11: // 0xA219: the next story phase (first time only)
	case 12: // 0xA235: the next chapter (first time only)
		if (_eventHandler)
			_eventHandler(_eventContext, entry.action(), wasSaid, _character);
		else
			_log.line(Common::String::format("Dialogue: event %u without a story handler", entry.action()));
		break;
	case 10:
		// 0xA25B: picks the CD voice's lip-sync record (ds:4780 through the
		// table at ds:197c into ds:47e1/47e4). No speech is played yet.
		break;
	case 14:
		// 0xA1ED: a counter at 0xC2, once.
		if (!wasSaid)
			_state.setB(0xc2, _state.b(0xc2) + 1);
		break;
	default:
		_log.line(Common::String::format("Dialogue: action %u not implemented", entry.action()));
		break;
	}
}

} // namespace Dune

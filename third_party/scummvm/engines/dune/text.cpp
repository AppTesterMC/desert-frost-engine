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

#include "dune/text.h"

#include "common/endian.h"

#include "dune/debug.h"
#include "dune/dialogue.h"
#include "dune/resource.h"

namespace Dune {

SentenceBank::SentenceBank(Resource &resources, StartupLog &log) : _resources(resources), _log(log) {
}

bool SentenceBank::load(uint language) {
	bool ok = _resources.load(Common::String::format("COMMAND%u.HSQ", language), _commands);
	ok = _resources.load(Common::String::format("PHRASE%u1.HSQ", language), _phrases[0]) && ok;
	ok = _resources.load(Common::String::format("PHRASE%u2.HSQ", language), _phrases[1]) && ok;
	if (!ok)
		_log.line("Sentences: COMMAND/PHRASE files missing");
	else
		_log.line(Common::String::format("Sentences: %u commands, %u + %u phrases",
				READ_LE_UINT16(_commands.data()) / 2, READ_LE_UINT16(_phrases[0].data()) / 2,
				READ_LE_UINT16(_phrases[1].data()) / 2));
	return ok;
}

const byte *SentenceBank::entry(const Common::Array<byte> &file, uint index, uint &length) {
	length = 0;
	if (file.size() < 2)
		return nullptr;
	const uint count = READ_LE_UINT16(file.data()) / 2;
	if (index >= count || (index + 1) * 2 > file.size())
		return nullptr;
	const uint offset = READ_LE_UINT16(file.data() + index * 2);
	if (offset >= file.size())
		return nullptr;
	uint end = offset;
	while (end < file.size() && file[end] != 0xff)
		++end;
	length = end - offset;
	return file.data() + offset;
}

const byte *SentenceBank::raw(uint16 id, bool secondPhraseFile, uint &length) const {
	// sub_1CF70: the id is decremented first, then bit 11 picks the file.
	length = 0;
	if (!id)
		return nullptr;
	const uint index = (uint)(id - 1) & 0x7ff;
	if ((id - 1) & kPhraseFlag)
		return entry(_phrases[secondPhraseFile ? 1 : 0], index, length);
	return entry(_commands, index, length);
}

Common::String SentenceBank::command(uint index) const {
	uint length;
	const byte *p = entry(_commands, index, length);
	Common::String out;
	for (uint i = 0; p && i < length; ++i)
		if (p[i] >= 0x20 && p[i] < 0x80)
			out += (char)p[i];
	return out;
}

void SentenceBank::patchCommandNumber(uint index, uint value) {
	uint length;
	const byte *p = entry(_commands, index, length);
	if (!p)
		return;
	byte *s = _commands.data() + (p - _commands.data());
	uint i = 0;
	while (i < length && (s[i] < '0' || s[i] > '9'))
		++i;
	if (i >= length)
		return;
	while (i < length && s[i] >= '0' && s[i] <= '9')
		++i;
	if (i < 3)
		return;
	value = MIN<uint>(value, 999);
	const uint hundreds = value / 100, tens = value / 10 % 10, ones = value % 10;
	s[i - 3] = hundreds ? (byte)('0' + hundreds) : ' ';
	s[i - 2] = (!hundreds && !tens) ? ' ' : (byte)('0' + tens);
	s[i - 1] = (byte)('0' + ones);
}

Common::String SentenceBank::text(uint16 id, bool secondPhraseFile, const GameState &state) const {
	Common::String out;
	uint length;
	const byte *p = raw(id, secondPhraseFile, length);
	if (!p) {
		_log.line(Common::String::format("Sentence %u (%s) missing", (id - 1) & 0x7ff,
				((id - 1) & kPhraseFlag) ? "phrase" : "command"));
		return out;
	}
	expand(p, length, secondPhraseFile, state, out, 0);
	return out;
}

void SentenceBank::expand(const byte *p, uint length, bool secondPhraseFile, const GameState &state,
		Common::String &out, uint depth) const {
	if (depth > 4)
		return;
	for (uint i = 0; i < length;) {
		const byte c = p[i++];
		if (c < 0x80) {
			if (c == 0x0d)
				out += '\r';
			else if (c >= 0x20)
				out += (char)c;
			// 0x06, 0x08 and the other control bytes are layout hints of the
			// original renderer.
			continue;
		}
		if (c == 0x80) {
			// Another sentence by id, stored big-endian (lodsw; xchg ah, al).
			if (i + 1 >= length)
				break;
			const uint16 id = (uint16)((p[i] << 8) | p[i + 1]);
			i += 2;
			uint nestedLength;
			const byte *nested = raw(id, secondPhraseFile, nestedLength);
			if (nested)
				expand(nested, nestedLength, secondPhraseFile, state, out, depth + 1);
			continue;
		}
		if (c < 0x90) {
			// A sentence whose id sits in the game's name table.
			const uint16 id = state.w(state.nameTable + 2 * (c & 0x0f));
			uint nestedLength;
			const byte *nested = id ? raw(id, secondPhraseFile, nestedLength) : nullptr;
			if (nested)
				expand(nested, nestedLength, secondPhraseFile, state, out, depth + 1);
			continue;
		}
		if (c < 0xa0) {
			// A variable as a number: 0x92 reads a word, the others a byte.
			if (i >= length)
				break;
			const byte variable = p[i++];
			const uint value = c == 0x92 ? state.w(variable) : state.b(variable);
			out += Common::String::format("%u", value);
			continue;
		}
		if (c < 0xd0) {
			out += (char)c; // extended characters: the font draws their low seven bits
			continue;
		}
		if (c < 0xf0) {
			// Multi-byte layout codes (sub_18944): 0xD0 carries two bytes,
			// 0xD1 four, the rest one.
			i += c == 0xd0 ? 2 : c == 0xd1 ? 4 : 1;
			continue;
		}
		if (c == kPageBreak) {
			out += (char)kPageBreak;
			continue;
		}
		break; // 0xF0-0xFD end a nested sentence, 0xFF the text
	}
}

} // namespace Dune

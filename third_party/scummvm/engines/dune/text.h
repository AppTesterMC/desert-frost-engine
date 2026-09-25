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

#ifndef ENGINES_DUNE_TEXT_H
#define ENGINES_DUNE_TEXT_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Dune {

class Resource;
class StartupLog;
struct GameState;

/**
 * The game's sentences.
 *
 * COMMANDx.HSQ holds commands, place and person names and menu words;
 * PHRASEx1.HSQ and PHRASEx2.HSQ hold the dialogue lines (x = language:
 * 1 English, 2 French, 3 German). Each file is a table of 16-bit offsets
 * followed by strings terminated by 0xFF.
 *
 * Inline codes, recovered from the CD executable's string builder
 * (sub_18944 in OpenRakis' DNCDPRG_RECENT.ASM):
 *   0x0D          line break
 *   0xFE          page break: the player clicks for the rest
 *   0x80 hi lo    another sentence, by id
 *   0x81-0x8F     a sentence whose id sits in the game's name table
 *                 (word 0x11EB + 2*(code & 15): the current sietch's first
 *                 and last name, Paul's Fremen name, ...)
 *   0x91 n        byte variable n as a decimal number
 *   0x92 n        word variable n as a decimal number
 *   0xA0-0xCF     literal
 *   0xD0-0xEF     multi-byte layout codes (skipped here)
 *   0xF0-0xFD     end of a nested sentence
 *
 * Ids follow the executable's sub_1CF70: they are 1-based, and bit 11
 * (0x800) selects the phrase files instead of the command file. Which of
 * the two phrase files applies depends on where the dialogue entry sits
 * (see Dialogue::secondPhraseFile).
 */
class SentenceBank {
public:
	enum {
		kPageBreak = 0xfe,
		kPhraseFlag = 0x800
	};

	SentenceBank(Resource &resources, StartupLog &log);

	bool load(uint language = 1);
	bool loaded() const { return !_commands.empty() && !_phrases[0].empty(); }

	/**
	 * A sentence with its codes resolved: printable text, '\r' for line
	 * breaks and kPageBreak between pages.
	 */
	Common::String text(uint16 id, bool secondPhraseFile, const GameState &state) const;

	/** The 0-based command string, or "" when it does not exist. */
	Common::String command(uint index) const;

private:
	static const byte *entry(const Common::Array<byte> &file, uint index, uint &length);
	const byte *raw(uint16 id, bool secondPhraseFile, uint &length) const;
	void expand(const byte *p, uint length, bool secondPhraseFile, const GameState &state,
				Common::String &out, uint depth) const;

	Resource &_resources;
	StartupLog &_log;
	Common::Array<byte> _commands;
	Common::Array<byte> _phrases[2];
};

} // namespace Dune

#endif // ENGINES_DUNE_TEXT_H

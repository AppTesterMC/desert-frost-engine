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

#include "common/endian.h"
#include "common/str.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/managed_surface.h"
#include "graphics/paletteman.h"

#include "dune/panel.h"
#include "dune/resource.h"
#include "dune/sprite.h"

namespace Dune {

Panel::Panel(OSystem *system, Resource &resources) : _system(system), _leftPanel(kLeftBook) {
	for (uint i = 0; i < kCommandRows; ++i) {
		_commandRows[i] = 0xffff;
		_rowDisabled[i] = false;
	}
	resources.load("ICONES.HSQ", _icons);
	resources.load("PERS.HSQ", _characters);
	if (!resources.load("DNCHAR.BIN", _font))
		resources.load("DUNECHAR.HSQ", _font);

	Common::Array<byte> commands;
	if (resources.load("COMMAND1.HSQ", commands) && commands.size() >= 2) {
		const uint count = READ_LE_UINT16(commands.data()) / 2;
		if (count <= 512 && (uint32)count * 2 <= commands.size()) {
			_commandStrings.resize(count);
			for (uint i = 0; i < count; ++i) {
				const uint32 start = READ_LE_UINT16(commands.data() + i * 2);
				if (start >= commands.size())
					continue;
				uint32 end = start;
				while (end < commands.size() && commands[end] != 0xff)
					++end;
				_commandStrings[i] = Common::String((const char *)commands.data() + start, end - start);
			}
		}
	}
}

void Panel::applyPalette() {
	if (!_characters.empty())
		Sprite(_system, _characters).setPalette();
}

void Panel::setCommandRows(const uint16 *indices, uint count) {
	for (uint i = 0; i < kCommandRows; ++i) {
		_commandRows[i] = i < count ? indices[i] : 0xffff;
		_rowText[i].clear();
		_rowDisabled[i] = false;
	}
}

void Panel::setRowDisabled(uint row, bool disabled) {
	if (row < kCommandRows)
		_rowDisabled[row] = disabled;
}

void Panel::setRowText(uint row, const Common::String &text) {
	if (row < kCommandRows)
		_rowText[row] = text;
}

const char *Panel::commandText(uint row) const {
	if (row >= kCommandRows)
		return nullptr;
	if (!_rowText[row].empty())
		return _rowText[row].c_str();
	const uint16 index = _commandRows[row];
	if (index == 0xffff || index >= _commandStrings.size() || _commandStrings[index].empty())
		return nullptr;
	return _commandStrings[index].c_str();
}

uint16 Panel::findCommand(const char *text, bool prefix) const {
	if (!text)
		return 0xffff;
	const uint length = strlen(text);
	for (uint i = 0; i < _commandStrings.size(); ++i) {
		if (_commandStrings[i].equalsIgnoreCase(text))
			return i;
		if (prefix) {
			// Records may start with padding spaces (the map box's title).
			const char *s = _commandStrings[i].c_str();
			while (*s == ' ')
				++s;
			if (scumm_strnicmp(s, text, length) == 0)
				return i;
		}
	}
	return 0xffff;
}

Common::Rect Panel::arrowRect(uint arrow) {
	static const int16 rects[4][4] = {
		{ 269, 162, 280, 173 }, { 284, 172, 295, 183 }, { 269, 181, 280, 192 }, { 255, 172, 266, 183 }
	};
	return Common::Rect(rects[arrow][0], rects[arrow][1], rects[arrow][2], rects[arrow][3]);
}

// The font file holds 256 glyph widths, then the 128 ASCII glyphs as 9 rows
// of one byte each (MSB = leftmost pixel), then the same glyphs as a 7-row
// small set at 1408, whose widths are capped at 6. That is 2304 bytes, the
// size of both DNCHAR.BIN and DUNECHAR.HSQ.
void Panel::drawText(Graphics::ManagedSurface &surface, const char *text, int x, int y, byte colour, bool small) const {
	const uint rows = small ? 7 : 9;
	const uint32 base = small ? 1408 : 256;
	if (_font.size() < base + 128 * rows)
		return;
	for (; *text; ++text) {
		const byte c = (byte)*text & 0x7f;
		const uint32 glyph = base + (uint32)c * rows;
		for (uint row = 0; row < rows; ++row) {
			const byte bits = _font[glyph + row];
			for (int column = 0; column < 8; ++column) {
				if ((bits & (0x80 >> column)) && x + column >= 0 && x + column < surface.w && y + (int)row < surface.h)
					*(byte *)surface.getBasePtr(x + column, y + row) = colour;
			}
		}
		x += small ? MIN<int>(6, _font[c]) : _font[c];
	}
}

int Panel::textWidth(const char *text, bool small) const {
	int width = 0;
	if (_font.size() >= 256)
		for (; *text; ++text) {
			const byte c = (byte)*text & 0x7f;
			width += small ? MIN<int>(6, _font[c]) : _font[c];
		}
	return width;
}

void Panel::draw(Graphics::ManagedSurface &surface, const bool exits[4], int pressedRow, int pressedArrow, uint day) {
	surface.fillRect(Common::Rect(0, kTop, 320, 200), 0);
	if (_icons.empty())
		return;

	Graphics::Surface *target = surface.surfacePtr();
	Sprite icons(_system, _icons);
	icons.drawFrame(15, target, 126, 148);
	icons.drawFrame(14, target, kCommandLeft, kTop);
	icons.drawFrame(26, target, 150, 137);
	icons.drawFrame(12, target, 2, 154);
	icons.drawFrame(12, target, 317, 154);
	icons.drawFrame(3, target, 228, kTop);

	// Book block with the day counter.
	if (_leftPanel == kLeftOpenBook) {
		icons.drawFrame(9, target, 0, kTop);
	} else if (_leftPanel == kLeftGlobe) {
		icons.drawFrame(6, target, 0, kTop);
	} else {
		icons.drawFrame(0, target, 0, kTop);
		// ui_draw_date_and_time_indicator (seg000:1a34): the sun (ICONES 0x4a)
		// and the moon (0x4b) at the period's positions in the table at
		// ds:1e7e (0, 0 = below the horizon), then the day number over them.
		static const int16 kSunMoon[16][4] = {
			{ 6, 187, 25, 186 }, { 6, 186, 26, 188 }, { 6, 185, 0, 0 }, { 7, 183, 0, 0 }, { 9, 182, 0, 0 },
			{ 10, 181, 0, 0 }, { 13, 181, 0, 0 }, { 16, 181, 0, 0 }, { 18, 182, 0, 0 }, { 20, 183, 0, 0 },
			{ 20, 185, 0, 0 }, { 20, 186, 8, 188 }, { 20, 187, 9, 186 }, { 0, 0, 12, 183 }, { 0, 0, 17, 182 },
			{ 0, 0, 23, 183 }
		};
		if (kSunMoon[_period][0])
			icons.drawFrame(0x4a, target, kSunMoon[_period][0], kSunMoon[_period][1]);
		if (kSunMoon[_period][2])
			icons.drawFrame(0x4b, target, kSunMoon[_period][2], kSunMoon[_period][3]);
		const Common::String dayText = Common::String::format("%u", day);
		drawText(surface, dayText.c_str(), 7 + (22 - textWidth(dayText.c_str(), true)) / 2, 189, kLightColour, true);
	}
	// The companions' boxes (seg000:d763, records ds:1c0c/1c1a): ICONES
	// 0x40 empty, or 0x41 + the character travelling with Paul.
	icons.drawFrame(_companions[0] != 0xff ? 0x41 + _companions[0] : 64, target, 35, 182);
	icons.drawFrame(_companions[1] != 0xff ? 0x41 + _companions[1] : 64, target, 58, 182);

	// Compass: centre, the position marker, then only the lit directions.
	icons.drawFrame(33, target, 255, 162);
	icons.drawFrame(36, target, 269, 173);
	for (uint arrow = 0; arrow < 4; ++arrow) {
		if (!exits[arrow])
			continue;
		const Common::Rect rect = arrowRect(arrow);
		if ((int)arrow == pressedArrow)
			surface.fillRect(rect, 0);
		icons.drawFrame(29 + arrow, target, rect.left, rect.top);
	}

	// Command box: the row bars form its dark background; a pressed row is
	// inverted the way the original highlights the row under the pointer.
	surface.fillRect(Common::Rect(kCommandLeft, kCommandTop, kCommandRight, kCommandTop + kCommandRows * kCommandHeight),
			kLightColour);
	for (uint row = 0; row < kCommandRows; ++row) {
		const int y = kCommandTop + (int)row * kCommandHeight;
		icons.drawFrame(27, target, kCommandLeft, y);
		const bool pressed = (int)row == pressedRow;
		if (pressed)
			surface.fillRect(Common::Rect(kCommandLeft + 1, y + 1, kCommandRight - 1, y + 1 + 7), kLightColour);
		const char *label = commandText(row);
		if (label)
			drawText(surface, label, kTextLeft, y + 1, pressed ? kDarkColour : _rowDisabled[row] ? kDarkColour + 3 : kLightColour,
					true);
	}
}

Panel::Action Panel::hitTest(int x, int y, int &row, int &arrow) const {
	row = arrow = -1;
	// Paul's head (ICONES 26 at 150,137) sticks up from the box's top edge;
	// the original's zone is that edge (92..229 x 152..159), widened here
	// around the head for fingers.
	if (x >= 138 && x < 182 && y >= 134 && y < 160)
		return kActionHead;

	if (y < kTop) {
		if (x < 80)
			return kActionLeft;
		return x >= 240 ? kActionRight : kActionUp;
	}

	if (Common::Rect(24, 155, 70, 177).contains(x, y))
		return kActionBook;

	for (uint i = 0; i < 4; ++i) {
		Common::Rect zone = arrowRect(i);
		zone.grow(3); // Finger-sized targets.
		if (zone.contains(x, y)) {
			arrow = i;
			return (Action)(kActionUp + i);
		}
	}

	if (x >= kCommandLeft && x < kCommandRight && y >= kCommandTop) {
		const uint hit = MIN<uint>((y - kCommandTop) / kCommandHeight, kCommandRows - 1);
		if (commandText(hit) && !_rowDisabled[hit]) {
			row = hit;
			return kActionCommand;
		}
		return kActionNone;
	}

	return kActionNone;
}

void Panel::wrapText(const Common::String &text, int width, bool small, Common::Array<Common::String> &lines) const {
	Common::String line, word;
	const uint length = text.size();
	for (uint i = 0; i <= length; ++i) {
		const char c = i < length ? text[i] : ' ';
		if (c != ' ' && c != '\r') {
			word += c;
			continue;
		}
		if (!word.empty()) {
			const Common::String candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && textWidth(candidate.c_str(), small) > width) {
				lines.push_back(line);
				line = word;
			} else {
				line = candidate;
			}
			word.clear();
		}
		if (c == '\r') {
			lines.push_back(line);
			line.clear();
		}
	}
	if (!line.empty())
		lines.push_back(line);
}

void Panel::drawParagraph(Graphics::ManagedSurface &surface, const Common::Array<Common::String> &lines, uint first,
		uint count) const {
	for (uint i = 0; i < count && first + i < lines.size(); ++i)
		drawText(surface, lines[first + i].c_str(), kTextLeft, kCommandTop + (int)i * kCommandHeight + 1, kLightColour,
				true);
}

void Panel::drawIcon(Graphics::ManagedSurface &surface, uint16 frame, int x, int y) const {
	if (_icons.empty())
		return;
	Sprite icons(_system, _icons);
	icons.drawFrame(frame, surface.surfacePtr(), x, y);
}

} // namespace Dune

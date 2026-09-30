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

	auto readCommands = [](const Common::Array<byte> &data, Common::Array<Common::String> &strings) {
		if (data.size() < 2)
			return;
		const uint count = READ_LE_UINT16(data.data()) / 2;
		if (count > 512 || (uint32)count * 2 > data.size())
			return;
		strings.resize(count);
		for (uint i = 0; i < count; ++i) {
			const uint32 start = READ_LE_UINT16(data.data() + i * 2);
			if (start >= data.size())
				continue;
			uint32 end = start;
			while (end < data.size() && data[end] != 0xff)
				++end;
			strings[i] = Common::String((const char *)data.data() + start, end - start);
		}
	};
	Common::Array<byte> commands;
	if (resources.load("COMMAND1.HSQ", commands))
		readCommands(commands, _commandStrings);
	// Engine callers currently identify a command by its English wording.
	// Its index is the same in every language: translate the display, not
	// the identifier, or non-English map/save/troop rows disappear.
	if (resources.language() != 1 && resources.loadUntranslated("COMMAND1.HSQ", commands))
		readCommands(commands, _commandKeys);
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
	const Common::Array<Common::String> &keys = _commandKeys.empty() ? _commandStrings : _commandKeys;
	const uint length = strlen(text);
	for (uint i = 0; i < keys.size(); ++i) {
		if (keys[i].equalsIgnoreCase(text))
			return i;
		if (prefix) {
			// Records may start with padding spaces (the map box's title).
			const char *s = keys[i].c_str();
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

	// The navigation panel (floppy seg000:329F, drawn by D135): the compass
	// screen is cleared to colour 240, then the layout's records are drawn.
	surface.fillRect(Common::Rect(254, 162, 297, 194), 240);
	switch (_navMode) {
	case kNavBlank:
		break;
	case kNavFlight:
		// Steerable flight: turn left, straight on, turn right.
		icons.drawFrame(42, target, 258, 172);
		icons.drawFrame(43, target, 270, 170);
		icons.drawFrame(44, target, 283, 172);
		break;
	default: {
		icons.drawFrame(_navMode == kNavRoom ? 33 : _navMode == kNavFront ? 34 : 35, target, 255, 162);
		if (_navMode == kNavRoom && _navDot)
			icons.drawFrame(36, target, 269, 173);
		for (uint arrow = 0; arrow < 4; ++arrow) {
			if (!exits[arrow] && _navMode != kNavDesert)
				continue;
			const Common::Rect rect = arrowRect(arrow);
			if ((int)arrow == pressedArrow)
				surface.fillRect(rect, 0);
			icons.drawFrame(29 + arrow, target, rect.left, rect.top);
		}
		break;
	}
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
	// Paul's head (ICONES 0x10 + ds:E8), on top as ui_hud_head_redraw draws it;
	// what lies under it is kept for redrawHead().
	for (int y = 0; y < kHeadHeight; ++y)
		for (int x = 0; x < kHeadWidth; ++x)
			_headBackdrop[y * kHeadWidth + x] = *(const byte *)surface.getBasePtr(kHeadX + x, kHeadY + y);
	_headBackdropValid = true;
	icons.drawFrame(0x10 + _headIndex, target, kHeadX, kHeadY);
}

void Panel::redrawHead(Graphics::ManagedSurface &surface) {
	if (_icons.empty() || !_headBackdropValid)
		return;
	for (int y = 0; y < kHeadHeight; ++y)
		for (int x = 0; x < kHeadWidth; ++x)
			*(byte *)surface.getBasePtr(kHeadX + x, kHeadY + y) = _headBackdrop[y * kHeadWidth + x];
	Sprite icons(_system, _icons);
	icons.drawFrame(0x10 + _headIndex, surface.surfacePtr(), kHeadX, kHeadY);
}

void Panel::drawHead(Graphics::ManagedSurface &surface) const {
	if (_icons.empty())
		return;
	Sprite icons(_system, _icons);
	icons.drawFrame(0x10 + _headIndex, surface.surfacePtr(), kHeadX, kHeadY);
}

Panel::Action Panel::hitTest(int x, int y, int &row, int &arrow) const {
	row = arrow = -1;
	// Paul's head (ICONES 0x10 + ds:E8 at 150,137) sticks up from the box's top edge.
	// The original's zone is that edge alone (92..229 x 152..159): a click
	// on the head itself, in the view, does nothing (the user chose the
	// original's zone on 2026-09-28; it was widened round the head before).
	if (x >= 92 && x <= 229 && y >= 152 && y <= 159)
		return kActionHead;

	if (y < kTop) {
		if (x < 80)
			return kActionLeft;
		return x >= 240 ? kActionRight : kActionUp;
	}

	if (Common::Rect(24, 155, 70, 177).contains(x, y))
		return kActionBook;

	// The red dot of the Atreides palace's compass box (UI element ds:1cbc:
	// 269,173-280,181, handler seg000:18ee) opens the palace plan. It comes
	// before the arrows, whose finger-sized zones reach over it.
	if (_navMode == kNavRoom && _navDot && Common::Rect(269, 173, 281, 182).contains(x, y))
		return kActionPlan;

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

int Panel::greyedRowAt(int x, int y) const {
	if (x < kCommandLeft || x >= kCommandRight || y < kCommandTop)
		return -1;
	const uint hit = MIN<uint>((y - kCommandTop) / kCommandHeight, kCommandRows - 1);
	return commandText(hit) && _rowDisabled[hit] ? (int)hit : -1;
}

void Panel::wrapText(const Common::String &text, int width, bool small, Common::Array<Common::String> &lines,
		bool markBreaks) const {
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
			// A forced break ends a paragraph: its line is not justified.
			lines.push_back(markBreaks ? line + '\x01' : line);
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

bool Panel::iconSize(uint16 frame, uint16 &width, uint16 &height) const {
	if (_icons.empty())
		return false;
	Sprite icons(_system, _icons);
	return icons.frameSize(frame, width, height);
}

} // namespace Dune

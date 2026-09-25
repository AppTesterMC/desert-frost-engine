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

#ifndef ENGINES_DUNE_PANEL_H
#define ENGINES_DUNE_PANEL_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/str.h"

class OSystem;

namespace Graphics {
class ManagedSurface;
}

namespace Dune {

class Resource;

/**
 * The control panel under the 320x152 game view, drawn as the original
 * draws it (swift-dune UI.swift, checked against the recording):
 *
 *   ICONES 15 at 126,148 and 26 at 150,137   the hinge above the command box
 *   ICONES 12 at 2,154 and 317,154            side pins
 *   ICONES 0 at 0,152                         book block (closed book)
 *     ICONES 74 at 6,184 + the day number     sun icon and day counter
 *     ICONES 64 at 35,182 and 58,182          the two companion slots
 *   ICONES 14 at 92,152                       command box frame
 *     five rows from y=159, 8 px apart, ICONES 27 as the row bar; text in
 *     the small game font at x=97 in colour 250, or 243 on a 250 highlight
 *   ICONES 3 at 228,152                       compass block
 *     ICONES 33 at 255,162, 36 at 269,173     compass centre and position
 *     ICONES 29-32                            up/right/down/left arrows
 *
 * Geometry agrees with the hot-zone table of DNCDPRG.EXE at file offset
 * 0x111A0. ICONES.HSQ has no palette: colours 1-15 and 224-239 come from
 * PERS.HSQ, 240-255 from the current room sheet, so the panel tint follows
 * the room. The commands are COMMAND1.HSQ records; which ones a room offers
 * is not decoded yet beyond the throne room's first two.
 */
class Panel {
public:
	enum Action {
		kActionNone,
		kActionUp, // The four directions are contiguous and ordered like Direction.
		kActionRight,
		kActionDown,
		kActionLeft,
		kActionBook,
		kActionCommand,
		kActionHead ///< Paul's head above the command box: the globe/stats screen
	};

	/** The box colours: 240-255 belong to the room sheet, so the panel's tint follows the room. */
	enum {
		kLightColour = 250,
		kDarkColour = 243
	};

	enum LeftPanel {
		kLeftBook,     ///< the closed book with the day counter
		kLeftOpenBook, ///< ICONES 9
		kLeftGlobe     ///< ICONES 6, used by the map and globe screens
	};

	enum {
		kTop = 152,
		kCommandRows = 5
	};

	Panel(OSystem *system, Resource &resources);

	/** Set the interface palette ranges; call before the room sheet's. */
	void applyPalette();

	/**
	 * Draw the panel into a 320x200 surface that already holds the view.
	 * @param exits        which compass arrows are lit (indexed by Direction)
	 * @param pressedRow   command row to draw highlighted, or -1
	 * @param pressedArrow compass arrow to draw pressed, or -1
	 * @param day          the day counter shown under the book
	 */
	void draw(Graphics::ManagedSurface &surface, const bool exits[4], int pressedRow = -1, int pressedArrow = -1,
			  uint day = 1);
	/** The period of the day (0-15) for the sun and moon in the book's window (seg000:1a34). */
	void setTimeOfDay(uint period) { _period = period & 15; }

	/** COMMAND1.HSQ records for rows 0..4 (0xffff = empty row). */
	void setCommandRows(const uint16 *indices, uint count);
	const char *commandText(uint row) const;
	/** Show this text on a row instead of its COMMAND string (the save logs carry their time). */
	void setRowText(uint row, const Common::String &text);
	/** A greyed row: shown but not selectable (the verbs a troop cannot take yet). */
	void setRowDisabled(uint row, bool disabled);
	/** A COMMAND string by id (0-based), empty when out of range. */
	Common::String commandString(uint16 index) const {
		return index < _commandStrings.size() ? _commandStrings[index] : Common::String();
	}
	/** Find a command by its decoded text; CD and floppy tables use different indices. */
	/** COMMAND record with this text (case-insensitive), or 0xffff; @p prefix matches the record's start. */
	uint16 findCommand(const char *text, bool prefix = false) const;

	/** Word-wraps @p text to @p width pixels in the small or the 9-row font; '\r' forces a line break. */
	void wrapText(const Common::String &text, int width, bool small, Common::Array<Common::String> &lines) const;
	/** The game font: 9-row glyphs at 256, or the 7-row small set at 1408. */
	void drawText(Graphics::ManagedSurface &surface, const char *text, int x, int y, byte colour, bool small) const;
	int textWidth(const char *text, bool small) const;
	/** The left part shows the open book (ICONES 9) instead of the closed one with the day. */
	void setBookOpen(bool open) { _leftPanel = open ? kLeftOpenBook : kLeftBook; }
	void setLeftPanel(LeftPanel left) { _leftPanel = left; }
	/** The two companions drawn in the book block's boxes (0xff: none). */
	void setCompanions(byte first, byte second) {
		_companions[0] = first;
		_companions[1] = second;
	}
	/** Draw one ICONES frame (map and globe navigation arrows). */
	void drawIcon(Graphics::ManagedSurface &surface, uint16 frame, int x, int y) const;
	/** Draws dialogue lines in the command box, one per row from @p first, over the rows draw() painted. */
	void drawParagraph(Graphics::ManagedSurface &surface, const Common::Array<Common::String> &lines, uint first,
					   uint count) const;

	/**
	 * Map a tap to an action. Taps on the view walk (side thirds turn, the
	 * middle goes on), which suits a touch screen better than the small
	 * compass. row/arrow receive the control to flash, or -1.
	 */
	Action hitTest(int x, int y, int &row, int &arrow) const;

	/** Sprite sheet with the standing characters (shares the panel palette). */
	const Common::Array<byte> &characterSheet() const { return _characters; }

private:
	enum {
		kCommandLeft = 92,
		kCommandRight = 228,
		kCommandTop = 159,
		kCommandHeight = 8,
		kTextLeft = 97
	};

	static Common::Rect arrowRect(uint arrow);

	OSystem *_system;
	LeftPanel _leftPanel;
	byte _companions[2] = { 0xff, 0xff };
	Common::Array<byte> _icons;      ///< ICONES.HSQ
	Common::Array<byte> _characters; ///< PERS.HSQ: characters and the interface palette
	Common::Array<byte> _font;       ///< DNCHAR.BIN (CD) or DUNECHAR.HSQ (floppy)
	Common::Array<Common::String> _commandStrings; ///< COMMAND1.HSQ, FF-terminated records
	uint16 _commandRows[kCommandRows];
	Common::String _rowText[kCommandRows];
	bool _rowDisabled[kCommandRows];
	uint _period = 2;
};

} // namespace Dune

#endif // ENGINES_DUNE_PANEL_H

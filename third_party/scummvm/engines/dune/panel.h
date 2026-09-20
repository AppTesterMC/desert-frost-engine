/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_PANEL_H
#define ENGINES_DUNE_PANEL_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

class OSystem;

namespace Graphics {
class ManagedSurface;
}

namespace Dune {

class Resource;

/**
 * The control panel under the 320x152 game view.
 *
 * Geometry comes from the hot-zone table of DNCDPRG.EXE at file offset
 * 0x111A0 (records of x1, y1, x2, y2, flags, ICONES sprite, handler):
 *
 *   book block      ICONES 0  at 0,152    (book zone 24,155-69,176)
 *   companions      ICONES 64+ at 35,182 and 58,182 (not drawn yet)
 *   command box     92..228, border ICONES 14 at 92,152, five 8-pixel rows
 *                   from y=159, highlight bar ICONES 27
 *   compass block   ICONES 3 at 228,152, centre ICONES 33 at 255,162,
 *                   arrows ICONES 29-32 (up, right, down, left)
 *
 * ICONES.HSQ has no palette. Interface colours 1-15 and 224-239 come from
 * PERS.HSQ; every room sheet supplies 240-255, so the panel tint follows the
 * room. Text uses the game font (see drawText()).
 *
 * The command rows are placeholders: the original fills them from
 * COMMAND*.HSQ depending on who and what is in the room.
 */
class Panel {
public:
	enum Action {
		kActionNone,
		kActionUp, // The four directions are contiguous and ordered like Direction.
		kActionRight,
		kActionDown,
		kActionLeft,
		kActionMenu,
		kActionCommand
	};

	enum {
		kTop = 152
	};

	Panel(OSystem *system, Resource &resources);

	/** Set the interface palette ranges; call before the room sheet's. */
	void applyPalette();

	/**
	 * Draw the panel into a 320x200 surface.
	 * @param title        shown in the first command row
	 * @param exits        which compass arrows are lit (indexed by Direction)
	 * @param pressedRow   command row to draw highlighted, or -1
	 * @param pressedArrow compass arrow to draw pressed, or -1
	 */
	void draw(Graphics::ManagedSurface &surface, const char *title, const bool exits[4], int pressedRow = -1,
			  int pressedArrow = -1);

	/** Use real COMMAND1.HSQ string records for rows 1..4. */
	void setCommandRows(const uint16 *indices, uint count);
	const char *commandText(uint row) const;

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
		kCommandRows = 5
	};

	static Common::Rect arrowRect(uint arrow);

	void drawText(Graphics::ManagedSurface &surface, const char *text, int x, int y, byte colour) const;
	int textWidth(const char *text) const;
	byte brightestInterfaceColour() const;

	OSystem *_system;
	Common::Array<byte> _icons;      ///< ICONES.HSQ
	Common::Array<byte> _characters; ///< PERS.HSQ: characters and the interface palette
	Common::Array<byte> _font;       ///< DNCHAR.BIN (CD) or DUNECHAR.HSQ (floppy)
	Common::Array<Common::String> _commandStrings; ///< COMMAND1.HSQ, FF-terminated records
	uint16 _commandRows[kCommandRows - 1];
};

} // namespace Dune

#endif // ENGINES_DUNE_PANEL_H

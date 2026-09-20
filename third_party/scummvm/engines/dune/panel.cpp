/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
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

Panel::Panel(OSystem *system, Resource &resources) : _system(system) {
	for (uint i = 0; i < kCommandRows - 1; ++i)
		_commandRows[i] = 0xffff;
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
	for (uint i = 0; i < kCommandRows - 1; ++i)
		_commandRows[i] = i < count ? indices[i] : 0xffff;
}

const char *Panel::commandText(uint row) const {
	if (row == 0 || row >= kCommandRows)
		return nullptr;
	const uint16 index = _commandRows[row - 1];
	if (index == 0xffff || index >= _commandStrings.size() || _commandStrings[index].empty())
		return nullptr;
	return _commandStrings[index].c_str();
}

Common::Rect Panel::arrowRect(uint arrow) {
	static const int16 rects[4][4] = {
		{ 269, 162, 280, 173 }, { 284, 172, 295, 183 }, { 269, 181, 280, 192 }, { 255, 172, 266, 183 }
	};
	return Common::Rect(rects[arrow][0], rects[arrow][1], rects[arrow][2], rects[arrow][3]);
}

// Font file: 256 glyph widths, then 9 rows of one byte (MSB = leftmost pixel)
// for every glyph.
void Panel::drawText(Graphics::ManagedSurface &surface, const char *text, int x, int y, byte colour) const {
	if (_font.size() < 256 + 9)
		return;
	for (; *text; ++text) {
		const byte c = (byte)*text;
		const uint32 glyph = 256 + (uint32)c * 9;
		if (glyph + 9 > _font.size())
			continue;
		for (int row = 0; row < 9; ++row) {
			const byte bits = _font[glyph + row];
			for (int column = 0; column < 8; ++column) {
				if ((bits & (0x80 >> column)) && x + column < surface.w && y + row < surface.h)
					*(byte *)surface.getBasePtr(x + column, y + row) = colour;
			}
		}
		x += _font[c];
	}
}

int Panel::textWidth(const char *text) const {
	int width = 0;
	if (_font.size() >= 256)
		for (; *text; ++text)
			width += _font[(byte)*text];
	return width;
}

// The original's text colours are not decoded yet, so use the brightest
// entry of the interface ranges (1-15, 224-239), which is readable on every
// room's panel tint.
byte Panel::brightestInterfaceColour() const {
	byte palette[256 * 3];
	_system->getPaletteManager()->grabPalette(palette, 0, 256);
	int best = -1;
	byte bestIndex = 15;
	for (uint i = 1; i < 240; i = (i == 15) ? 224 : i + 1) {
		const int luma = palette[i * 3] * 3 + palette[i * 3 + 1] * 6 + palette[i * 3 + 2];
		if (luma > best) {
			best = luma;
			bestIndex = i;
		}
	}
	return bestIndex;
}

void Panel::draw(Graphics::ManagedSurface &surface, const char *title, const bool exits[4], int pressedRow,
		int pressedArrow) {
	surface.fillRect(Common::Rect(0, kTop, 320, 200), 0);
	if (_icons.empty())
		return;

	Graphics::Surface *target = surface.surfacePtr();
	Sprite icons(_system, _icons);
	icons.drawFrame(0, target, 0, kTop);
	icons.drawFrame(3, target, 228, kTop);
	icons.drawFrame(14, target, kCommandLeft, kTop);
	icons.drawFrame(33, target, 255, 162);
	for (uint arrow = 0; arrow < 4; ++arrow) {
		if (!exits[arrow])
			continue; // Only the directions that lead somewhere are lit.
		const Common::Rect rect = arrowRect(arrow);
		if ((int)arrow == pressedArrow)
			surface.fillRect(rect, 0);
		icons.drawFrame(29 + arrow, target, rect.left, rect.top);
	}

	Common::String upperTitle(title);
	upperTitle.toUppercase(); // The original's panel text is upper case.
	const byte colour = brightestInterfaceColour();
	for (uint row = 0; row < kCommandRows; ++row) {
		const char *label = row == 0 ? upperTitle.c_str() : commandText(row);
		if (row == kCommandRows - 1 && !label)
			label = "MAIN MENU";
		if (!label)
			continue;
		const int y = kCommandTop + row * kCommandHeight;
		if ((int)row == pressedRow)
			icons.drawFrame(27, target, kCommandLeft, y);
		drawText(surface, label, kCommandLeft + (kCommandRight - kCommandLeft - textWidth(label)) / 2, y, colour);
	}
}

Panel::Action Panel::hitTest(int x, int y, int &row, int &arrow) const {
	row = arrow = -1;

	if (y < kTop) {
		if (x < 80)
			return kActionLeft;
		return x >= 240 ? kActionRight : kActionUp;
	}

	if (Common::Rect(24, 155, 70, 177).contains(x, y))
		return kActionMenu; // The book; it will open the real book later.

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
		if (hit == kCommandRows - 1)
			return kActionMenu;
		if (commandText(hit)) {
			row = hit;
			return kActionCommand;
		}
		return kActionNone;
	}

	return kActionNone;
}

} // namespace Dune

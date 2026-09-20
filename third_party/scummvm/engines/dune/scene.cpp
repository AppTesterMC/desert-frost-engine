/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#include "common/events.h"
#include "common/str.h"
#include "common/system.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/paletteman.h"

#include "dune/debug.h"
#include "dune/palace.h"
#include "dune/resource.h"
#include "dune/room.h"
#include "dune/scene.h"
#include "dune/sky.h"
#include "dune/sprite.h"

namespace Dune {

GameScreen::GameScreen(OSystem *system, Resource &resources, StartupLog &log) :
		_system(system), _resources(resources), _log(log), _panel(system, resources), _mode(kMenu),
		_menuSelection(0), _room(kPalaceFirstRoom) {
	_surface.create(320, 200, Graphics::PixelFormat::createFormatCLUT8());
}

GameScreen::~GameScreen() {
	_surface.free();
}

// ---- Placeholder main menu ---------------------------------------------

void GameScreen::showMenu() {
	_mode = kMenu;
	_menuSelection = 0;

	const byte palette[6 * 3] = {
		0, 0, 0,
		8, 12, 32,
		20, 28, 64,
		160, 120, 48,
		220, 190, 96,
		255, 255, 255
	};
	_system->getPaletteManager()->setPalette(palette, 0, 6);
	drawMenu();
}

void GameScreen::drawMenuText(const char *text, int y, uint32 colour) {
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (font)
		font->drawString(&_surface, text, 0, y, 320, colour, Graphics::kTextAlignCenter);
}

void GameScreen::drawMenu() {
	_surface.fillRect(Common::Rect(0, 0, 320, 200), 1);
	_surface.fillRect(Common::Rect(20, 12, 300, 188), 2);
	_surface.fillRect(Common::Rect(24, 16, 296, 184), 1);
	drawMenuText("DUNE", 27, 4);
	drawMenuText("MAIN MENU", 52, 5);

	static const char *const labels[kMenuItems] = { "START GAME", "LOAD GAME", "OPTIONS", "QUIT" };
	for (uint i = 0; i < kMenuItems; ++i) {
		const int top = kMenuTop + (int)i * kMenuItemHeight;
		if (i == _menuSelection)
			_surface.fillRect(Common::Rect(72, top, 248, top + 20), 3);
		drawMenuText(labels[i], top + 4, i == _menuSelection ? 1 : 5);
	}

	drawMenuText("UP/DOWN SELECT   RETURN ACCEPT", 174, 4);
	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
	dumpScreen(_system, "menu");
}

bool GameScreen::activateMenuItem() {
	switch (_menuSelection) {
	case 0:
		showPalaceRoom(kPalaceFirstRoom);
		return false;
	case 1:
		showStatus("Dune: load game is not implemented yet");
		_log.line("Menu: LOAD GAME selected (not implemented)");
		return false;
	case 2:
		showStatus("Dune: options are not implemented yet");
		_log.line("Menu: OPTIONS selected (not implemented)");
		return false;
	default:
		return true;
	}
}

// ---- Palace ---------------------------------------------------------------

void GameScreen::showPalaceRoom(uint number) {
	_mode = kPalace;
	_room = number;
	drawRoom();
	dumpScreen(_system, Common::String::format("room-%u", _room).c_str());
}

void GameScreen::drawRoom(int pressedRow, int pressedArrow) {
	const PalaceRoom &entry = palaceRoom(_room);
	const uint salRoom = palaceSalRoom(entry);
	const char *sheetName = palaceSheetName(entry);

	_surface.fillRect(Common::Rect(0, 0, 320, 200), 0);

	// Palette order matters: interface ranges first, then the room sheet,
	// which also owns 240-255 and so tints the panel.
	_panel.applyPalette();
	Common::Array<byte> roomData, sheetData;
	bool ok = _resources.load("PALACE.SAL", roomData) && _resources.load(sheetName, sheetData);
	if (ok) {
		// Two palace rooms look outside: the sky goes in first and the room is
		// drawn over it. It is always midday until the game clock exists.
		const uint kSalBalcony = 10, kSalStairs = 11;
		if (salRoom == kSalBalcony)
			drawSky(_system, _resources, *_surface.surfacePtr(), kSkyNarrow, 320, kSkyDay);
		else if (salRoom == kSalStairs)
			drawSky(_system, _resources, *_surface.surfacePtr(), kSkyLarge, 200, kSkyDay);

		Sprite sheet(_system, sheetData);
		Sprite characters(_system, _panel.characterSheet());
		Common::Array<uint16> markerSprites;
		if (_room == kPalaceFirstRoom)
			markerSprites.push_back(0); // PERS sprite 0 is Duke Leto.

		ok = sheet.setPalette();
		if (ok && (salRoom == 10 || salRoom == 11))
			// BALCON.HSQ frame 2 is the shared 320x152 backdrop. The SAL
			// records contain only its overlays and character markers.
			sheet.drawFrame(2, _surface.surfacePtr(), 0, 0);
		ok = ok && Room(roomData).draw(salRoom, sheet, *_surface.surfacePtr(),
				_panel.characterSheet().empty() ? nullptr : &characters, &markerSprites);
	}

	// Colour 0 is the black behind the view and the command box; several
	// room sheets set it to something else.
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);

	bool exits[4];
	for (uint direction = 0; direction < 4; ++direction)
		exits[direction] = palaceExit(_room, (Direction)direction) != 0;

	if (pressedRow < 0 && pressedArrow < 0)
		_log.line(Common::String::format("Room %u: PALACE.SAL #%u with %s (%s): %s", _room, salRoom, sheetName,
				entry.label, ok ? "drawn" : "FAILED"));

	// These are the first real room commands from COMMAND1.HSQ. The original
	// chooses a longer list from game state; keeping the resource indices here
	// makes the text/data path real while that state machine is still pending.
	static const uint16 throneCommands[4] = { 151, 119, 0xffff, 0xffff };
	static const uint16 noCommands[4] = { 0xffff, 0xffff, 0xffff, 0xffff };
	_panel.setCommandRows(_room == kPalaceFirstRoom ? throneCommands : noCommands, 4);
	_panel.draw(_surface, entry.label, exits, pressedRow, pressedArrow);

	_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, 320, 200);
	_system->updateScreen();
}

void GameScreen::panelAction(Panel::Action action, int row, int arrow) {
	if (action == Panel::kActionNone)
		return;
	if (action == Panel::kActionCommand) {
		const char *text = row >= 0 ? _panel.commandText(row) : nullptr;
		if (text) {
			showStatus(Common::String::format("Dune: %s", text).c_str());
			_log.line(Common::String::format("Command selected: %s", text));
		}
		return;
	}
	if (action == Panel::kActionMenu) {
		showMenu();
		return;
	}

	const uint target = palaceExit(_room, (Direction)(action - Panel::kActionUp));
	if (!target)
		return;

	if (row >= 0 || arrow >= 0) {
		// Show the pressed control for a moment, as the original does.
		drawRoom(row, arrow);
		_system->delayMillis(120);
	}
	showPalaceRoom(target);
}

// ---- Input ----------------------------------------------------------------

bool GameScreen::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
		return true;

	if (_mode == kMenu) {
		if (event.type == Common::EVENT_KEYDOWN) {
			switch (event.kbd.keycode) {
			case Common::KEYCODE_UP:
				_menuSelection = (_menuSelection + kMenuItems - 1) % kMenuItems;
				drawMenu();
				break;
			case Common::KEYCODE_DOWN:
				_menuSelection = (_menuSelection + 1) % kMenuItems;
				drawMenu();
				break;
			case Common::KEYCODE_RETURN:
			case Common::KEYCODE_KP_ENTER:
			case Common::KEYCODE_SPACE:
				return activateMenuItem();
			case Common::KEYCODE_ESCAPE:
				return true;
			default:
				break;
			}
		} else if (event.type == Common::EVENT_LBUTTONDOWN) {
			const int bottom = kMenuTop + kMenuItems * kMenuItemHeight;
			_log.line(Common::String::format("Tap: menu at (%d, %d)", event.mouse.x, event.mouse.y));
			if (event.mouse.y >= kMenuTop && event.mouse.y < bottom) {
				_menuSelection = (event.mouse.y - kMenuTop) / kMenuItemHeight;
				return activateMenuItem();
			}
		}
		return false;
	}

	if (event.type == Common::EVENT_KEYDOWN) {
		switch (event.kbd.keycode) {
		case Common::KEYCODE_ESCAPE:
			showMenu();
			break;
		case Common::KEYCODE_UP:
			panelAction(Panel::kActionUp, -1, -1);
			break;
		case Common::KEYCODE_RIGHT:
			panelAction(Panel::kActionRight, -1, -1);
			break;
		case Common::KEYCODE_DOWN:
			panelAction(Panel::kActionDown, -1, -1);
			break;
		case Common::KEYCODE_LEFT:
			panelAction(Panel::kActionLeft, -1, -1);
			break;
		default:
			break;
		}
	} else if (event.type == Common::EVENT_LBUTTONDOWN) {
		int row, arrow;
		const Panel::Action action = _panel.hitTest(event.mouse.x, event.mouse.y, row, arrow);
		// Taps are logged with what they hit: on a device this is the only
		// way to tell a touch-mapping problem from a hit-zone problem.
		static const char *const names[] = { "nothing", "up", "right", "down", "left", "menu", "command" };
		_log.line(Common::String::format("Tap: room %u at (%d, %d) -> %s", _room, event.mouse.x, event.mouse.y, names[action]));
		panelAction(action, row, arrow);
	}

	return false;
}

} // namespace Dune

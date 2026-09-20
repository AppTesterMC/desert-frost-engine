/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_SCENE_H
#define ENGINES_DUNE_SCENE_H

#include "common/scummsys.h"

#include "graphics/managed_surface.h"

#include "dune/panel.h"

class OSystem;

namespace Common {
struct Event;
}

namespace Dune {

class Resource;
class StartupLog;

/**
 * What the player sees after the intro: a placeholder main menu and the
 * palace, walked room by room through the control panel.
 *
 * Everything is composed into one 320x200 8-bit surface and copied to the
 * screen in a single call; the view occupies rows 0-151 and the Panel the
 * rest.
 *
 * Not the original's flow yet: the real game has no such menu (it starts in
 * the throne room), and rooms will eventually come from a general location
 * system rather than from palace.h alone.
 */
class GameScreen {
public:
	GameScreen(OSystem *system, Resource &resources, StartupLog &log);
	~GameScreen();

	void showMenu();

	/** Show a palace room directly (1-based; also used by dump runs). */
	void showPalaceRoom(uint number);

	/** @return true when the engine should return to the launcher */
	bool handleEvent(const Common::Event &event);

private:
	enum Mode {
		kMenu,
		kPalace
	};

	enum {
		kMenuItems = 4,
		kMenuTop = 68,
		kMenuItemHeight = 24
	};

	bool activateMenuItem();
	void drawMenu();
	void drawMenuText(const char *text, int y, uint32 colour);

	void drawRoom(int pressedRow = -1, int pressedArrow = -1);
	void panelAction(Panel::Action action, int row, int arrow);

	OSystem *_system;
	Resource &_resources;
	StartupLog &_log;
	Panel _panel;
	Graphics::ManagedSurface _surface;
	Mode _mode;
	uint _menuSelection;
	uint _room; ///< Current palace room, 1-based.
};

} // namespace Dune

#endif // ENGINES_DUNE_SCENE_H

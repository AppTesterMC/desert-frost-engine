/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_INTRO_H
#define ENGINES_DUNE_INTRO_H

#include "common/scummsys.h"

#include "graphics/managed_surface.h"

class OSystem;

namespace Dune {

class Resource;
class StartupLog;

/**
 * The CD release's intro is a list of HNM videos (VIRGIN, CRYO, CRYO2,
 * PRESENT, TITLE, IRULAN); a tap skips the current one.
 * @return false when the user asked to quit
 */
bool playCdIntro(OSystem *system, Resource &resources, StartupLog &log);

/**
 * The floppy release's intro.
 *
 * Apart from LOGO.HNM the floppy release has no intro videos: DUNEPRG.EXE
 * composes the sequence from sprite sheets under a script. We do not run
 * that script yet. The steps below are rebuilt from the intro of
 * codingstyle's swift-dune (a macOS reimplementation; used as a source of
 * facts only, it carries no licence) and checked against a recording of the
 * original (RPReplay_Final1789852776.mov in the repository root):
 *
 *   LOGO.HNM                     Cryo and Virgin logos            6.0 s
 *   INTDS 6, 7                   "VIRGIN GAMES / presents"        3.8 s
 *   INTDS 8, 9, 10               "A production from / CRYO ..."   5.5 s
 *   STARS 0-2, 3-35, 36, 37-44   pan across the stars to Arrakis  9.8 s
 *   SKY + INTDS 1, then 2-5      dunes scroll away, DUNE title   16.5 s
 *
 * Still to do, in order (swift-dune scene names): WormCall, Paul on red
 * (Background + Character), Sunrise, Chani, Liet, sietch rooms, DesertWalk,
 * palace stairs and balcony with the family, Baron, Feyd, Attack, Kiss,
 * ornithopter take-off and Flight; then the credits and the prologue.
 *
 * A tap or key skips the rest of the intro.
 */
class FloppyIntro {
public:
	FloppyIntro(OSystem *system, Resource &resources, StartupLog &log);
	~FloppyIntro();

	/** @return false when the user asked to quit */
	bool play();

private:
	enum {
		kViewTop = 24 ///< The intro shows the 320x152 view 24 rows down the screen.
	};

	struct Placement {
		uint16 sprite;
		int16 x, y; ///< Relative to the view.
	};

	bool loadSheet(const char *name, Common::Array<byte> &data);
	void applyPalette(uint level);
	void present();
	bool wait(uint32 millis);
	void fade(bool in);
	void card(const char *sheetName, const Placement *placements, uint count, uint32 hold, const char *dumpName);
	void starfield();
	void title();
	void wormCall();
	void characterScene(bool sunrise, const char *characterName, const char *dumpName, uint32 duration);

	OSystem *_system;
	Resource &_resources;
	StartupLog &_log;
	Graphics::ManagedSurface _surface;
	byte _palette[256 * 3]; ///< Target palette of the current step; fades scale it.
	bool _aborted, _quit;
};

} // namespace Dune

#endif // ENGINES_DUNE_INTRO_H

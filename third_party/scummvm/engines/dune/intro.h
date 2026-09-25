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

#ifndef ENGINES_DUNE_INTRO_H
#define ENGINES_DUNE_INTRO_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "graphics/managed_surface.h"

#include "dune/sky.h"

class OSystem;

namespace Dune {

class Music;
class Resource;
class Sprite;
class StartupLog;

/**
 * The CD release's recovered loader order is VIRGIN, CRYO, CRYO2, PRESENT,
 * then TITLE. It comes from the executable's play_intro table and load_*
 * entry points, not from a guessed filename list.
 * @return false when the user asked to quit
 */
bool playCdIntro(OSystem *system, Resource &resources, StartupLog &log);

/**
 * The floppy release's intro, credits and narrated prologue: everything
 * between the Cryo logo and the throne room.
 *
 * Apart from LOGO.HNM the floppy release composes the whole sequence from
 * sprite sheets. The scene list, sprite numbers, positions, durations and
 * transitions follow codingstyle's swift-dune (Game/Scenes/Intro.swift,
 * Prologue.swift and one file per scene), whose author allowed this reuse;
 * every scene was then checked against a recording of the original
 * (RPReplay_Final1789852776.mov, floppy release, logo to first map screen).
 * Where the two disagree the recording wins and the comment says so.
 *
 * Layout: the intro shows its 320x152 pictures 24 rows down the screen; the
 * prologue draws them at the top with the narration in the 48 rows below.
 *
 * Order (recording timestamps in seconds): logos 0-14, presents 14-24,
 * stars 24-36, dunes/title 36-50, worm 50-60, Paul 60-64, sunrise 64-72,
 * Chani 72-84, Liet 76-80, Paul in the desert 84-88, sietch 88-104, desert
 * walk 104, palace stairs and balcony 106-114, Paul 114, sunset fort 116-121,
 * Baron/Feyd 122-129, night attack 130-139, Paul 140, kiss 142-150,
 * ornithopter take-off 152-156, night flight 156-177, credits 178-191,
 * prologue 192-255, throne room 256.
 *
 * A tap or key skips the rest of the intro. On dump/harness runs each scene
 * renders one representative frame and moves on.
 */
class FloppyIntro {
public:
	FloppyIntro(OSystem *system, Resource &resources, StartupLog &log, Music *music = nullptr);
	~FloppyIntro();

	/** @return false when the user asked to quit */
	bool play();
	/**
	 * The CD's story after TITLE (intro_script entries 11-47): the scenes it
	 * shares with the floppy, in the CD's order, with the flyover to the
	 * palace (MTG1), the flight to the sietch (MTG2), the night flight
	 * (MTG3) and the plant growing in the desert (PLANT) in their places.
	 */
	bool playCdStory();

private:
	enum {
		kViewTop = 24,   ///< Intro pictures sit 24 rows down the screen.
		kViewHeight = 152
	};

	enum Transition {
		kNoTransition,
		kFade,     ///< palette scaled to and from black
		kDissolve, ///< the original's 4x4 pixel-pattern dissolve
		kPixelate, ///< (in) 16-pixel blocks sharpening to the picture
		kZoom      ///< (out) the picture magnifies into _zoomTarget
	};

	struct Placement {
		uint16 sprite;
		int16 x, y; ///< Relative to the view.
	};

	/** Characters are placed at a room's markers by PERS.HSQ frame number. */
	struct MarkerCharacter {
		uint marker;
		uint16 frame;
	};

	/**
	 * A character sprite sheet playing its authored animations the way the
	 * original's character node does: each queued animation runs for
	 * frames x 160 ms, the last one loops as the idle pose.
	 */
	struct CharacterPlayer {
		Sprite *sprite;
		Common::Array<uint16> queue;
		uint16 idle;
		int current;
		uint32 animationStart, animationDuration;
		int offsetX, offsetY;
	};

	// ---- infrastructure (intro.cpp) ----
	bool loadSheet(const char *name, Common::Array<byte> &data);
	bool playVideo(const char *name, uint top);
	void applyPalette(uint level);
	void present();
	bool wait(uint32 millis);
	void fade(bool in);
	void startSong(const char *name);
	void loadSubtitles();
	void drawSubtitle(uint16 sentenceNumber);
	int drawSubtitleLine(const Common::Array<Common::String> &words, uint first, uint last, int y, bool justify);
	int textWidth(const Common::String &text) const;
	/** The narration's sentence number for prologue step 0-7, resolved per release. */
	uint16 prologueSentence(uint step) const;

	/** What runScene() calls to draw one frame into the 320x152 view. */
	struct SceneRenderer {
		virtual ~SceneRenderer() {}
		virtual void render(uint32 sceneTime, Graphics::Surface &view) = 0;
	};
	template<typename RenderFn>
	struct LambdaRenderer : public SceneRenderer {
		RenderFn fn;
		explicit LambdaRenderer(RenderFn f) : fn(f) {}
		void render(uint32 sceneTime, Graphics::Surface &view) override { fn(sceneTime, view); }
	};

	/**
	 * Run one scene: the renderer is called with the scene time and the
	 * 320x152 view surface; transitions are applied around it, and the
	 * picture stands still while they run. Returns false when the intro was
	 * skipped or quit. On dump/harness runs the scene renders once at
	 * captureAt and writes dumpName.
	 */
	bool runSceneImpl(const char *dumpName, uint32 duration, Transition in, uint32 inMillis, Transition out,
			uint32 outMillis, uint32 captureAt, SceneRenderer &renderer);
	template<typename RenderFn>
	bool runScene(const char *dumpName, uint32 duration, Transition in, uint32 inMillis, Transition out,
			uint32 outMillis, uint32 captureAt, RenderFn render) {
		LambdaRenderer<RenderFn> renderer(render);
		return runSceneImpl(dumpName, duration, in, inMillis, out, outMillis, captureAt, renderer);
	}
	void dissolve(Graphics::Surface &view, uint clearedPerBlock);
	void pixelate(Graphics::Surface &view, uint blockSize);
	void setViewTop(int top) { _viewTop = top; }

	// ---- first half of the intro (intro_first.cpp) ----
	bool drawSunrise(Sprite &background, Graphics::Surface &view, bool village, bool fort);
	void logoSwap();
	void presentsScene(const char *dumpName, bool cryo);
	void starsScene(const char *dumpName);
	void titleScene(const char *dumpName);
	void wormScene(const char *dumpName);
	void sunriseScene(const char *dumpName, uint32 duration);
	void chaniCloseUpScene(const char *dumpName);
	void chaniZoomOutScene(const char *dumpName, uint32 duration);
	void sunriseCharacterScene(const char *dumpName, const char *characterName, bool village, uint32 duration);
	void desertPaulScene(const char *dumpName, uint32 duration);

	// ---- second half, credits and prologue (intro_scenes.cpp) ----
	bool drawSkyView(Graphics::Surface &view, SkyType type, int width, uint palette);
	void fillView(Graphics::Surface &view, const Common::Rect &rect, byte colour);
	bool drawRedBackdrop(Sprite &back, Graphics::Surface &view);
	bool drawBaronBackdrop(Sprite &back, Graphics::Surface &view);
	bool drawFeydBackdrop(Sprite &back, Graphics::Surface &view);
	void drawCharacter(CharacterPlayer &player, Graphics::Surface &view, uint32 elapsed);
	void markerTable(const MarkerCharacter *markers, uint markerCount, Common::Array<uint16> &table);
	void drawScaled(Sprite &sheet, uint16 frame, Graphics::Surface &view, int x, int y, uint percent);
	void drawWaterRipple(Graphics::Surface &view, int cx, int cy, int rx, int ry);
	void drawDuneField(Sprite &dunes, Sprite &dunes2, Graphics::Surface &view, bool kissLayout);

	void sietchScene(const char *dumpName, uint room, const MarkerCharacter *markers, uint markerCount,
			const char *foreground, uint32 duration, Transition in, uint32 inMillis, Transition out,
			uint32 outMillis);
	void palaceScene(const char *dumpName, uint room, const MarkerCharacter *markers, uint markerCount,
			const Common::Rect *zoom, const char *foreground, uint32 duration, Transition out, uint32 outMillis);
	void backdropCharacterScene(const char *dumpName, const char *backdrop, const char *characterName,
			const uint16 *animations, uint animationCount, bool sardaukar, uint32 duration,
			Transition in, uint32 inMillis, Transition out, uint32 outMillis, uint16 subtitle = 0);
	void sunsetFortScene(const char *dumpName, uint32 duration);
	void desertWalkScene(const char *dumpName, uint skyPalette, uint32 duration, Transition in, uint32 inMillis,
			Transition out, uint32 outMillis, uint16 subtitle = 0);
	void kissScene(const char *dumpName);
	void ornithopterScene(const char *dumpName);
	void flightScene(const char *dumpName, uint32 duration);
	void attackScene(const char *dumpName);
	void creditsScene(const char *dumpName);
	void prologueStarsScene(const char *dumpName, bool globe, uint16 subtitle);
	void prologueDesertScene(const char *dumpName, uint16 subtitle);
	void prologuePalaceScene(const char *dumpName);
	void prologue();

	OSystem *_system;
	Resource &_resources;
	StartupLog &_log;
	Music *_music;
	Graphics::ManagedSurface _surface;
	byte _palette[256 * 3]; ///< Target palette of the current step; fades scale it.
	int _viewTop;
	Common::Array<Common::String> _subtitles; ///< COMMAND1.HSQ records.
	uint16 _prologueBase;   ///< Record of "In these times of the future..."; 0 if not found.
	byte _subtitleColour[3]; ///< STARS.HSQ colour 25, the narration's orange.
	Common::Array<byte> _font; ///< DNCHAR.BIN or DUNECHAR.HSQ for intro captions.
	bool _aborted, _quit;
	Common::Rect _zoomTarget;             ///< Where a kZoom transition ends.
	int _sceneNumber;                     ///< Numbered as the log reports them.
	uint32 _introStart, _lastTimedDump;   ///< For dumpEveryMillis().
};

} // namespace Dune

#endif // ENGINES_DUNE_INTRO_H

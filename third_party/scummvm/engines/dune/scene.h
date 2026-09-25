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

#ifndef ENGINES_DUNE_SCENE_H
#define ENGINES_DUNE_SCENE_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "graphics/managed_surface.h"

#include "dune/dialogue.h"
#include "dune/hnm.h"
#include "dune/map.h"
#include "dune/panel.h"
#include "dune/world.h"

class OSystem;

namespace Common {
struct Event;
}

namespace Dune {

class Book;
class Music;
class Resource;
class SaveGame;
class SentenceBank;
class Sprite;
class StartupLog;

/** Render the map screen: FRESK frame and the MAP/GLOBDATA globe. */
bool drawDuneGlobe(OSystem *system, Graphics::Surface &surface, Resource &resources,
		uint16 rotation = 0, int tilt = 0);
/**
 * Render only the globe, centred at (159, 79), with whatever palette is in
 * place: the prologue draws it over the star field, whose palette entries
 * 16-45 (the sun's yellows) give the planet its flat golden look.
 */
bool drawGlobeSphere(OSystem *system, Graphics::Surface &surface, Resource &resources, uint16 rotation, int tilt);

/**
 * What the player sees after the intro: the places of Arrakis walked room
 * by room through the control panel (the palace, the sietches, villages
 * and fortresses of the World), the characters found there and their
 * conversations, the flat map and the globe, and the book.
 *
 * Everything is composed into one 320x200 8-bit surface and copied to the
 * screen in a single call; the view occupies rows 0-151 and the Panel the
 * rest. There is no start menu, as in the original: the game lands in the
 * throne room.
 */
class GameScreen {
public:
	GameScreen(OSystem *system, Resource &resources, StartupLog &log);
	~GameScreen();

	/** A new game: the executable's initial data, then the throne room. */
	void startNewGame();
	/** Show a room of the current place (1-based in its room table; also used by dump runs). */
	void showRoom(uint number);
	/** Kept for the palace dumps: the palace rooms are numbered as in palace.h. */
	void showPalaceRoom(uint number) { showRoom(number); }
	/** Fly to a place and land in its entrance. */
	void travelTo(uint locationIndex);
	/** The flat map (optionally choosing a destination) or the globe. */
	void openMap(MapScreen::Mode mode, bool selectDestination);
	bool inMap() const { return _mode == kMap; }
	/** Tap the map at a view position (dump runs pick destinations this way). */
	void mapTap(int x, int y);

	/** Talk to a character (DIALOGUE.HSQ group number: 0 Duke Leto, 1 Jessica, ...). */
	void startConversation(uint character);
	/** Next page or line of the conversation; ends it when nothing is left. */
	void advanceConversation();
	bool inConversation() const { return _mode == kTalk; }
	/** A line is on screen (false once only the verbs remain). */
	bool talking() const { return _mode == kTalk && !_talkEnded; }
	/** LOOK AT MIRROR (dump runs). */
	void lookInMirror() { openMirror(); }
	/** STOP TALKING. */
	void stopTalking() { endConversation(); }

	enum BookAction {
		kBookTopic,
		kBookNext,
		kBookPrevious
	};

	/** The book behind the panel's icon: cover, then the pages of the chosen topic. */
	void openBook();
	void bookAction(BookAction action, int row = 0);
	bool inBook() const { return _mode == kBook; }

	/** The game menu lives in the globe's command box (swift-dune Fresk.swift). */
	enum Menu {
		kMenuNone,    ///< EXIT GLOBE, SEE MAP OF THIS AREA, SAVE GAME, LOAD GAME, OPTIONS & QUIT GAME
		kMenuSave,    ///< the four logs
		kMenuLoad,
		kMenuOptions, ///< music, RESTART GAME, EXIT GAME
		kMenuQuit     ///< YES I WANT TO EXIT GAME / NO I DON'T WANT TO FINISH
	};
	void openMenu(Menu menu);
	/** Write or read one of the original's four save files (SaveGame). */
	bool saveSlot(uint slot);
	bool loadSlot(uint slot);
	/** The music the options row switches; the song restarts when it is turned back on. */
	void setMusic(Music *music, bool playing) { _music = music; _musicOn = playing; }
	/** EXIT GAME was confirmed: the engine returns to the launcher. */
	bool quitRequested() const { return _quitRequested; }
	/** Days on Dune, from the game clock (sixteen slots a day). */
	uint day() const;
	/**
	 * Dump runs: the sietch's Fremen, talking to them (the troop is hired),
	 * the chief's orders and a new occupation, a day passing, the spice
	 * density and the results (troop, troop-ordered, map-density, results).
	 */
	void dumpGameplay();
	/** Dump runs: the scripted scenes, the first vision, the COMM room and Duncan's bargaining (story_scene.cpp). */
	void dumpStory();
	void dumpStillsuit();
	/**
	 * Developer key dune_story_setup (the story regression scenario): "comm"
	 * = after the first vision, in the COMM room with Duncan and 2000 kg in
	 * stock; "gathering" = the COMM room gathering scene (phase 0x0c).
	 */
	void storySetup(const Common::String &what);
	void prepareEcologyTest(bool showMap);
	/** Dump runs: a whole conversation with a character present. */
	void talkThrough(uint character);

	/** Once per frame: drives the talking animation. */
	void update();

	/** @return true when the engine should return to the launcher */
	bool handleEvent(const Common::Event &event);

	World &world() { return _world; }

private:
	enum Mode {
		kRoom,
		kMap,
		kTalk,
		kBook,
		kTroop, ///< a troop's orders: its figures in a box over the room or the map
		kMirror ///< the palace bedroom's mirror: Paul's reflection and the game menu
	};

	enum {
		kParagraphWidth = 126 ///< Text width inside the command box.
	};

	/** What a command row does in the current screen. */
	enum RowAction {
		kRowNone,
		kRowMap,        ///< SEE DUNE MAP
		kRowTalk,       ///< a character (rowArgument = character)
		kRowOrnithopter,///< TAKE AN ORNITHOPTER: pick a destination on the map
		kRowExitMap,    ///< EXIT MAPS / EXIT GLOBE
		kRowFly,        ///< GO THERE FLYING AN ORNI
		kRowFlatMap,    ///< SEE MAP OF THIS AREA (from the globe)
		kRowSaveMenu,   ///< SAVE GAME
		kRowLoadMenu,   ///< LOAD GAME
		kRowOptionsMenu,///< OPTIONS & QUIT GAME
		kRowSaveSlot,   ///< a log (rowArgument = slot)
		kRowLoadSlot,
		kRowMusic,      ///< MUSIC OFF / MUSIC ON
		kRowRestart,    ///< RESTART GAME
		kRowExitGame,   ///< EXIT GAME (asks first)
		kRowConfirmExit,
		kRowCancelExit,
		kRowResults,    ///< SEE RESULTS / STANDARD VISION on the globe
		kRowDensity,    ///< SEE SPICE DENSITY / STANDARD VISION on the map
		kRowOrders,     ///< GIVE ORDERS TO TROOP (map)
		kRowContact,    ///< CONTACT FREMEN TROOPS (map): the next rallied troop
		kRowTroopTalk,  ///< " TALK TO ME " (troop chief)
		kRowTroopOccupation, ///< CHANGE TROOP OCCUPATION
		kRowSetOccupation,   ///< an occupation (rowArgument)
		kRowTroopDone,  ///< NO MORE ORDERS / Cancel
		kRowComeWithMe, ///< " COME WITH ME " to an unrallied troop's leader
		kRowStopTalking,
		kRowTalkMore,   ///< " TALK TO ME "
		kRowWorkForMe,  ///< " WORK FOR ME " (an unrallied troop's Fremen)
		kRowGiveOrders, ///< GIVE ORDERS TO TROOP (a rallied troop's chief)
		kRowCompanion,  ///< " COME WITH ME " / " STAY HERE " (characters)
		kRowAskMore,    ///< ASK FOR MORE INFORMATION (troop contact)
		kRowMirror,     ///< LOOK AT MIRROR (palace bedroom)
		kRowMirrorAway, ///< Look away from the mirror
		kRowBargain,    ///< ARGUE / ACCEPT / REFUSE (rowArgument = the ds:9f value)
		kRowWhat,       ///< " WHAT ? ": the line again
		kRowContinue,   ///< " Continue..." (scripted scenes)
		kRowViewed,     ///< " Viewed" (a COMM message)
		kRowCommNew,    ///< VIEW NEW MESSAGES
		kRowCommSeen,   ///< Messages already seen
		kRowCommPick,   ///< a sender in the COMM list (rowArgument = the message index)
		kRowCommCancel, ///< "  Cancel" under the COMM list
		kRowWait,       ///< WAIT FOR EVENING / WAIT FOR MORNING (the desert)
		kRowWorm,       ///< CALL A WORM (not built)
		kRowProspectors,///< FIND PROSPECTORS (map)
		kRowEquipment,  ///< MODIFY EQUIPMENT (troop contact)
		kRowEquipDone,  ///< "  Done" under the equipment panel
		kRowStatus      ///< anything not implemented: shows its name
	};

	void composeView();
	bool drawVideoBackdrop(byte placeType);
	void drawRoom(int pressedRow = -1, int pressedArrow = -1);
	void refreshRooms();
	const RoomRecord *currentRoom() const;
	void setRows(const RowAction *actions, const int *arguments, const uint16 *commands, uint count);
	void drawSceneText(const char *text, int y, uint32 colour);
	void panelAction(Panel::Action action, int row, int arrow);
	void drawMapScreen();
	void leaveMap();
	/** Fly to a place; returns where the flight ended (a sietch found on the way may be chosen). */
	uint flyTo(uint locationIndex);
	/** Fly toward a map position (a place, or the desert when @p target < 0). */
	uint flyToward(uint16 targetLongitude, int16 targetLatitude, int target);
	/** Fly toward a desert point picked on the map, then land at the nearest known place. */
	void travelToward(uint16 longitude, int16 latitude);
	/** Ornithopters parked on the place's pad (location byte 21; Paul's at the palace). */
	uint parkedOrnis() const;

	// ---- The story's screens (story_scene.cpp) ----
	enum TalkKind {
		kTalkNormal,
		kTalkScene,  ///< a line of a scripted scene: " Continue..." / WHAT ?
		kTalkComm,   ///< a COMM message: " Viewed" / WHAT ?
		kTalkVision  ///< a vision message (the sender speaks, or the dream)
	};
	void openTalk(uint character);
	/** Present @p character's list @p list, one line, with @p speaker's portrait. */
	void presentLine(uint speaker, uint character, uint list, byte mask, TalkKind kind);
	/** The scripted scene at CD code offset @p cdOffset (seg000:11771), once no dialogue runs. */
	void startScene(uint16 cdOffset);
	void sceneStep();
	void endScene();
	bool maybeStartScene();
	void openComm(bool seen);
	void updateRoomVars();
	/** room_person_present_auto_dialogue over the people here (seg000:35b4); true when one speaks. */
	bool roomEntryScan();
	void landInDesert();
	void drawDesert();
	void drawKiss();
	void checkIdle(uint32 now);
	void presentVision(bool dream);
	void emperorEnding();
	void drawEnding();
	void findProspectors();
	static const char *characterName(uint who);
	void addRoomRows(RowAction *actions, int *arguments, uint16 *commands, bool *greyed, uint &count, bool canLeave);
	enum {
		kDesertLanding = 0xffff, ///< flyToward's answer for a landing in the open desert
		kShotDown = 0xfffe       ///< ... for an ornithopter shot down over Harkonnen land
	};
	bool askHostileZone();
	// The CD's flight view (travel_select_flight_video, seg000:4ec6): MNT1
	// sand, MNT2 sand to rock, MNT3 rock, MNT4 rock to sand, switched at the
	// clips' ends by the terrain ahead; and the approach clip on arrival.
	HnmPlayer *_flightVideo = nullptr;
	Common::Array<byte> _mntData[4];
	int _mntClip = -1;
	Common::String _pendingFlightDump; ///< dump runs: a flight frame to write once on screen
	uint32 _mntNextFrame = 0;
	bool startCdFlightView();
	void drawCdFlightView(byte terrainAhead);
	void stopCdFlightView();
	void setVideoSkyPalette();
	void playArrivalVideo(byte placeType);
	bool arrivalIsFatal(uint place);
	void showFinal(uint picture);
	uint _finalPicture = 0;         ///< the final scene's FINAL.HSQ picture on screen (1, 2), 0 none
	TalkKind _talkKind = kTalkNormal;
	bool _talkBargain = false;      ///< the bargaining menu is up (events 4, 5)
	Common::String _talkLastPage;   ///< for WHAT ?
	Common::Array<byte> _scene;     ///< the running scripted scene's bytes
	uint _sceneCursor = 0;
	bool _sceneActive = false;
	uint16 _pendingScene = 0;
	uint _sceneReturnRoom = 0;
	Common::Array<byte> _cast;      ///< the scene's explicit cast (slot -> person, 0xff empty)
	uint _sceneKiss = 0;            ///< CHANKISS over the shot: 1 sprite 0 (scene byte 0x0a), 2 sprite 1 (0x0c)
	int _commList = -1;             ///< the COMM list on the panel: 0 new, 1 seen, -1 none
	bool _desert = false;           ///< Paul stands in the open desert (current_scene 0xff)
	bool _visionDream = false;
	bool _ending = false;           ///< an ending text is up
	Common::String _endingText = "As Paul Atreides failed"; ///< the ending's COMMAND, found by its start
	const char *_pendingEnding = nullptr; ///< an ending waiting for the talk to close
	uint32 _idleStart = 0;
	bool _troopEquipment = false;   ///< the MODIFY EQUIPMENT panel is up (seg000:7cbb)
	Common::Rect _equipRects[2][7]; ///< hit zones: the troop's items, the place's free ones
	void drawEquipmentPanel(const Common::Rect &area);
	bool equipmentTap(int x, int y);
	/** A rallied Fremen troop stationed at @p location. */
	bool hiredTroopAt(uint location) const;
	/** The next rallied Fremen troop id after @p after, cycling (0 none). */
	uint nextRalliedTroop(uint after) const;
	/** The last troop CONTACT FREMEN TROOPS reached (data_01955), 0 none. */
	uint _lastContacted = 0;
	/** draw_orni (seg000:3aa9): ORNYTK parts at a pad slot, @p frame 0 parked .. 0x21 gone. */
	void drawOrni(Graphics::Surface &target, int x, int y, uint frame);
	void drawParkedOrnis(Graphics::Surface &target, uint skip);
	/** orni_anim_loop (seg000:47fb): take-off (+1) or landing (-1) over the current room. */
	void animateOrni(int step);
	void openMirror();
	void drawMirror();
	bool askFlightStop(uint sietch);
	void drawResults();
	void openTroop(uint troopId, bool fromMap);
	void drawTroop();
	void drawInfoBox(const Common::Array<Common::String> &lines);
	uint skyPalette() const;
	void passTime(uint slots);
	bool ensureSaves();
	Common::String slotLabel(uint slot) const;
	void toggleMusic();

	bool loadDialogue();
	void closeBook();
	void drawBook();
	void endConversation();
	void startTalkAnimation();
	void drawTalk();
	void setTalkRows();
	void drawBubble(const Common::Array<Common::String> &lines, uint first, uint count, const Common::Rect &box, byte ink);
	uint bubbleLines() const;
	void presentVerb(uint list);
	/** set_game_phase_and_trigger_callbacks (seg000:121f). */
	void setGamePhase(byte phase);
	void applyStory();
	/** run_game_phase_triggers (seg000:b17a): the first matching line of DIALOGUE 16 list 7 fires its event. */
	void runPhaseTriggers();
	static void storyEvent(void *context, byte event, bool wasSaid, uint speaker);
	void stageTroopForConditions(uint troopId);
	bool nextTroopLine();

	OSystem *_system;
	Resource &_resources;
	StartupLog &_log;
	mutable Panel _panel;
	Graphics::ManagedSurface _surface;
	Mode _mode;
	uint _room;    ///< Current room, 1-based in the place's table.
	bool _viewOk;  ///< The last composeView() found its resources.

	GameState _state;
	World _world;
	Common::Array<RoomRecord> _rooms; ///< The current place's room table.
	SentenceBank *_sentences;
	Dialogue *_dialogue;
	Conditions *_conditions;
	Conversation *_conversation;
	Book *_book;
	MapScreen *_map;
	SaveGame *_saves;
	Menu _menu;
	uint16 _menuStatus; ///< COMMAND id shown on the last row after a save (0xffff: none).
	Music *_music;
	bool _musicOn;
	bool _quitRequested;

	uint _troopId;       ///< the troop whose orders are shown
	bool _troopFromMap;
	bool _troopChoosing; ///< the occupation list instead of the orders
	bool _recruiting;    ///< COME WITH ME / STOP TALKING for an unrallied troop's leader
	bool _troopFromRoom; ///< the contact came from a chief in the room: NO MORE ORDERS returns there
	uint _hireTroop;     ///< troop to hire when the conversation with the Fremen ends
	uint32 _clockStart;  ///< real time of the last clock slot
	byte _pendingPhase = 0;       ///< a chapter a spoken line asked for (applied between lines)
	bool _pendingTriggers = false;
	uint _orniFrame = 0; ///< orni_anim_frame: 0 parked, 0xff hidden (during take-off)

	// The CD's exterior backdrop: the last picture of the place's arrival video.
	Common::String _backdropName;
	Common::Array<byte> _backdrop;
	byte _backdropPalette[256 * 3];

	RowAction _rowActions[Panel::kCommandRows];
	int _rowArguments[Panel::kCommandRows];

	Sprite *_talkSheet;
	Common::Array<Common::String> _talkLines;
	uint _talkWho;       ///< the DIALOGUE group Paul talks to
	uint _talkIdle;      ///< the head's idle animation (a Fremen's expression)
	bool _talkEnded;     ///< the lines are out: only the verbs remain (the room unzoomed)
	uint _talkRecruit;   ///< troop whose WORK FOR ME answer is being shown (0: none)
	bool _talkRecruitOk;
	Common::Point _personPos[24]; ///< top-left of each person's marker in the current room (-1: absent)
	Common::String _troopLine;    ///< the troop contact popup's current line
	uint _talkLine;      ///< First line of _talkLines on screen.
	uint _talkPage;      ///< Pages shown so far (dump names, animation choice).
	uint _talkAnimation;
	uint _talkFrame;
	uint32 _talkStart;
	bool _talkAnimating;
};

} // namespace Dune

#endif // ENGINES_DUNE_SCENE_H

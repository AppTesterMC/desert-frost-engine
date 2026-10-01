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
class NightAttack;
class Sprite;
class StartupLog;

/**
 * The globe's colour for a map cell (bits 0-3 terrain, 4-5 stage): the VGA
 * driver's STANDARD VISION or SEE RESULTS path (floppy DUNEVGA 1D26/1DA3,
 * CD DNVGA 1E4C/1EC9; notes/globe-results.md).
 */
byte globeCellColour(byte cell, bool results);
/** Render the map screen: FRESK frame and the MAP/GLOBDATA globe. */
bool drawDuneGlobe(OSystem *system, Graphics::Surface &surface, Resource &resources,
		const Common::Array<byte> &map, uint16 rotation, int tilt, uint results, const Location &player,
		bool resultsColours);
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
	void openMap(MapScreen::Mode mode, bool selectDestination, bool fromFlatView = false);
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
		kMenuSave,    ///< the two manual logs and Cancel
		kMenuLoad,
		kMenuOptions, ///< three music choices, EXIT GAME, Cancel
		kMenuQuit,    ///< YES I WANT TO EXIT GAME / NO I DON'T WANT TO FINISH
		kMenuMusicOrder ///< STANDARD ORDER / SHUFFLE / Cancel
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
	/** Dump runs: the walk out of the palace into the desert (desert.cpp). */
	void dumpDesertWalk();
	/** Dump runs: the orni cockpit's destination screen, then a flight from it (cockpit.cpp). */
	void dumpCockpit();
	/**
	 * Developer key dune_test_cockpit (a scripted real-time test): 1 the palace
	 * front and TAKE AN ORNITHOPTER; 2 Gurney aboard on a free flight toward
	 * the nearest hidden place, made findable, for the sighting.
	 */
	void testCockpit(int mode);
	/**
	 * Developer key dune_story_setup (the story regression scenario): "comm"
	 * = after the first vision, in the COMM room with Duncan and 2000 kg in
	 * stock; "gathering" = the COMM room gathering scene (phase 0x0c).
	 */
	void storySetup(const Common::String &what);
	void prepareEcologyTest(bool showMap);
	/** Dump runs: a whole conversation with a character present. */
	void talkThrough(uint character);
	/** COME WITH ME / STAY HERE to the person Paul talks to (seg000:95e2, 9533). */
	void companionVerb();
	/** " WORK FOR ME " to the Fremen of an unrallied troop (seg000:95c1). */
	void workForMe();
	/** An ACCEPT (1) / REFUSE (2) / ARGUE (3) answer to the question on screen. */
	void answerQuestion(byte choice);
	/** The speedrun check (dune_speedrun=campaign|full): logs each route milestone. */
	void speedrun(const Common::String &part);

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
		kParagraphWidth = 126, ///< Text width inside the command box.
		kHeadStepMillis = 40   ///< the head's frame step: wait_a_bit(8), 8 ticks of 5 ms
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
		kRowMenuBack,   ///< Cancel to the game menu on the globe or mirror
		kRowSaveSlot,   ///< a log (rowArgument = slot)
		kRowLoadSlot,
		kRowMusic,      ///< MUSIC OFF / MUSIC ON
		kRowMusicOrderMenu, ///< MUSIC ON (CD-STYLE)
		kRowMusicOrder, ///< STANDARD ORDER / SHUFFLE
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
		kRowOverpower,  ///< " OVERPOWER THE PRISONER " (the Harkonnen captain, CD 9584)
		kRowGiveOrders, ///< GIVE ORDERS TO TROOP (a rallied troop's chief)
		kRowCompanion,  ///< " COME WITH ME " / " STAY HERE " (characters)
		kRowAskMore,    ///< ASK FOR MORE INFORMATION (troop contact)
		kRowMirror,     ///< LOOK AT MIRROR (palace bedroom)
		kRowMixer,      ///< Mixer Panel (CD rooms; the panel is not built)
		kRowOthers,     ///< "  Others...": the next page of rows, or the first (CD d45d)
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
		kRowWorm,       ///< CALL A WORM (seg000:42d1): the map, choosing where the worm goes
		kRowWormTravel, ///< GO THERE RIDING A WORM (seg000:50ea)
		kRowMassiveAttack, ///< MASSIVE ATTACK (seg000:7317)
		kRowFightDay,   ///< FIGHT FOR A WHOLE DAY (seg000:0fc5)
		kRowEspionage,  ///< ESPIONAGE (seg000:6a45)
		kRowAttack,     ///< ATTACK, for a troop on espionage (seg000:6a2f)
		kRowSearchEquipment, ///< GO & SEARCH FOR EQUIPMENT (seg000:7734 / 775c / 776d)
		kRowMoveTroop,  ///< MOVE TROOP (seg000:8064): the map, choosing where
		kRowMoveDone,   ///< "  Done" once the troop's destination is chosen (seg000:8214)
		kRowPickCancel, ///< Cancel while a move order is picked (loc_0824d)
		kRowPickAdd,    ///< ADD A DESTINATION (the prospectors; the next pick appends)
		kRowPickNew,    ///< GIVE NEW DESTINATIONS (clears the working queue)
		kRowPickDone,   ///< Done (the prospectors' queue as it stands)
		kRowProspectors,///< FIND PROSPECTORS (map)
		kRowEquipment,  ///< MODIFY EQUIPMENT (troop contact)
		kRowEquipDone,  ///< "  Done" under the equipment panel
		kRowCockpitCancel, ///< "  Cancel" on the orni cockpit (menu_multiple_cancel)
		kRowPlanDone,   ///< "Done" under the palace plan (menu ds:2012)
		kRowStatus      ///< anything not implemented: shows its name
	};

	void composeView();
	bool drawVideoBackdrop(byte placeType);
	bool drawBackdropStill(byte placeType);
	Common::String _lastBackdrop; ///< logged when it changes
	void drawRoom(int pressedRow = -1, int pressedArrow = -1);
	/** The navigation panel's layout and lit exits for the room (floppy seg000:329F). */
	void roomNav(bool exits[4], bool &canLeave);
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
	/** Start the script at @p cdOffset; @p holdLine keeps its triggering line until Continue. */
	void startScene(uint16 cdOffset, bool holdLine = false);
	void sceneStep();
	void endScene();
	bool maybeStartScene();
	void openComm(bool seen);
	/** init_room_persons at a village (seg000:3166): stage its smugglers (seg000:2318). */
	void stageVillageSmugglers();
	void updateRoomVars();
	/** room_person_present_auto_dialogue over the people here (seg000:35b4); true when one speaks. */
	bool roomEntryScan(bool always = false);
	void landInDesert();
	void drawDesert();
	// ---- The walk out into the desert (desert.cpp, notes/desert-walk-spec.md) ----
	/** Leave the place on foot through exit @p exit (0xFB-0xFF; floppy 422C). */
	void walkOut(byte exit);
	/** One arrow in the desert (1 north, 2 east, 3 south, 4 west; floppy 424F). */
	void desertStep(uint direction, bool counted);
	/** The sun's glare and the faint (floppy 39C2). */
	void thirstCheck();
	/** desert_collapse_cutscene (CD 0e77): DEAD3.HNM with the slow dissolve. */
	void desertCollapse();
	void drawWalkView();
	/** The landscape of build_landscape / project_and_draw (floppy 57FA, 5A8D) into @p view. */
	void drawLandscape(Graphics::Surface &view, uint16 longitude, int16 latitude, byte fine, uint16 key, bool inPlace);
	uint16 landRandom();
	uint landPick(byte terrain);
	void drawLandObject(Graphics::Surface &view, const Common::Array<byte> &sheet, uint sprite, uint z, int x, uint height);
	// The flight's landscape: the same objects, 4 a row, nearing one step a frame.
	struct LandObject {
		int16 z;
		int16 x;
		uint16 sprite;
	};
	Common::Array<LandObject> _flightObjects;
	byte _flightTerrain = 0;
	uint32 _flightFrameAt = 0;
	uint _flightTicks = 0; ///< landscape frames since the last step (ds:4286)
	void flightLandscapeStart(const uint16 *longitudes, const int16 *latitudes);
	void flightLandscapeReseed(uint16 longitude, int16 latitude);
	void flightLandscapeRow(uint z);
	void flightLandscapeTick();
	void flightLandscapePan(int direction);
	void flightLandscapeDraw(Graphics::Surface &view, const Common::Array<byte> &dunes);
	bool _walking = false;  ///< Paul walked out on foot (no ornithopter beside him)
	uint16 _walkLng = 0;    ///< ds:4 in the desert: the longitude
	int16 _walkLat = 0;     ///< ds:6 low byte: the latitude row
	byte _walkFine = 0;     ///< ds:6 high byte: 1/256 of a row
	byte _walkSteps = 0;    ///< the step counter (floppy ds:4291)
	int _walkFrom = -1;     ///< last_location_ptr: the place walked out of
	uint16 _landSeed = 0;   ///< ds:20E3
	// ---- The orni cockpit's destination screen (cockpit.cpp, notes/orni-cockpit-spec.md) ----
	void openCockpit(bool changing);
	void drawCockpit();
	void cockpitHover(int x, int y);
	int cockpitArrow(int x, int y) const;
	void cockpitTap(int x, int y);
	void cockpitCancel();
	void updateCockpit(uint32 now);
	bool cockpitPlayer(int &x, int &y) const;
	bool _cockpit = false;          ///< the map is shown in the cockpit's window
	bool _cockpitChanging = false;  ///< opened by CHANGE DESTINATION during a flight
	uint32 _cockpitStart = 0;
	uint32 _cockpitDrawn = 0;
	Common::Point _cockpitPointer = Common::Point(-1, -1);
	Common::String _cockpitLabel;
	// A companion sights a place (notes/orni-flight-spec.md 7): the cabin, the line, GO TOWARDS THIS PLACE.
	bool _cabinView = false;
	int _linePlace = -1;  ///< ds:47e6: the place a line names (a message, the ill place)
	int _insetPlace = -1; ///< the map window of action 13 on that place (CD a28e)
	bool _cabinWarning = false;     ///< the cabin shows the Harkonnen-zone warning's rows, not GO TOWARDS THIS PLACE
	void drawCabin();
	void showSighting(uint place, byte relativeBearing);
	/** travel_pick_speaking_companion (CD 366f, floppy 3908): the companion, -1 nobody aboard, -2 no line (slot 0 empty). */
	int flyoverSpeaker() const;
	/** The fly-over line (CD 96d8, floppy a195): block 16, list 4, the first line whose condition holds with ds:23 = @p action. */
	bool flyoverLine(byte action, Common::String &line);
	uint16 _flightLng = 0;          ///< the flight's position when CHANGE DESTINATION opens the cockpit
	int16 _flightLat = 0;
	int _changeTarget = -1;         ///< CHANGE DESTINATION's pick: a place, -2 a desert point, -1 none
	uint16 _changeLongitude = 0;
	int16 _changeLatitude = 0;
	bool _landAtSet = false; ///< travelToward set the landing point for landInDesert()
	void drawKiss();
	void checkIdle(uint32 now);
	void presentVision(bool dream);
	void emperorEnding();
	void waterOfLifeWakeUp();
	void waterOfLifeSetup();
	void ecologyWinSetup();
	void firstVisionForSetup();
	void endlessPlaySetup(bool withPaul);
	void epidemicSetup();  ///< dune_story_setup=epidemic (scripts/check_epidemic.sh)
	void chaniSetup(bool gurney); ///< dune_story_setup=chani / chani-gurney (scripts/check_chani.sh)
	/** Story setups that check a death or the ending keep those rules in a capture run. */
	static void forceRules(bool on);
	void drawEnding();
	void findProspectors();
	static const char *characterName(uint who);
	void addRoomRows(RowAction *actions, int *arguments, uint16 *commands, bool *greyed, uint &count, bool canLeave);
	enum {
		kDesertLanding = 0xffff, ///< flyToward's answer for a landing in the open desert
		kShotDown = 0xfffe       ///< ... for an ornithopter shot down over Harkonnen land
	};
	/** pending_room_action 4 (CD 35e9/3637, floppy 3889/38d7): true for CHANGE DESTINATION, false for IGNORE WARNING. */
	bool askHostileZone();
	// The CD's flight view (travel_select_flight_video, seg000:4ec6): MNT1
	// sand, MNT2 sand to rock, MNT3 rock, MNT4 rock to sand, switched at the
	// clips' ends by the terrain ahead; and the approach clip on arrival.
	HnmPlayer *_flightVideo = nullptr;
	Common::Array<byte> _mntData[4];
	// The worm ride (worm.cpp): VER.BIN's view rect, sprite lists and frame
	// script (CD 4285 over cs:015f, floppy cs:44d9), DFL2.HNM on the CD.
	struct WormPiece {
		uint16 sprite;
		int16 dx, dy;
	};
	struct WormAnim {
		Common::Rect rect;
		Common::Array<Common::Array<WormPiece> > lists;
		Common::Array<byte> script;
		uint cursor = 0;
		bool loaded = false;
	} _wormAnim;
	Common::Array<byte> _dflData;
	bool _wormMap = false;          ///< the destination map was opened by CALL A WORM (no cockpit)
	Common::Array<byte> _wormBackdrop; ///< the view under the worm's map (320 x 152)
	byte _wormBackdropPalette[256 * 3];
	bool _wormToggle = false;       ///< floppy: ror [2937], the landscape moves every second frame
	bool wormAnimLoad();
	void wormRideSetup();
	/** One frame of the VER.BIN script over @p view; @p advance moves the script on. */
	void wormAnimFrame(Graphics::Surface &view, bool advance);
	void wormAnimAdvance();
	void wormDeparture();
	bool startCdWormView();
	void dottedColumnsPresent(const byte *pixels, const byte *palette);
	int _mntClip = -1;
	/** The last flight ended by SKIP TO DESTINATION (CD 4ffb jumps to loc_4fc3, past
	 *  "ds:4732 = ds:11C9 & 1" at loc_4fb0): the scene reload (2dfb) then skips 488a. */
	bool _flightSkipped = false;
	Common::String _pendingFlightDump; ///< dump runs: a flight frame to write once on screen
	uint32 _mntNextFrame = 0;
	bool startCdFlightView();
	void drawCdFlightView(byte terrainAhead);
	void stopCdFlightView();
	void setVideoSkyPalette();
	void playArrivalVideo(byte placeType);
	bool arrivalIsFatal(uint place);
	/** ds:2b: Paul stands in a battle (the night-attack screen, seg000:503c). */
	bool _battle = false;
	/** The worm carries Paul on the next journey (travel mode 2, seg000:4795). */
	bool _riding = false;
	/** The troop whose destination the map is choosing (MOVE TROOP), 0 none. */
	uint _movingTroop = 0;
	/** seg000:1bec after time passed: the battle may be over, or Paul dead. */
	bool battleCheck();
	/** A worm journey (seg000:4703, travel mode 2). */
	void rideWormTo(int destination);
	void speedrunLog(const Common::String &what);
	void speedrunPause(uint millis);
	bool speedrunAlive();
	void speedrunCampaignSetup();
	void speedrunCampaign();
	void speedrunFinalAttack();
	bool speedrunFight(uint place);
	void speedrunEquip(uint place);
	void speedrunConverse();
	bool speedrunMeet(uint who);
	bool speedrunCompanion(uint who, bool come);
	void speedrunWait();
	void speedrunTalkHere(Common::Array<uint> &newTroops);
	void speedrunVisitPlace(uint place, Common::Array<uint> &newTroops);
	void speedrunCompanions(const uint *want, uint count);
	void speedrunStory();
	void speedrunShipment();
	void speedrunSpice();
	void speedrunSpiceFields(const Common::Array<uint> &troops);
	bool _speedrunShipping = false;
	void speedrunTravel(uint place);
	void speedrunGarrison(const Common::Array<uint> *busy = nullptr);
	void speedrunKeepInTouch();
	/** Walk into a room of the place (seg000:3f27): the entry lines may speak. */
	void enterRoom(uint room);
	/** The room-leave scan (CD 3faa-3fc2, 36d3): false when a person's line stops the move. */
	bool roomLeaveScan(uint room);
	bool _speedrunRecruited = false;
	int speedrunSpot(uint from, uint16 lng, int16 lat);
	bool speedrunExplore();
	/**
	 * Orders as a player gives them: the troop popup (the chief's GIVE ORDERS
	 * TO TROOP in his sietch, else the map's contact), then its rows, clicked
	 * where they stand. job < 0 keeps the occupation; harvester takes one
	 * lying free where the troop stands (MODIFY EQUIPMENT); moveTo >= 0 is a
	 * MOVE TROOP pick, -1 the prospectors' queue (queue, count).
	 */
	void speedrunOrders(uint id, int job, bool harvester, int moveTo, const uint *queue = nullptr, uint count = 0);
	bool speedrunClickRow(RowAction action, int argument, const char *what);
	bool speedrunOpenOrders(uint id);
	void speedrunCloseOrders();
	/** True the first time a key is seen (the route does each thing once, until something changes). */
	bool speedrunOnce(const Common::String &key);
	Common::Array<Common::String> _speedrunDone;
	uint _speedrunRoundVisits = 0;
	void showFinal(uint picture);
	uint _finalPicture = 0;         ///< the final scene's FINAL.HSQ picture on screen (1, 2), 0 none
	TalkKind _talkKind = kTalkNormal;
	bool _talkBargain = false;      ///< the bargaining menu is up (events 4, 5)
	Common::String _talkLastPage;   ///< for WHAT ?
	Common::Array<byte> _scene;     ///< the running scripted scene's bytes
	uint _sceneCursor = 0;
	bool _sceneActive = false;
	/**
	 * The palace plan (ui_draw_palace_plan, CD seg000:18ee): a floor plan of
	 * the Atreides palace over the view, from the red dot in the compass box.
	 */
	bool _palacePlan = false;
	void drawPalacePlan();
	uint16 _pendingScene = 0;
	uint _sceneReturnRoom = 0;
	Common::Array<byte> _cast;      ///< the scene's explicit cast (slot -> person, 0xff empty)
	uint _sceneKiss = 0;            ///< CHANKISS over the shot: 1 sprite 0 (scene byte 0x0a), 2 sprite 1 (0x0c)
	int _commList = -1;             ///< the COMM list on the panel: 0 new, 1 seen, -1 none
	bool _desert = false;           ///< Paul stands in the open desert (current_scene 0xff)
	bool _visionDream = false;
	/**
	 * The dream (present_vision_dream, CD seg000:2bd2 / floppy 2eba): the
	 * troop whose chief speaks a report (2c23-2c43: the place record's byte 9,
	 * troop 3 for message 0x0e; lip-sync 0x0e), the place (ds:47E6), when it
	 * began and when it ends by itself (wait_interruptable: CD 0xbb8 ticks,
	 * floppy 0x7d0), and the shimmer: VIS's colours 128-191 rotated one step
	 * every 6 ticks (frame task CD 2cc7 / floppy 2f8e, effect 0x0a).
	 */
	uint _dreamTroop = 0;
	int _dreamPlace = -1;
	uint32 _dreamStart = 0;
	uint32 _dreamUntil = 0;
	byte _dreamPalette[64 * 3];
	bool _dreamPaletteValid = false;
	uint _dreamStep = 0xffff;
	void endDream();
	void applyDreamPalette(uint32 now, bool force);
	void drawDreamSubtitle(const Common::String &text);
	void drawPlaceInset(uint place);
	bool _ending = false;           ///< an ending text is up
	Common::String _endingText = "As Paul Atreides failed"; ///< the ending's COMMAND, found by its start
	const char *_pendingEnding = nullptr; ///< an ending waiting for the talk to close
	bool _pendingWakeUp = false; ///< Paul drank the Water of Life: he comes to after Stilgar's line (seg000:2d1e)
	uint32 _wakeUpAt = 0;        ///< when, in real play (the line's 600 ticks)
	Conversation::Position _wakeResume; ///< the talk the wake-up line interrupted, to go on after it
	uint32 _idleStart = 0;
	bool _troopEquipment = false;   ///< the MODIFY EQUIPMENT panel is up (seg000:7cbb)
	Common::Rect _equipRects[2][7]; ///< hit zones: the troop's items, the place's free ones
	void drawEquipmentPanel(const Common::Rect &area);
	bool equipmentTap(int x, int y);
	/** A rallied Fremen troop stationed at @p location. */
	bool hiredTroopAt(uint location) const;
	/** The next rallied Fremen troop id after @p after, cycling (0 none). */
	Common::String _lastTroopRows; ///< logged when they change
	Common::String _lastZoom;      ///< the talk zoom, logged when it changes
	Common::String _lastRoomRows;  ///< the room menu, logged when it changes
	bool troopInView(uint id) const; ///< the troop's icon would be in the flat map's view
	uint nextRalliedTroop(uint after) const;
	/** The last troop CONTACT FREMEN TROOPS reached (data_01955), 0 none. */
	uint _lastContacted = 0;
	/** draw_orni (seg000:3aa9): ORNYTK parts at a pad slot, @p frame 0 parked .. 0x21 gone. */
	void drawOrni(Graphics::Surface &target, int x, int y, uint frame);
	int orniPadX() const;
	void drawParkedOrnis(Graphics::Surface &target, uint skip);
	/** orni_anim_loop (seg000:47fb): take-off (+1) or landing (-1) over the current room. */
	void animateOrni(int step);
	/**
	 * Paul's head on the panel (ds:E8, GameState::kHeadIndex: ICONES 0x10 +
	 * index, 10 faces the player), one frame every 8 ticks (40 ms):
	 * ui_hud_head_animate_up (CD seg000:17e6, floppy 1b64), blocked while
	 * travelling (CD ds:11C9, floppy ds:11D6); animate_down (CD 181e, floppy
	 * 1b82); the fold to 9 then 8 (CD 1843, floppy 1b98). Capture runs set
	 * the last frame at once, so a harness run always ends on it.
	 */
	void headUp(const char *why);
	void headDown(const char *why);
	void headFold(const char *why);
	void setHead(uint index, bool step);
	/** The head at a departure (CD 4745 / 47ad, floppy 4f55 / 4fc7 / 5019). */
	void headForDeparture();
	/** CD only: a character's line lowers the head in text mode (9fec -> 11803). */
	void lineHeadDown();
	bool _headTravel = false; ///< ds:11C9 (CD) / ds:11D6 (floppy) set: the head cannot come up
	/**
	 * The CD's voice_subtitle_mode (ds:28E7): the default (ds:28E8) is 2 when
	 * DNCDPRG finds digital voices and the language is 0 or 3 (cfa0), else 0
	 * (text). The engine plays no CD voices, so its default is 0; the dev key
	 * dune_cd_voice_mode models the voiced modes. The globe/map view forces 1
	 * (5a1a); the room view restores the default (1877).
	 */
	byte _voiceMode = 0;
	byte defaultVoiceMode() const;
	void openMirror();
	void drawMirror();
	void drawResults();
	void openTroop(uint troopId, bool fromMap);
	void drawTroop();
	void drawInfoBox(const Common::Array<Common::String> &lines);
	/** The sky record for a time (sky_palette_id_for_time: CD 395f, floppy 3bed), minus the table's 8. */
	static uint skyPaletteFor(uint16 gameTime);
	uint skyPalette() const;
	/**
	 * The time-of-day light (the sky's 80 colours and the panel's tail, CD
	 * 388d-39e1, floppy 3b13-3c55): set_sky_palette writes the record of the
	 * hour, or leaves a running blend alone; each period that changes the
	 * record while an outdoor view is up arms the blend (38e1 / 3b7b): 0x40
	 * steps, one every 0x10 ticks, each moving the live colours by
	 * (target - live) / steps left. The CD's rooms read SKYDN.HSQ (151
	 * colours at 73, 16 at 240) unless ds:22e3 = 0 (room 0x1005 only); the
	 * floppy SKY.HSQ (80 at 128, 15 at 240).
	 */
	struct SkyLight {
		int record = -1;       ///< current_sky_palette (CD ds:46d6, floppy 4232), minus 8
		bool active = false;   ///< ds:46df / floppy 423b: an outdoor view is up
		bool skyDn = false;    ///< ds:22e3: the record comes from SKYDN.HSQ
		uint steps = 0;        ///< ds:46d7 / floppy 4233: blend steps left
		uint32 next = 0;       ///< when the next step is due
		byte live[256 * 3];    ///< 6-bit DAC values of the sky's ranges
		byte target[256 * 3];
	};
	SkyLight _sky;
	bool loadSkyRecord(uint record, bool skyDn, byte *rgb6);
	void writeSkyLight();
	void setSkyPalette(bool skyDn);
	void skyPeriodChanged(bool blend);
	void updateSkyBlend(uint32 now);
	void spiralPresent();
	void waitPumping(uint32 millis);
	uint _lastProbeStep = 0xffff; ///< the CD flight's probe, logged once a step
	bool _waitingBlend = false; ///< the WAIT verbs blend even in a capture run
	bool _holdPresent = false;  ///< drawRoom composes without showing
	bool _holdRedraw = false;   ///< passTime leaves the view to its caller
	enum { kMaxRoomRows = 32 };
	/** The night battle view in a battle (ds:2b, CD 2dd3; F4). */
	static const bool kNightBattleView = true;
	uint _roomRowSkip = 0;      ///< the room menu's skip (records), paged by " Others..."
	bool _keepRowSkip = false;  ///< the next drawRoom keeps the page
	/** The night battle (CD 0acd / floppy 0c51, drawn for every room while ds:2b is set, CD 2dd3). */
	NightAttack *_nightAttack = nullptr;
	class Sound *_attackSound = nullptr; ///< SN3 (CD) / SD3 (floppy): the night battle (CD 0b1c)
	Sprite *_attackSheet = nullptr;
	Common::Array<byte> _attackData;
	uint32 _attackLast = 0;
	void drawNightBattle();
	void endNightBattle();
	void updateNightBattle(uint32 now);
	enum {
		kCdFlightStepMillis = 3834, ///< 0x300 ticks: the CD's travel step (travel_pump 4f2e)
		kSkyStepMillis = 80,   ///< 0x10 ticks of the 200.3 Hz timer (CD 3901, floppy 3b9e)
		kSpiralStepMillis = 11 ///< transition 0x2a's step (segvga 2572: 3 counts after the previous stamp, 2-3 ticks; fitted to captures/evening)
	};
	void passTime(uint slots);
	bool ensureSaves();
	void setSaveMenuRows();
	Common::String slotLabel(uint slot) const;
	void toggleMusic();
	void playCurrentMusic();

	bool loadDialogue();
	void closeBook();
	void drawBook();
	void endConversation();
	void startTalkAnimation();
	void drawTalk();
	void setTalkRows();
	void drawBubble(const Common::Array<Common::String> &lines, uint first, uint count, const Common::Rect &box, byte ink, int pad = 12);
	uint bubbleLines() const;

	void presentVerb(uint list);
	/**
	 * present_first_matching_dialogue_line's seeds (CD 94f3 -> 9519, floppy
	 * 9fdd): before phase 0x64, a speaker below 9 has the latest ill place
	 * (ds:11db) named for the text codes 0x81/0x82 ("We have to go to ... to
	 * stem the epidemic."). The room menu names the current place again
	 * (2ecd), which endConversation stands for.
	 */
	void stageIllnessNames(uint speaker);
	bool _illnessNamesStaged = false;
	/** set_game_phase_and_trigger_callbacks (seg000:121f). */
	void setGamePhase(byte phase);
	void applyStory();
	/** run_game_phase_triggers (seg000:b17a): the first matching line of DIALOGUE 16 list 7 fires its event. */
	void runPhaseTriggers();
	static void storyEvent(void *context, byte event, bool wasSaid, uint speaker);
	void stageTroopForConditions(uint troopId);
	/**
	 * One line of the troop's answer list (character 15, list 4) with
	 * pending_room_action @p action (troop_present_reaction_line, CD 7bb9):
	 * the line goes to the popup; false when its event dropped the gate.
	 */
	bool troopReaction(byte action);
	/** GO & SEARCH FOR EQUIPMENT for the open troop (CD seg000:7734-77d4, floppy 8498-852f). */
	void searchForEquipment();
	bool nextTroopLine();
	void troopSceneStep();
	uint16 _troopScene = 0;      ///< a scripted scene running over the troop popup (the prospector's lesson)
	uint _troopSceneCursor = 0;
	/**
	 * MOVE TROOP over the troop popup (seg000:8064): the density popup is up,
	 * the caption asks where to go and a tap on the popup's window picks a
	 * place. The prospectors fill a working queue of three (ds:4274, count
	 * ds:4294) first.
	 */
	bool _troopPicking = false;
	uint16 _pickQueue[3] = { 0, 0, 0 };
	uint _pickCount = 0;
	Common::String _pickLineBefore;
	void startTroopPick();
	void troopPickTap(int x, int y);
	void endTroopPick(int dest);

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
	byte _amigaMusic; ///< Original resource choice: M2 at startup, M3 inside sietches.
	byte _musicOrder; ///< Original ds:33fe menu selection: 0 game relative, 1 standard, 3 shuffle.
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
	Common::String _lastStanding; ///< the last "Room people:" log line
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
	uint16 _personFrame[24];      ///< the PERS frame each person stands as (for taps on them)
	/** The person whose figure is under a view position, or -1. */
	int personAt(int x, int y) const;
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

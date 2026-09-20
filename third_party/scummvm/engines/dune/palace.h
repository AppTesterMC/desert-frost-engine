/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_PALACE_H
#define ENGINES_DUNE_PALACE_H

#include "common/scummsys.h"

namespace Dune {

/**
 * The Atreides palace as the original describes it.
 *
 * Source: the location table of DNCDPRG.EXE. A location is a 16-bit value,
 * high byte = place type, low byte = room number (1-based). The pointer table
 * at DS:0x13C4 (file offset 0x10D14) gives, per place type, an array of 5-byte
 * room records; the palace is place type 0x20, records at DS:0x1225. The
 * routines are sub_13EFE (lookup) and sub_13F27 (move in direction BP = 1..4)
 * in OpenRakis' DNCDPRG_RECENT.ASM.
 *
 * Record: room byte, then the exits for up, right, down and left.
 *   Room byte: bit 7 set = not a drawn room (room 3; meaning still unknown).
 *              Otherwise (byte - 1): high nibble selects the sprite sheet
 *              (resource 0x13 + n, see sheetName()), low nibble is the room
 *              inside PALACE.SAL.
 *   Exit:      0 = none, 1..12 = palace room, bit 7 = same but through a door
 *              (conditions not decoded), 252..254 = leave the palace.
 *
 * The data is embedded rather than read from the executable so the engine
 * needs only the game's data files, and because the floppy executable is a
 * different build.
 */
struct PalaceRoom {
	byte code;
	byte exits[4];
	const char *label; ///< Our description for the panel; not game text.
};

enum PalaceConstants {
	kPalaceFirstRoom = 10, ///< The game opens in the throne room.
	kPalaceRoomCount = 12
};

enum Direction {
	kDirectionUp,
	kDirectionRight,
	kDirectionDown,
	kDirectionLeft
};

/** Room record for a 1-based room number. */
const PalaceRoom &palaceRoom(uint number);

/** Room number behind an exit, or 0 if there is none we can show yet. */
uint palaceExit(uint room, Direction direction);

/** Room index inside PALACE.SAL. */
uint palaceSalRoom(const PalaceRoom &room);

/** Sprite sheet file that the room is drawn with. */
const char *palaceSheetName(const PalaceRoom &room);

} // namespace Dune

#endif // ENGINES_DUNE_PALACE_H

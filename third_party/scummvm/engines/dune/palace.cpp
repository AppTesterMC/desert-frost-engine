/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#include "common/util.h"

#include "dune/palace.h"

namespace Dune {

const PalaceRoom &palaceRoom(uint number) {
	// Verbatim from DNCDPRG.EXE, DS:0x1225 (see palace.h).
	static const PalaceRoom rooms[kPalaceRoomCount] = {
		{ 76, { 2, 0, 253, 0 }, "Palace balcony" },
		{ 58, { 7, 0, 1, 140 }, "Equipment room" },
		{ 207, { 0, 0, 0, 11 }, "" },
		{ 98, { 10, 0, 7, 0 }, "Entrance hall" },
		{ 75, { 0, 0, 0, 10 }, "Balcony" },
		{ 21, { 11, 0, 0, 0 }, "Armoury" },
		{ 93, { 4, 139, 2, 136 }, "Corridor" },
		{ 38, { 0, 135, 12, 0 }, "Communications room" },
		{ 99, { 0, 0, 10, 0 }, "Paul's room" },
		{ 97, { 9, 5, 4, 0 }, "Throne room" },
		{ 100, { 0, 131, 6, 7 }, "Empty room" },
		{ 94, { 8, 2, 0, 0 }, "Walkway" }
	};
	return rooms[(number - 1) % kPalaceRoomCount];
}

uint palaceExit(uint room, Direction direction) {
	const byte exit = palaceRoom(room).exits[direction];
	const byte target = exit & 0x7f;
	if (exit >= 252 || target == 0 || target > kPalaceRoomCount)
		return 0;
	// Room 1 is the palace front (PALACE.SAL room 11). Its room description
	// holds only character markers, and where its backdrop comes from is not
	// known yet, so the way out stays closed instead of showing an empty sky.
	if (target == 1)
		return 0;
	return (palaceRoom(target).code & 0x80) ? 0 : target;
}

uint palaceSalRoom(const PalaceRoom &room) {
	return (room.code - 1) & 0x0f;
}

const char *palaceSheetName(const PalaceRoom &room) {
	// Resources 0x13.. in the executable's sprite sheet numbering.
	static const char *const sheets[] = {
		"GENERIC.HSQ", "PROUGE.HSQ", "COMM.HSQ", "EQUI.HSQ", "BALCON.HSQ", "CORR.HSQ", "POR.HSQ"
	};
	return sheets[MIN<uint>((room.code - 1) >> 4, ARRAYSIZE(sheets) - 1)];
}

} // namespace Dune

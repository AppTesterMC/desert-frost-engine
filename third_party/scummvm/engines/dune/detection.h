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

#ifndef DUNE_DETECTION_H
#define DUNE_DETECTION_H

/**
 * Engine options shown in Game Options > Game (and read from scummvm.ini).
 * The two original-bug fixes are off by default.
 *
 * dune_fix_leto_loop: the original never clears Duke Leto's character record
 * when he dies (phase 0x4c, CD sub_11166 / floppy phase callback), so he keeps
 * standing in the throne room, is listed and talks. On: he is gone from every
 * room, command row and presence test from phase 0x4c on.
 *
 * dune_fix_celimyn_tuek: the initial data gives the sietch Celimyn-Tuek the
 * discovery phase 0xff (location byte 0x0b), which the phase compares
 * (floppy seg000:6257, 6340, 7f56) never reach, so it can never be found.
 * On: the byte is 0x58 (the wiki's save patch) at new game and after a load.
 */
#define GAMEOPTION_FIX_LETO_LOOP GUIO_GAMEOPTIONS1
#define GAMEOPTION_FIX_CELIMYN_TUEK GUIO_GAMEOPTIONS2
#define GAMEOPTION_ORIGINAL_OPTIONS GUIO_GAMEOPTIONS3
#define GAMEOPTION_AMIGA_OPTIONS GUIO_GAMEOPTIONS4

#endif

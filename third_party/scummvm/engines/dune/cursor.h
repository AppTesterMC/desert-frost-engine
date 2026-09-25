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

#ifndef ENGINES_DUNE_CURSOR_H
#define ENGINES_DUNE_CURSOR_H

namespace Dune {

/**
 * The game's mouse pointer, shown through ScummVM's cursor manager so that
 * the backend draws and moves it (on touch screens this is what tells the
 * player where a tap lands).
 *
 * The arrow is the original's: DNCDPRG.EXE keeps it at DS:0x2584 in the
 * classic DOS mouse driver layout, which the game's own draw routine
 * (_sub_1DBEC_draw_mouse in the OpenRakis listing) consumes: hotspot x and y,
 * sixteen 16-bit AND-mask rows, then sixteen image rows, most significant bit
 * leftmost. Mask 1 = transparent; otherwise image 1 = white, 0 = black.
 * The executable switches to other images in places (the pointer is a
 * variable, _word_21A32); only the default arrow is used so far.
 *
 * The cursor carries its own three-colour palette, so room palettes cannot
 * recolour it.
 */
void showGameCursor();
void hideGameCursor();

/**
 * Make a tap act where the finger is. ScummVM's phone backends default 2D
 * games to "touchpad" mode, in which dragging moves the pointer and a tap
 * clicks wherever the pointer happens to be - unusable for a game driven by
 * small buttons. A touch mode the user chose in ScummVM's options wins.
 * Call before initGraphics(), which is when the backend reads the setting.
 */
void preferDirectTouch();

} // namespace Dune

#endif // ENGINES_DUNE_CURSOR_H

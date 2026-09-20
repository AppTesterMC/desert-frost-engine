/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
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

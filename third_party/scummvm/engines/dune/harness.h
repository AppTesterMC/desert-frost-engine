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

#ifndef ENGINES_DUNE_HARNESS_H
#define ENGINES_DUNE_HARNESS_H

#include "common/events.h"
#include "common/str.h"

class OSystem;

namespace Dune {

/** True when named checkpoint capture is enabled for a desktop run. */
bool isDuneHarnessRun();

/** Capture the current framebuffer and cursor metadata under a checkpoint name. */
void captureDuneCheckpoint(OSystem *system, const Common::String &name);

/** Poll real input, or the next due command from dune_input. */
bool pollDuneEvent(OSystem *system, Common::Event &event);

} // namespace Dune

#endif // ENGINES_DUNE_HARNESS_H

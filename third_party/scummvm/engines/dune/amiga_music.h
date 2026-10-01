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

#ifndef DUNE_AMIGA_MUSIC_H
#define DUNE_AMIGA_MUSIC_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Audio { class AudioStream; }

namespace Dune {

/** Native four-channel tracker playback of the Amiga M1/M2/M3 resources.
 * Returns nullptr for malformed or unsupported modules. Copies the input;
 * the caller retains ownership of data. The mixer owns the returned stream.
 */
Audio::AudioStream *makeAmigaMusicStream(const Common::Array<byte> &data, int rate);

} // namespace Dune

#endif

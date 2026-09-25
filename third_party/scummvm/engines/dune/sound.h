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

#ifndef ENGINES_DUNE_SOUND_H
#define ENGINES_DUNE_SOUND_H

#include "audio/mixer.h"

#include "common/array.h"
#include "common/scummsys.h"

class OSystem;

namespace Dune {

/** Small owner for a Dune VOC stream and its mixer handle. */
class Sound {
public:
	 explicit Sound(OSystem *system);
	~Sound();

	/** Start a decoded Dune VOC resource as an effects stream. */
	bool playVOC(const Common::Array<byte> &data);
	void stop();
	bool isPlaying() const;

private:
	OSystem *_system;
	Common::Array<byte> _vocData;
	Audio::SoundHandle _handle;
};

} // namespace Dune

#endif // ENGINES_DUNE_SOUND_H

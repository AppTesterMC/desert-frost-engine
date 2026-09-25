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

#include "audio/decoders/voc.h"
#include "audio/decoders/raw.h"

#include "common/memstream.h"
#include "common/system.h"

#include "dune/debug.h"
#include "dune/sound.h"

namespace Dune {

Sound::Sound(OSystem *system) : _system(system) {
}

Sound::~Sound() {
	stop();
}

bool Sound::playVOC(const Common::Array<byte> &data) {
	stop();
	if (data.empty())
		return false;

	// VocStream reads from the source stream while it is playing, so retain
	// the backing bytes for the lifetime of this Sound object.
	_vocData = data;
	Common::SeekableReadStream *source = new Common::MemoryReadStream(_vocData.data(), _vocData.size());
	Audio::SeekableAudioStream *stream = Audio::makeVOCStream(source, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	if (!stream)
		return false;

	_system->getMixer()->playStream(Audio::Mixer::kSFXSoundType, &_handle, stream);
	debugSetAudioStream("sfx:VOC playing");
	return true;
}

void Sound::stop() {
	if (_system)
		_system->getMixer()->stopHandle(_handle);
	_vocData.clear();
	debugSetAudioStream("sfx:VOC stopped");
}

bool Sound::isPlaying() const {
	return _system && _system->getMixer()->isSoundHandleActive(_handle);
}

} // namespace Dune

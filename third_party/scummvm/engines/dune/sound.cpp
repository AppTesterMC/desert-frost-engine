/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#include "audio/decoders/voc.h"
#include "audio/decoders/raw.h"

#include "common/memstream.h"
#include "common/system.h"

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
	return true;
}

void Sound::stop() {
	if (_system)
		_system->getMixer()->stopHandle(_handle);
	_vocData.clear();
}

bool Sound::isPlaying() const {
	return _system && _system->getMixer()->isSoundHandleActive(_handle);
}

} // namespace Dune

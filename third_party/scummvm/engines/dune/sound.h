/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
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

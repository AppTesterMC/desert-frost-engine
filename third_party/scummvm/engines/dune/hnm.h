/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_HNM_H
#define ENGINES_DUNE_HNM_H

#include "common/array.h"
#include "common/scummsys.h"

class OSystem;

namespace Dune {

/**
 * Blocking player for the first-generation Cryo HNM videos used by Dune.
 *
 * A file is a sequence of 16-bit-length chunks. The first holds the initial
 * palette; every other chunk holds optional "sd" (sound) and "pl" (palette)
 * blocks followed by one frame. The sound blocks concatenate to a Creative
 * VOC file, which also paces playback.
 *
 * The chunk and tag layout comes from "Cryo HNM 1 Dumper v.0.2 by VAG, ARR.
 * vagsoft@mail.ru" (console version by Rymoah); the frame header bits were
 * worked out against the game data. See CREDITS.md.
 *
 * Frame header: bits 0-8 width, bit 9 HSQ-packed, bit 10 full frame without
 * an x/y prefix, bit 15 per-line RLE; then height and a mode byte (0xFF
 * keeps the previous frame where the new one has colour 0).
 */
class HnmPlayer {
public:
	enum Result {
		kFinished,
		kSkipped,
		kQuit,
		kError
	};

	explicit HnmPlayer(OSystem *system);

	/**
	 * Play a complete video. A click, tap, Return, Space or Escape skips it.
	 * With dumpFrame >= 0 the video runs untimed and silent up to that frame,
	 * which is written through dumpScreen() under the given name.
	 */
	Result play(const Common::Array<byte> &data, const char *name, int dumpFrame = -1);

private:
	bool readPalette(const byte *data, uint32 size, uint32 &position);
	bool decodeFrame(const byte *block, uint32 size);
	void present();

	OSystem *_system;
	byte _palette[256 * 3];
	bool _paletteDirty;
	Common::Array<byte> _screen;
	Common::Array<byte> _unpacked;
	uint _scale;
};

} // namespace Dune

#endif // ENGINES_DUNE_HNM_H

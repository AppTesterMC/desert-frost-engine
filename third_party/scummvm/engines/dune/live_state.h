/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ENGINES_DUNE_LIVE_STATE_H
#define ENGINES_DUNE_LIVE_STATE_H

#include "common/array.h"
#include "common/str.h"

class OSystem;

namespace Dune {

class Resource;
class SentenceBank;
class StartupLog;
class World;

/**
 * Optional local JSON feed for the live results website. No networking, input
 * or save mutations: only information already available to the player leaves
 * the engine. dune_live_state is an absolute filename in an existing directory.
 */
class LiveStateExporter {
public:
	LiveStateExporter(World &world, Resource &resources, StartupLog &log);
	~LiveStateExporter();
	void tick(OSystem *system, bool ending = false);
	void setGlobeView(uint phase, int tilt, bool visible);

private:
	Common::String snapshot(uint32 elapsed, bool running);
	void appendMap(Common::String &json);
	void appendLocations(Common::String &json);

	World &_world;
	Resource &_resources;
	StartupLog &_log;
	SentenceBank *_sentences = nullptr;
	Common::Array<byte> _spiceFields;
	Common::String _path;
	uint32 _lastWrite = 0, _start = 0, _sequence = 0;
	uint _globePhase = 0;
	int _globeTilt = 0;
	bool _started = false, _failed = false;
	bool _globeViewKnown = false, _globeVisible = false;
};

/** Called alongside the stream pump, including while the bot waits or flies. */
void liveStateTick(OSystem *system);
/** Keep dashboard globe orientation aligned with the map screen the player sees. */
void liveStateGlobeView(uint phase, int tilt, bool visible);

} // namespace Dune

#endif // ENGINES_DUNE_LIVE_STATE_H

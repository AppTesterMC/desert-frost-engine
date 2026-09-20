/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this program.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ENGINES_DUNE_DUNE_H
#define ENGINES_DUNE_DUNE_H

#include "common/error.h"
#include "engines/engine.h"

struct ADGameDescription;

namespace Dune {

/**
 * Engine entry point. run() is the whole life of a game session: data check,
 * graphics, music, intro, then the GameScreen event loop. See README.md in
 * this directory for the map of the engine.
 */
class DuneEngine : public Engine {
public:
	DuneEngine(OSystem *syst, const ADGameDescription *gameDescription);
	~DuneEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature feature) const override;

private:
	const ADGameDescription *_gameDescription;
};

} // namespace Dune

#endif // ENGINES_DUNE_DUNE_H

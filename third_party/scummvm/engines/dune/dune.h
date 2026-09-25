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

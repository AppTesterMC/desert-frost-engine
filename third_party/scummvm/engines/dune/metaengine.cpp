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

#include "base/plugins.h"

#include "engines/advancedDetector.h"

#include "dune/dune.h"

class DuneMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
public:
	const char *getName() const override {
		return "dune";
	}

	bool hasFeature(MetaEngineFeature feature) const override {
		return false;
	}

	Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *description) const override {
		*engine = new Dune::DuneEngine(syst, description);
		return Common::kNoError;
	}
};

#if PLUGIN_ENABLED_DYNAMIC(DUNE)
	REGISTER_PLUGIN_DYNAMIC(DUNE, PLUGIN_TYPE_ENGINE, DuneMetaEngine);
#else
	REGISTER_PLUGIN_STATIC(DUNE, PLUGIN_TYPE_ENGINE, DuneMetaEngine);
#endif

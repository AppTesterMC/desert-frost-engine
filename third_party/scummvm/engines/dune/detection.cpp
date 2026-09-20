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

static const PlainGameDescriptor duneGames[] = {
	{"dune", "Dune"},
	{nullptr, nullptr}
};

namespace Dune {

static const ADGameDescription gameDescriptions[] = {
	// English floppy version.
	{
		"dune",
		"Floppy",
		AD_ENTRY1s("DUNES.HSQ", "c290b19cfc87333ed2208fa8ffba655d", 21874),
		Common::EN_ANY,
		Common::kPlatformDOS,
		ADGF_NO_FLAGS,
		GUIO0()
	},

	// English CD version. This is the DUNE.DAT currently present in the
	// working data set and in both recovered ScummVM Cryo references.
	{
		"dune",
		"CD",
		AD_ENTRY1s("DUNE.DAT", "f096565944ab48cf4cb6cbf389384e6f", 397794384),
		Common::EN_ANY,
		Common::kPlatformDOS,
		ADGF_CD,
		GUIO0()
	},

	AD_TABLE_END_MARKER
};

} // namespace Dune

class DuneMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	DuneMetaEngineDetection() : AdvancedMetaEngineDetection(Dune::gameDescriptions, duneGames) {
	}

	const char *getName() const override {
		return "dune";
	}

	const char *getEngineName() const override {
		return "Dune";
	}

	const char *getOriginalCopyright() const override {
		return "Dune (C) Cryo Interactive Entertainment";
	}
};

REGISTER_PLUGIN_STATIC(DUNE_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, DuneMetaEngineDetection);

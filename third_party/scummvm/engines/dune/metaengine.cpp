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

#include "base/plugins.h"

#include "common/translation.h"

#include "engines/advancedDetector.h"

#include "dune/detection.h"
#include "dune/dune.h"

namespace {

const ADExtraGuiOptionsMap optionsList[] = {
	{
		GAMEOPTION_FIX_LETO_LOOP,
		{
			_s("Fix the Leto loop"),
			_s("The original leaves Duke Leto standing and talking in the palace after his death. When enabled he is gone from phase 0x4c on."),
			"dune_fix_leto_loop",
			false,
			0,
			0
		}
	},
	{
		GAMEOPTION_FIX_CELIMYN_TUEK,
		{
			_s("Fix Celimyn-Tuek"),
			_s("The original's data never lets the sietch Celimyn-Tuek be found. When enabled it can be found from story phase 0x58 on."),
			"dune_fix_celimyn_tuek",
			false,
			0,
			0
		}
	},
	AD_EXTRA_GUI_OPTIONS_TERMINATOR
};

} // namespace

class DuneMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
public:
	const char *getName() const override {
		return "dune";
	}

	const ADExtraGuiOptionsMap *getAdvancedExtraGuiOptions() const override {
		return optionsList;
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

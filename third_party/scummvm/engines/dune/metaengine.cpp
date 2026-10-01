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
#include "common/config-manager.h"
#include "common/platform.h"
#include "backends/keymapper/action.h"
#include "backends/keymapper/keymap.h"
#include "backends/keymapper/standard-actions.h"
#include "dune/options.h"

#include "engines/advancedDetector.h"
#include "engines/dialogs.h"

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

	GUI::OptionsContainerWidget *buildEngineOptionsWidget(GUI::GuiObject *boss, const Common::String &name, const Common::String &target) const override {
		// Older Amiga targets have no engine GUI flags. Their stored platform
		// still identifies them before they are run and detected again.
		if (Common::checkGameGUIOption(GAMEOPTION_AMIGA_OPTIONS, ConfMan.get("guioptions", target)) ||
			Common::parsePlatform(ConfMan.get("platform", target)) == Common::kPlatformAmiga) {
			// Refresh cached pre-music capabilities as well as older targets with
			// no flags. OptionsDialog rereads these before enabling its controls.
			ConfMan.set("guioptions", Common::getGameGUIOptionsDescription(GUIO7(
				GAMEOPTION_FIX_LETO_LOOP, GAMEOPTION_FIX_CELIMYN_TUEK, GAMEOPTION_AMIGA_OPTIONS,
				GUIO_NOSFX, GUIO_NOSPEECH, GUIO_NOMIDI, GUIO_NOLANG)), target);
			return new Dune::OriginalOptionsWidget(boss, name, target, true);
		}
		if (Common::checkGameGUIOption(GAMEOPTION_ORIGINAL_OPTIONS, ConfMan.get("guioptions", target)) ||
			Common::checkGameGUIOption(GAMEOPTION_FIX_LETO_LOOP, ConfMan.get("guioptions", target)))
			return new Dune::OriginalOptionsWidget(boss, name, target);
		return AdvancedMetaEngine::buildEngineOptionsWidget(boss, name, target);
	}

	Common::KeymapArray initKeymaps(const char *target) const override {
		using namespace Common;
		Keymap *map = new Keymap(Keymap::kKeymapTypeGame, "dune", _("Dune controls"));
		map->setPartialMatchAllowed(false);
		Action *action = new Action(kStandardActionLeftClick, _("Select / Interact"));
		action->setLeftClickEvent();
		action->addDefaultInputMapping("MOUSE_LEFT");
		action->addDefaultInputMapping("JOY_A");
		map->addAction(action);
		action = new Action(kStandardActionSkip, _("Cancel / Skip"));
		action->setKeyEvent(KeyState(KEYCODE_ESCAPE, ASCII_ESCAPE));
		action->addDefaultInputMapping("ESCAPE");
		action->addDefaultInputMapping("JOY_B");
		map->addAction(action);
		const char *ids[] = { kStandardActionMoveUp, kStandardActionMoveRight, kStandardActionMoveDown, kStandardActionMoveLeft };
		const char *labels[] = { _s("Up"), _s("Right"), _s("Down"), _s("Left") };
		const char *keys[] = { "UP", "RIGHT", "DOWN", "LEFT" };
		const char *buttons[] = { "JOY_UP", "JOY_RIGHT", "JOY_DOWN", "JOY_LEFT" };
		const KeyCode codes[] = { KEYCODE_UP, KEYCODE_RIGHT, KEYCODE_DOWN, KEYCODE_LEFT };
		for (uint i = 0; i < 4; ++i) {
			action = new Action(ids[i], _(labels[i]));
			action->setKeyEvent(codes[i]);
			action->addDefaultInputMapping(keys[i]);
			action->addDefaultInputMapping(buttons[i]);
			map->addAction(action);
		}
		return Keymap::arrayOf(map);
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

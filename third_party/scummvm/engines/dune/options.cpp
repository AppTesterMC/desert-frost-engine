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

#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/translation.h"
#include "gui/ThemeEval.h"
#include "gui/widgets/popup.h"
#include "dune/options.h"

namespace Dune {

const char *originalLanguageName(uint language) {
	// Floppy DF00: FRA/GER. CD E40C/E610: seven consecutive text banks;
	// CFE4 uses DNCHAR2 for bank 7, whose text is French, not Dutch.
	static const char *const names[] = {
		_s("English (default)"), _s("French (FRA)"), _s("German (GER)"),
		_s("English (ENG)"), _s("Italian (ITA)"), _s("Spanish (SPA)"),
		_s("French with alien font (DUT)")
	};
	return language >= 1 && language <= ARRAYSIZE(names) ? names[language - 1] : "Unknown";
}

namespace {

uint installedLanguages(const Common::Path &path) {
	Common::Array<Common::String> files;
	Common::FSList children;
	Common::FSNode(path).getChildren(children, Common::FSNode::kListFilesOnly);
	Common::FSNode archive;
	for (uint i = 0; i < children.size(); ++i) {
		Common::String name = children[i].getName();
		name.toUppercase();
		files.push_back(name);
		if (name == "DUNE.DAT")
			archive = children[i];
	}
	Common::File file;
	if (archive.exists() && file.open(archive)) {
		files.clear(); // The CD archive is authoritative, as in Resource.
		const uint count = file.readUint16LE();
		if (count && count <= 4096 && 2 + 25 * count <= file.size()) {
			for (uint i = 0; i < count; ++i) {
				char name[17];
				if (file.read(name, 16) != 16)
					break;
				name[16] = 0;
				const uint32 size = file.readUint32LE();
				const uint32 offset = file.readUint32LE();
				file.readByte();
				if (offset <= file.size() && size <= file.size() - offset) {
					Common::String entry(name);
					entry.toUppercase();
					files.push_back(entry);
				}
			}
		}
	}
	uint mask = 0;
	for (uint language = 1; language <= 7; ++language) {
		const Common::String command = Common::String::format("COMMAND%u.HSQ", language);
		const Common::String phrase1 = Common::String::format("PHRASE%u1.HSQ", language);
		const Common::String phrase2 = Common::String::format("PHRASE%u2.HSQ", language);
		uint found = 0;
		for (uint i = 0; i < files.size(); ++i) {
			if (files[i] == command) found |= 1;
			if (files[i] == phrase1) found |= 2;
			if (files[i] == phrase2) found |= 4;
			if (files[i] == "DNCHAR2.BIN") found |= 8;
		}
		if ((found & 7) == 7 && (language != 7 || (found & 8)))
			mask |= 1 << language;
	}
	return mask;
}

bool optionEnabled(const char *key, const Common::String &domain) {
	return ConfMan.hasKey(key, domain) && ConfMan.getBool(key, domain);
}

} // namespace

OriginalOptionsWidget::OriginalOptionsWidget(GUI::GuiObject *boss, const Common::String &name, const Common::String &domain) :
		OptionsContainerWidget(boss, name, "DuneOriginalOptions", domain) {
	new GUI::StaticTextWidget(widgetsBoss(), _dialogLayout + ".languageLabel", _("Text language:"));
	_language = new GUI::PopUpWidget(widgetsBoss(), _dialogLayout + ".language",
		_("Uses the installed game's original command, dialogue, book and intro text. Takes effect when the game starts."));
	const uint installed = installedLanguages(ConfMan.getPath("path", domain));
	for (uint language = 1; language <= 7; ++language)
		if (installed & (1 << language))
			_language->appendEntry(_(originalLanguageName(language)), language);
	if (!_language->numEntries()) {
		_language->appendEntry(_("Game text files not found"), 0);
		_language->setEnabled(false);
	}
	_music = new GUI::CheckboxWidget(widgetsBoss(), _dialogLayout + ".music", _("AdLib music"),
		_("Play the original AdLib music through ScummVM's OPL emulator (ADL). Other original music drivers are not yet supported."));
	_sound = new GUI::CheckboxWidget(widgetsBoss(), _dialogLayout + ".sound", _("Sampled sounds"),
		_("Play sound effects and CD video soundtracks (SDB/SBP). Volume is set in the Volume tab."));
	_leto = new GUI::CheckboxWidget(widgetsBoss(), _dialogLayout + ".leto", _("Fix the Leto loop"),
		_("Remove Duke Leto from the palace after his death, from phase 0x4c."));
	_celimyn = new GUI::CheckboxWidget(widgetsBoss(), _dialogLayout + ".celimyn", _("Fix Celimyn-Tuek"),
		_("Allow the sietch Celimyn-Tuek to be discovered from story phase 0x58."));
}

void OriginalOptionsWidget::load() {
	const uint language = ConfMan.hasKey("dune_language", _domain) ? ConfMan.getInt("dune_language", _domain) : 1;
	_language->setSelectedTag(language);
	_music->setState(!optionEnabled("dune_no_music", _domain));
	_sound->setState(!optionEnabled("dune_no_sound", _domain));
	_leto->setState(optionEnabled("dune_fix_leto_loop", _domain));
	_celimyn->setState(optionEnabled("dune_fix_celimyn_tuek", _domain));
}

bool OriginalOptionsWidget::save() {
	const uint language = _language->getSelectedTag();
	if (language >= 1 && language <= 7)
		ConfMan.setInt("dune_language", language, _domain);
	ConfMan.setBool("dune_no_music", !_music->getState(), _domain);
	ConfMan.setBool("dune_no_sound", !_sound->getState(), _domain);
	ConfMan.setBool("dune_fix_leto_loop", _leto->getState(), _domain);
	ConfMan.setBool("dune_fix_celimyn_tuek", _celimyn->getState(), _domain);
	return true;
}

void OriginalOptionsWidget::defineLayout(GUI::ThemeEval &layouts, const Common::String &layoutName,
		const Common::String &overlayedLayout) const {
	layouts.addDialog(layoutName, overlayedLayout)
		.addLayout(GUI::ThemeLayout::kLayoutVertical).addPadding(8, 8, 8, 8)
		.addWidget("languageLabel", "OptionsLabel")
		.addWidget("language", "PopUp")
		.addSpace(8)
		.addWidget("music", "Checkbox")
		.addWidget("sound", "Checkbox")
		.addSpace(8)
		.addWidget("leto", "Checkbox")
		.addWidget("celimyn", "Checkbox")
		.closeLayout().closeDialog();
}

} // namespace Dune

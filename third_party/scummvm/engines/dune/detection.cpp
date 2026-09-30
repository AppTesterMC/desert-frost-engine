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

#include "engines/advancedDetector.h"

#include "dune/detection.h"

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
		GUIO6(GAMEOPTION_FIX_LETO_LOOP, GAMEOPTION_FIX_CELIMYN_TUEK, GAMEOPTION_ORIGINAL_OPTIONS, GUIO_MIDIADLIB, GUIO_NOLANG, GUIO_NOMIDI)
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
		GUIO6(GAMEOPTION_FIX_LETO_LOOP, GAMEOPTION_FIX_CELIMYN_TUEK, GAMEOPTION_ORIGINAL_OPTIONS, GUIO_MIDIADLIB, GUIO_NOLANG, GUIO_NOMIDI)
	},

	// Sega CD / Mega CD version (USA T-70065, 1994-08-30): the data track's
	// DUNE.DAT, extracted with scripts/segacd_iso.py, or the data track image
	// itself (Redump naming), in which case the engine finds DUNE.DAT in the
	// image's ISO 9660 directory. SegaCdArchive tells the revisions apart.
	{
		"dune",
		"Sega CD",
		AD_ENTRY1s("DUNE.DAT", "6a8557bc59665fdda7fdcdf5245a83d9", 471040000),
		Common::EN_ANY,
		Common::kPlatformSegaCD,
		ADGF_CD,
		GUIO0()
	},
	{
		"dune",
		"Sega CD",
		AD_ENTRY1s("Dune (USA) (Track 1).bin", nullptr, 541369248),
		Common::EN_ANY,
		Common::kPlatformSegaCD,
		ADGF_CD,
		GUIO0()
	},
	// Mega CD, Europe (1994-04; English and French, chosen on a flag screen).
	{
		"dune",
		"Mega CD",
		AD_ENTRY1s("DUNE.DAT", "d83f9f5e50aa89163813fde8b492f572", 471040000),
		Common::EN_ANY,
		Common::kPlatformSegaCD,
		ADGF_CD | ADGF_UNSTABLE,
		GUIO0()
	},

	// English Amiga version (three disks, 1992). The files as the original
	// installer (disk_to_hd) or scripts/dune_amiga_extract.py write them;
	// the executable is the same in the original and the cracked images up
	// to its protection check, which lies past the first 5000 bytes.
	{
		"dune",
		"",
		AD_ENTRY2s("dune", "d858bc26d299fcb715f54ae78f8f1bb9", 137264,
				"dunechar.hsq", "11c0dd6a0f83d7b01781f0f091c38f02", 1058),
		Common::EN_ANY,
		Common::kPlatformAmiga,
		ADGF_NO_FLAGS,
		GUIO8(GAMEOPTION_FIX_LETO_LOOP, GAMEOPTION_FIX_CELIMYN_TUEK, GAMEOPTION_AMIGA_OPTIONS,
			GUIO_NOMUSIC, GUIO_NOSFX, GUIO_NOSPEECH, GUIO_NOMIDI, GUIO_NOLANG)
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

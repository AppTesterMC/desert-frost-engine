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

#include "dune/saves.h"

#include "common/endian.h"
#include "common/file.h"
#include "common/savefile.h"
#include "common/system.h"

#include "dune/debug.h"
#include "dune/dialogue.h"
#include "dune/resource.h"
#include "dune/world.h"

namespace Dune {

SaveGame::SaveGame(World &world, Dialogue &dialogue, Resource &resources, StartupLog &log) :
		_world(world), _dialogue(dialogue), _resources(resources), _log(log) {
}

Common::String SaveGame::fileName(uint slot, bool floppy, bool amiga) {
	// The original's files: Log 1 is DUNE21S1.SAV, Log 2 S2, "last entering
	// into a place" S3 and "last entering new sietch" S4 (checked by saving
	// in the floppy DUNEPRG.EXE on Spice86); S0 is not one of the four logs.
	// Engine Amiga saves use a separate namespace; they are not native
	// Amiga DUNE10 saves and must never overwrite the DOS CD logs.
	return Common::String::format(amiga ? "DUNEAMS%u.SAV" : floppy ? "DUNE21S%u.SAV" : "DUNE37S%u.SAV", slot + 1);
}

bool SaveGame::loadMap() {
	// The live map is the world's (ecology.cpp): its stage bits are saved.
	return _world.map().size() >= (uint)kMapPixels;
}

const Common::Array<byte> &SaveGame::map() {
	return _world.map();
}

bool SaveGame::unpack(const Common::Array<byte> &packed, Common::Array<byte> &body) const {
	body.clear();
	// DOS files have 00/02 in the high marker byte. New engine Amiga
	// files are tagged A1, so a renamed file cannot become a DOS save.
	if (packed.size() < 6 || packed.size() > 0x10001 || packed[2] != kRleMarker ||
			READ_LE_UINT16(packed.data() + 4) != packed.size() - 2 ||
			(packed[3] != 0 && packed[3] != 2 && !(_world.amiga() && packed[3] == kAmigaSaveTag)))
		return false;
	const uint maximum = kMapFlagBytes + kExtraSize + _dialogue.data().size() +
		(_world.layoutShift() ? (uint)kDialogueSlack : (uint)kDialogueSlackCd) + _world.savedSize();
	for (uint i = 6; i < packed.size();) {
		const byte b = packed[i++];
		uint count = 1;
		byte value = b;
		if (b == kRleMarker) {
			if (i + 1 >= packed.size())
				return false;
			count = packed[i++];
			value = packed[i++];
			if (!count)
				return false;
		}
		if (count > maximum - body.size())
			return false;
		for (uint k = 0; k < count; ++k)
			body.push_back(value);
	}
	return true;
}

bool SaveGame::dialogueHeaderMatches(const Common::Array<byte> &body, uint offset, uint16 base) const {
	const Common::Array<byte> &dialogue = _dialogue.data();
	if (dialogue.size() < 2)
		return false;
	const uint header = READ_LE_UINT16(dialogue.data());
	if (header < 2 || (header & 1) || header > dialogue.size() || offset + header > body.size())
		return false;
	for (uint i = 0; i < header; i += 2)
		if (READ_LE_UINT16(body.data() + offset + i) != (uint16)(READ_LE_UINT16(dialogue.data() + i) + base))
			return false;
	return true;
}

bool SaveGame::decode(const Common::Array<byte> &packed, Common::Array<byte> &body, uint &extraSize, uint &slack) const {
	if (!unpack(packed, body))
		return false;
	const bool floppy = _world.layoutShift() != 0;
	extraSize = kExtraSize;
	slack = floppy ? (uint)kDialogueSlack : (uint)kDialogueSlackCd;
	const uint dialogueSize = _dialogue.data().size();
	const uint canonical = kMapFlagBytes + extraSize + dialogueSize + slack + _world.savedSize();
	const uint16 pointerBase = floppy ? kDialoguePointerBase : kDialoguePointerBaseCd;
	if (body.size() == canonical && dialogueHeaderMatches(body, kMapFlagBytes + extraSize, pointerBase))
		return true;
	// Recognize only the two historical engine layouts. Exact sizes and
	// the complete immutable list header distinguish them from another
	// release's save; an arbitrary shorter body is never a "no gap" save.
	if (floppy && body.size() == canonical &&
			dialogueHeaderMatches(body, kMapFlagBytes + kExtraSize + kDialogueSlack, 0)) {
		extraSize += kDialogueSlack;
		slack = 0;
		return true;
	}
	if (!floppy && !_world.amiga() && body.size() == canonical - kDialogueSlackCd &&
			(dialogueHeaderMatches(body, kMapFlagBytes + extraSize, pointerBase) ||
			 dialogueHeaderMatches(body, kMapFlagBytes + extraSize, 0))) {
		slack = 0;
		return true;
	}
	return false;
}

void SaveGame::pack(const Common::Array<byte> &body, Common::Array<byte> &packed) const {
	// dune-rust compress_rle: runs longer than two, and the marker itself, become marker/count/value.
	for (uint i = 0; i < body.size();) {
		const byte value = body[i];
		uint run = 1;
		while (i + run < body.size() && body[i + run] == value && run < 255)
			++run;
		if (run > 2 || value == kRleMarker) {
			packed.push_back(kRleMarker);
			packed.push_back((byte)run);
			packed.push_back(value);
		} else {
			for (uint k = 0; k < run; ++k)
				packed.push_back(value);
		}
		i += run;
	}
}

static Common::SeekableReadStream *openSave(const Common::String &name) {
	// Raw: time 0x0178 starts with a zlib header, but these files are RLE.
	Common::SeekableReadStream *stream = g_system->getSavefileManager()->openRawFile(name);
	if (stream)
		return stream;
	// Explicit original DOS saves or matching engine fixtures in game data.
	Common::File *file = new Common::File();
	if (file->open(Common::Path(name)))
		return file;
	delete file;
	return nullptr;
}

bool SaveGame::readFile(uint slot, Common::Array<byte> &packed) const {
	if (slot >= kSlots)
		return false;
	const bool floppy = _world.layoutShift() != 0;
	Common::SeekableReadStream *stream = openSave(fileName(slot, floppy, _world.amiga()));
	if (!stream && _world.amiga()) {
		// Earlier engine Amiga builds shared CD names. Import only when
		// the new slot is absent; decode() must confirm the Amiga layout.
		// Reading never renames, deletes or rewrites the legacy file.
		stream = openSave(fileName(slot, false));
	}
	if (!stream)
		return false;
	const int64 size = stream->size();
	if (size < 6 || size > 0x10001) {
		delete stream;
		return false;
	}
	packed.resize((uint)size);
	const bool ok = stream->read(packed.data(), packed.size()) == packed.size();
	delete stream;
	return ok;
}

int SaveGame::slotTime(uint slot) const {
	Common::Array<byte> packed, body;
	uint extraSize, slack;
	if (!readFile(slot, packed))
		return -1;
	if (!decode(packed, body, extraSize, slack)) {
		_log.line(Common::String::format("Saves: slot %u is incompatible or damaged", slot));
		return -1;
	}
	return READ_LE_UINT16(packed.data());
}

bool SaveGame::save(uint slot) {
	if (slot >= kSlots || !loadMap())
		return false;
	const bool floppy = _world.layoutShift() != 0;
	const uint extraSize = kExtraSize;
	Common::Array<byte> body;
	// The map's flag bits, four pixels to a byte, first pixel in the top bits (sub_1B427).
	for (uint i = 0; i < kMapFlagBytes; ++i) {
		byte b = 0;
		for (uint k = 0; k < 4; ++k) {
			const uint pixel = 4 * i + k;
			const byte flags = pixel < _world.map().size() ? (byte)((_world.map()[pixel] >> 4) & 3) : 0;
			b |= (byte)(flags << (6 - 2 * k));
		}
		body.push_back(b);
	}
	for (uint i = 0; i < extraSize; ++i)
		body.push_back(i < _extra.size() ? _extra[i] : 0);
	// The dialogue table as the executable keeps it in memory: its list
	// header holds near pointers (offset + kDialoguePointerBase on the
	// floppy), and the floppy's buffer is 36 bytes longer than the file.
	const Common::Array<byte> &dialogue = _dialogue.data();
	const uint header = dialogue.size() >= 2 ? READ_LE_UINT16(dialogue.data()) : 0;
	for (uint i = 0; i < dialogue.size(); i += 2) {
		uint16 word = i + 1 < dialogue.size() ? READ_LE_UINT16(dialogue.data() + i) : dialogue[i];
		if (i < header)
			word = (uint16)(word + (floppy ? kDialoguePointerBase : kDialoguePointerBaseCd));
		body.push_back((byte)word);
		if (i + 1 < dialogue.size())
			body.push_back((byte)(word >> 8));
	}
	const uint slackSize = floppy ? (uint)kDialogueSlack : (uint)kDialogueSlackCd;
	for (uint i = 0; i < slackSize; ++i)
		body.push_back(i < _slack.size() ? _slack[i] : 0);
	const GameState &state = _world.state();
	for (uint i = 0; i < _world.savedSize(); ++i)
		body.push_back(state.vars[i]);

	Common::Array<byte> packed;
	packed.resize(6);
	WRITE_LE_UINT16(packed.data(), state.w(GameState::kGameTime));
	WRITE_LE_UINT16(packed.data() + 2, _world.amiga() ? (kAmigaSaveTag << 8) | kRleMarker : floppy ? 0x02f7 : 0x00f7);
	pack(body, packed);
	WRITE_LE_UINT16(packed.data() + 4, (uint16)(packed.size() - 2));

	const Common::String name = fileName(slot, floppy, _world.amiga());
	Common::OutSaveFile *out = g_system->getSavefileManager()->openForSaving(name, false);
	if (!out) {
		_log.line(Common::String::format("Saves: cannot write %s", name.c_str()));
		return false;
	}
	out->write(packed.data(), packed.size());
	out->finalize();
	const bool ok = !out->err();
	delete out;
	_log.line(Common::String::format("Saves: %s written (%u bytes, %u unpacked)", name.c_str(), packed.size(), body.size()));
	return ok;
}

bool SaveGame::load(uint slot) {
	if (slot >= kSlots || !loadMap())
		return false;
	Common::Array<byte> packed, body;
	if (!readFile(slot, packed)) {
		_log.line(Common::String::format("Saves: slot %u is empty", slot));
		return false;
	}
	uint extraSize, slack;
	if (!decode(packed, body, extraSize, slack)) {
		_log.line(Common::String::format("Saves: slot %u is incompatible or damaged", slot));
		return false;
	}
	const uint dialogueSize = _dialogue.data().size();
	const uint header = READ_LE_UINT16(_dialogue.data().data());
	// All format checks finish before changing the map, dialogue or state.
	for (uint i = 0; i < kMapFlagBytes; ++i)
		for (uint k = 0; k < 4; ++k) {
			const uint pixel = 4 * i + k;
			if (pixel < _world.map().size())
				_world.map()[pixel] = (byte)((_world.map()[pixel] & 0xcf) | (((body[i] >> (6 - 2 * k)) & 3) << 4));
		}
	_extra.clear();
	for (uint i = 0; i < extraSize; ++i)
		_extra.push_back(body[kMapFlagBytes + i]);
	// The entries (their said flags) come from the save; the list header
	// stays the file's (the save holds the executable's pointers there).
	Common::Array<byte> dialogue;
	for (uint i = 0; i < dialogueSize; ++i)
		dialogue.push_back(i < header ? _dialogue.data()[i] : body[kMapFlagBytes + extraSize + i]);
	_dialogue.setData(dialogue);
	_slack.clear();
	for (uint i = 0; i < slack; ++i)
		_slack.push_back(body[kMapFlagBytes + extraSize + dialogueSize + i]);
	GameState &state = _world.mutableState();
	memcpy(state.vars, body.data() + kMapFlagBytes + extraSize + dialogueSize + slack, _world.savedSize());
	state.notebook.clear();
	_world.markPlaceCells();
	_world.applyCelimynTuekFix(); // in memory; only a save writes it
	_log.line(Common::String::format("Saves: slot %u loaded, time %u, place %u room %u", slot,
			state.w(GameState::kGameTime), _world.currentLocation(), _world.room()));
	return true;
}

} // namespace Dune

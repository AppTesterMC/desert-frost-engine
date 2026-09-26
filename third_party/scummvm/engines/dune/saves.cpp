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

Common::String SaveGame::fileName(uint slot, bool floppy) {
	return Common::String::format(floppy ? "DUNE21S%u.SAV" : "DUNE37S%u.SAV", slot);
}

bool SaveGame::loadMap() {
	// The live map is the world's (ecology.cpp): its stage bits are saved.
	return _world.map().size() >= (uint)kMapPixels;
}

const Common::Array<byte> &SaveGame::map() {
	return _world.map();
}

void SaveGame::unpack(const Common::Array<byte> &packed, Common::Array<byte> &body) const {
	body.clear();
	if (packed.size() < 6)
		return;
	const byte marker = packed[2];
	for (uint i = 6; i < packed.size();) {
		const byte b = packed[i++];
		if (b == marker && i + 1 < packed.size()) {
			const byte count = packed[i++];
			const byte value = packed[i++];
			for (uint k = 0; k < count; ++k)
				body.push_back(value);
		} else {
			body.push_back(b);
		}
	}
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

bool SaveGame::readFile(uint slot, Common::Array<byte> &packed) const {
	const bool floppy = _world.layoutShift() != 0;
	const Common::String name = fileName(slot, floppy);
	// Raw: a save begins with the game time, and a time such as 0x0178
	// writes 78 01, a zlib header, which openForLoading() would try to
	// inflate (the speedrun check found it at game time 376).
	Common::SeekableReadStream *stream = g_system->getSavefileManager()->openRawFile(name);
	if (!stream) {
		// A save of the DOS game in the game directory works too.
		Common::File *file = new Common::File();
		if (!file->open(Common::Path(name))) {
			delete file;
			return false;
		}
		stream = file;
	}
	packed.resize(stream->size());
	const bool ok = stream->read(packed.data(), packed.size()) == packed.size();
	delete stream;
	return ok;
}

int SaveGame::slotTime(uint slot) const {
	Common::Array<byte> packed;
	if (!readFile(slot, packed) || packed.size() < 6)
		return -1;
	return READ_LE_UINT16(packed.data());
}

bool SaveGame::save(uint slot) {
	if (!loadMap())
		return false;
	const bool floppy = _world.layoutShift() != 0;
	const uint extraSize = floppy ? 0xc6 : 0xa2;
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
	const Common::Array<byte> &dialogue = _dialogue.data();
	for (uint i = 0; i < dialogue.size(); ++i)
		body.push_back(dialogue[i]);
	const GameState &state = _world.state();
	for (uint i = 0; i < _world.savedSize(); ++i)
		body.push_back(state.vars[i]);

	Common::Array<byte> packed;
	packed.resize(6);
	WRITE_LE_UINT16(packed.data(), state.w(GameState::kGameTime));
	WRITE_LE_UINT16(packed.data() + 2, floppy ? 0x02f7 : 0x00f7);
	pack(body, packed);
	WRITE_LE_UINT16(packed.data() + 4, (uint16)(packed.size() - 2));

	const Common::String name = fileName(slot, floppy);
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
	if (!loadMap())
		return false;
	Common::Array<byte> packed, body;
	if (!readFile(slot, packed)) {
		_log.line(Common::String::format("Saves: slot %u is empty", slot));
		return false;
	}
	unpack(packed, body);
	const bool floppy = _world.layoutShift() != 0;
	const uint extraSize = floppy ? 0xc6 : 0xa2;
	const uint dialogueSize = _dialogue.data().size();
	const uint expected = kMapFlagBytes + extraSize + dialogueSize + _world.savedSize();
	if (body.size() < expected) {
		_log.line(Common::String::format("Saves: slot %u holds %u bytes (%u packed), %u expected", slot, body.size(),
				packed.size(), expected));
		return false;
	}
	for (uint i = 0; i < kMapFlagBytes; ++i)
		for (uint k = 0; k < 4; ++k) {
			const uint pixel = 4 * i + k;
			if (pixel < _world.map().size())
				_world.map()[pixel] = (byte)((_world.map()[pixel] & 0xcf) | (((body[i] >> (6 - 2 * k)) & 3) << 4));
		}
	_extra.clear();
	for (uint i = 0; i < extraSize; ++i)
		_extra.push_back(body[kMapFlagBytes + i]);
	Common::Array<byte> dialogue;
	for (uint i = 0; i < dialogueSize; ++i)
		dialogue.push_back(body[kMapFlagBytes + extraSize + i]);
	_dialogue.setData(dialogue);
	GameState &state = _world.mutableState();
	memcpy(state.vars, body.data() + kMapFlagBytes + extraSize + dialogueSize, _world.savedSize());
	state.notebook.clear();
	_world.markPlaceCells();
	_log.line(Common::String::format("Saves: slot %u loaded, time %u, place %u room %u", slot,
			state.w(GameState::kGameTime), _world.currentLocation(), _world.room()));
	return true;
}

} // namespace Dune

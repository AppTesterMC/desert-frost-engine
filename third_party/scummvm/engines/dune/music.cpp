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

#include "common/endian.h"
#include "common/func.h"
#include "common/textconsole.h"
#include "common/system.h"

#include "audio/fmopl.h"

#include "dune/music.h"
#include "dune/amiga_music.h"

namespace Dune {

namespace {

const byte kSlotOffset[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
const uint16 kFNum[12] = { 343, 364, 385, 408, 433, 459, 486, 515, 546, 579, 614, 650 };
const byte kFineBend[13] = { 19, 21, 21, 23, 25, 26, 27, 29, 31, 33, 35, 36, 37 };
const byte kCoarseBend[10] = { 0, 5, 10, 15, 20, 0, 6, 12, 18, 24 };

// Instrument record layout.
enum {
	kModKSL = 2, kModMul, kFeedback, kModA, kModS, kModEG, kModD, kModR, kModOut, kModAM, kModVib, kModKSR,
	kCon, kCarKSL, kCarMul, kPan, kCarA, kCarS, kCarEG, kCarD, kCarR, kCarOut, kCarAM, kCarVib, kCarKSR,
	kMacroFeedbackAT, kModWave, kCarWave, kMacroModOutVel, kMacroCarOutVel, kMacroFeedbackVel,
	kMacroSlideCoarse, kMacroTranspose, kMacroSlideDuration, kMacroSlideRange, kUnknown,
	kMacroModOutAT, kMacroCarOutAT
};

const uint kHeaderSize = 0x34;
const uint kMaxTracks = 21;

} // namespace

Music::Music(bool amiga) :
		_amiga(amiga), _opl(nullptr), _playing(false), _instrumentOffset(0), _instrumentCount(0), _loopStart(0), _loopEnd(0),
		_speed(0), _time(0), _tickPosition(0), _totalTicks(0), _loopPosition(0), _songEnd(false) {
	memset(_channels, 0, sizeof(_channels));
}

Music::~Music() {
	stop();
	delete _opl;
}

bool Music::play(const Common::Array<byte> &data) {
	stop();
	if (_amiga) {
		Audio::AudioStream *stream = makeAmigaMusicStream(data, g_system->getMixer()->getOutputRate());
		if (!stream)
			return false;
		g_system->getMixer()->playStream(Audio::Mixer::kMusicSoundType, &_amigaHandle, stream);
		_playing = true;
		return true;
	}

	if (data.size() < kHeaderSize)
		return false;
	const uint32 instrumentOffset = READ_LE_UINT16(data.data());
	// 0x32 marks the OPL2 flavour; 0x52 would be AdLib Gold, and the MT-32
	// files carry no instrument bank at all.
	if (READ_LE_UINT16(data.data() + 2) != 0x32 || instrumentOffset > data.size() ||
			(data.size() - instrumentOffset) < kInstrumentSize)
		return false;
	const uint16 speed = READ_LE_UINT16(data.data() + 0x32);
	if (!speed)
		return false;

	Common::Array<Track> tracks;
	for (uint i = 0; i < kMaxTracks; ++i) {
		const uint32 offset = READ_LE_UINT16(data.data() + 2 + i * 2);
		if (!offset)
			break;
		uint32 next = (i + 1 < kMaxTracks) ? READ_LE_UINT16(data.data() + 2 + (i + 1) * 2) : 0;
		next = next ? next + 2 : instrumentOffset;
		if (offset + 2 > next || next > data.size())
			return false;
		Track track;
		track.start = offset + 2;
		track.size = next - track.start;
		track.position = track.counter = track.ticks = 0;
		tracks.push_back(track);
	}
	if (tracks.empty())
		return false;

	if (!_opl) {
		_opl = OPL::Config::create();
		if (!_opl || !_opl->init()) {
			warning("Dune: no OPL emulator available for music");
			delete _opl;
			_opl = nullptr;
			return false;
		}
	}

	{
		Common::StackLock lock(_mutex);
		_data = data;
		_instrumentOffset = instrumentOffset;
		_instrumentCount = (data.size() - instrumentOffset) / kInstrumentSize;
		_loopStart = READ_LE_UINT16(data.data() + 0x2c);
		_loopEnd = READ_LE_UINT16(data.data() + 0x2e);
		_speed = speed;
		_tracks = tracks;
		rewind();
		_playing = true;
	}

	_opl->start(new Common::Functor0Mem<void, Music>(this, &Music::onTimer), 200);
	return true;
}

void Music::stop() {
	if (_amiga) {
		g_system->getMixer()->stopHandle(_amigaHandle);
		_playing = false;
		return;
	}
	if (!_opl || !_playing)
		return;
	_opl->stop();
	Common::StackLock lock(_mutex);
	_playing = false;
	for (uint c = 0; c < kVoices; ++c)
		_opl->writeReg(0xb0 + c, 0);
}

void Music::rewind() {
	_time = 0;
	_songEnd = false;
	_tickPosition = ~0u; // There is always one excess tick at the start.
	_totalTicks = 0;
	_loopPosition = ~0u;

	for (uint i = 0; i < _tracks.size(); ++i) {
		Track &track = _tracks[i];
		track.position = 0;
		uint32 length = 0;
		while (track.position < track.size) {
			length += readTicks(track);
			if (track.position >= track.size)
				break;
			switch (_data[track.start + track.position++] & 0xf0) {
			case 0x80:
			case 0x90:
			case 0xa0:
			case 0xb0:
				track.position += 2;
				break;
			case 0xc0:
			case 0xd0:
			case 0xe0:
				track.position += 1;
				break;
			default:
				track.position = track.size;
				break;
			}
		}
		_totalTicks = MAX(_totalTicks, length);
		track.position = track.counter = track.ticks = 0;
	}

	for (uint c = 0; c < kVoices; ++c) {
		_channels[c].program = 0;
		_channels[c].note = 0;
		_channels[c].keyOn = false;
		_channels[c].bend = kBendCenter;
		_channels[c].slideDuration = 0;
	}

	_opl->reset();
	_opl->writeReg(0x01, 0x20); // Enable waveform select
	_opl->writeReg(0xbd, 0x00); // No percussion mode
	_opl->writeReg(0x08, 0x40); // Note-Sel
}

void Music::onTimer() {
	Common::StackLock lock(_mutex);
	if (!_playing)
		return;

	_time -= 256;
	if (_time >= 0)
		return;
	_time += _speed;
	processEvents();

	if (_songEnd) {
		// Songs without a usable loop range simply start over.
		rewind();
	}
}

uint32 Music::readTicks(Track &track) {
	uint32 result = 0;
	byte value;
	do {
		if (track.position >= track.size)
			break;
		value = _data[track.start + track.position++];
		result = (result << 7) | (value & 0x7f);
	} while (value & 0x80);
	return result;
}

void Music::processEvents() {
	_songEnd = true;

	if (_loopStart && _loopEnd && (_tickPosition + 1) % kMeasureTicks == 0 &&
			(_tickPosition + 1) / kMeasureTicks + 1 == _loopStart) {
		_loopPosition = _tickPosition;
		_loopTracks = _tracks;
	}

	for (uint i = 0; i < _tracks.size(); ++i) {
		Track &track = _tracks[i];
		if (i < kVoices && _channels[i].slideDuration > 0 && _channels[i].keyOn)
			macroSlide(i);
		if (track.position >= track.size)
			continue;

		_songEnd = false;
		if (!track.counter) {
			const bool first = track.position == 0;
			track.ticks = readTicks(track);
			if (first && track.ticks)
				++track.ticks; // Keeps the tracks aligned with the excess first tick.
		}

		if (++track.counter >= track.ticks) {
			track.counter = 0;
			while (track.position < track.size) {
				executeCommand(i);
				if (track.position >= track.size)
					break;
				if (_data[track.start + track.position])
					break;
				++track.position; // Zero delay: the next event is simultaneous.
			}
		} else if (track.ticks >= 0x8000) {
			track.position = track.size;
			track.counter = track.ticks;
		}
	}

	if (!_songEnd)
		++_tickPosition;

	if (_loopStart && _loopEnd && !_loopTracks.empty() &&
			(_tickPosition == _totalTicks ||
			 (_tickPosition % kMeasureTicks == 0 && _tickPosition / kMeasureTicks + 1 == _loopEnd))) {
		_tickPosition = _loopPosition;
		_tracks = _loopTracks;
		_songEnd = false;
	}
}

void Music::executeCommand(uint t) {
	Track &track = _tracks[t];
	if (t >= kVoices) {
		track.position = track.size;
		return;
	}

	const byte *events = _data.data() + track.start;
	const byte status = events[track.position++];
	const uint parameters = (status == 0xff) ? 0 : ((status & 0xf0) < 0xc0 ? 2 : ((status & 0xf0) < 0xf0 ? 1 : 0));
	if (status < 0x80 || status >= 0xf0 || track.position + parameters > track.size) {
		track.position = track.size;
		return;
	}

	const byte first = events[track.position];
	const byte second = parameters > 1 ? events[track.position + 1] : 0;
	track.position += parameters;

	switch (status & 0xf0) {
	case 0x80:
		noteOff(t, first);
		break;
	case 0x90:
		noteOn(t, first, second);
		break;
	case 0xc0:
		if (first < _instrumentCount) {
			_channels[t].program = first;
			changeProgram(t, first);
		}
		break;
	case 0xd0:
		aftertouch(t, first);
		break;
	case 0xe0:
		_channels[t].bend = first;
		if (_channels[t].keyOn)
			playNote(t, _channels[t].note, kNoteUpdate);
		break;
	default:
		break;
	}
}

void Music::noteOn(uint c, byte note, byte velocity) {
	Channel &channel = _channels[c];
	if (channel.keyOn) {
		channel.keyOn = false;
		playNote(c, channel.note, kNoteOff);
	}

	channel.note = note;
	channel.keyOn = true;
	channel.bend = kBendCenter;
	playNote(c, note, kNoteOn);

	const byte *data = instrument(channel.program);
	if (data[kMacroModOutVel])
		macroOutput(c, false, (int8)data[kMacroModOutVel], velocity);
	if (data[kMacroCarOutVel])
		macroOutput(c, true, (int8)data[kMacroCarOutVel], velocity);
	if (data[kMacroFeedbackVel])
		macroFeedback(c, (int8)data[kMacroFeedbackVel], velocity);
}

void Music::noteOff(uint c, byte note) {
	Channel &channel = _channels[c];
	if (note != channel.note || !channel.keyOn)
		return;
	channel.keyOn = false;
	playNote(c, note, kNoteOff);
}

void Music::aftertouch(uint c, byte value) {
	const byte *data = instrument(_channels[c].program);
	if (data[kMacroModOutAT])
		macroOutput(c, false, (int8)data[kMacroModOutAT], value);
	if (data[kMacroCarOutAT] && data[kMacroCarOutVel])
		macroOutput(c, true, (int8)data[kMacroCarOutAT], value);
	if (data[kMacroFeedbackAT])
		macroFeedback(c, (int8)data[kMacroFeedbackAT], value);
}

void Music::playNote(uint c, byte note, NoteState state) {
	Channel &channel = _channels[c];
	const byte *data = instrument(channel.program);

	if (data[kMacroTranspose])
		note += data[kMacroTranspose];
	note -= 24;
	if (state != kNoteUpdate && note >= 0x60)
		note = 0;

	int octave = note / 12;
	int key = note % 12;

	if (state != kNoteUpdate && data[kMacroSlideDuration])
		channel.slideDuration = (state == kNoteOn) ? data[kMacroSlideDuration] : 0;

	const int bend = (int)channel.bend - kBendCenter;
	const int amount = ABS(bend);
	int detune = 0;

	if (!(data[kMacroSlideCoarse] & 1)) {
		const int steps = amount >> 5;
		const int fraction = (amount << 3) & 0xff;
		if (bend < 0) {
			key -= steps;
			if (key < 0) {
				key += 12;
				--octave;
			}
			if (octave < 0)
				key = octave = 0;
			detune = -((kFineBend[key] * fraction) >> 8);
		} else {
			key += steps;
			if (key >= 12) {
				key -= 12;
				++octave;
			}
			detune = (kFineBend[key + 1] * fraction) >> 8;
		}
	} else {
		if (bend < 0) {
			key -= amount / 5;
			if (key < 0) {
				key += 12;
				--octave;
			}
			if (octave < 0)
				key = octave = 0;
			detune = -kCoarseBend[(amount % 5) + (key >= 6 ? 5 : 0)];
		} else {
			key += amount / 5;
			if (key >= 12) {
				key -= 12;
				++octave;
			}
			detune = kCoarseBend[(amount % 5) + (key >= 6 ? 5 : 0)];
		}
	}

	const uint16 frequency = kFNum[key] + detune;
	_opl->writeReg(0xa0 + c, frequency & 0xff);
	_opl->writeReg(0xb0 + c, ((frequency >> 8) & 3) | ((octave & 7) << 2) | (state != kNoteOff ? 0x20 : 0));
}

void Music::changeProgram(uint c, uint index) {
	const byte *data = instrument(index);
	const byte slot = kSlotOffset[c];

	_opl->writeReg(0x20 + slot, (data[kModMul] & 15) | ((data[kModKSR] & 1) << 4) | ((data[kModEG] ? 1 : 0) << 5) |
			((data[kModVib] & 1) << 6) | ((data[kModAM] & 1) << 7));
	_opl->writeReg(0x23 + slot, (data[kCarMul] & 15) | ((data[kCarKSR] & 1) << 4) | ((data[kCarEG] ? 1 : 0) << 5) |
			((data[kCarVib] & 1) << 6) | ((data[kCarAM] & 1) << 7));
	_opl->writeReg(0x40 + slot, (data[kModOut] & 63) | ((data[kModKSL] & 3) << 6));
	_opl->writeReg(0x43 + slot, (data[kCarOut] & 63) | ((data[kCarKSL] & 3) << 6));
	_opl->writeReg(0x60 + slot, (data[kModD] & 15) | ((data[kModA] & 15) << 4));
	_opl->writeReg(0x63 + slot, (data[kCarD] & 15) | ((data[kCarA] & 15) << 4));
	_opl->writeReg(0x80 + slot, (data[kModR] & 15) | ((data[kModS] & 15) << 4));
	_opl->writeReg(0x83 + slot, (data[kCarR] & 15) | ((data[kCarS] & 15) << 4));
	_opl->writeReg(0xc0 + c, (data[kCon] ? 0 : 1) | ((data[kFeedback] & 7) << 1));
	_opl->writeReg(0xe0 + slot, data[kModWave] & 3);
	_opl->writeReg(0xe3 + slot, data[kCarWave] & 3);
}

void Music::macroOutput(uint c, bool carrier, int8 sensitivity, byte level) {
	if (sensitivity < -4 || sensitivity > 4)
		return;

	const byte *data = instrument(_channels[c].program);
	uint output = (sensitivity < 0) ? (level >> (sensitivity + 4)) : ((0x80 - level) >> (4 - sensitivity));
	output = MIN<uint>(output, 63) + data[carrier ? kCarOut : kModOut];
	output = MIN<uint>(output, 63);
	_opl->writeReg((carrier ? 0x43 : 0x40) + kSlotOffset[c], output | ((data[carrier ? kCarKSL : kModKSL] & 3) << 6));
}

void Music::macroFeedback(uint c, int8 sensitivity, byte level) {
	if (sensitivity < -6 || sensitivity > 6)
		return;

	const byte *data = instrument(_channels[c].program);
	uint feedback = (sensitivity < 0) ? (level >> (sensitivity + 7)) : ((0x80 - level) >> (7 - sensitivity));
	feedback = MIN<uint>(feedback, 7) + data[kFeedback];
	feedback = MIN<uint>(feedback, 7);
	_opl->writeReg(0xc0 + c, (data[kCon] ? 0 : 1) | (feedback << 1));
}

void Music::macroSlide(uint c) {
	Channel &channel = _channels[c];
	if (!channel.slideDuration)
		return;
	--channel.slideDuration;
	channel.bend += instrument(channel.program)[kMacroSlideRange];
	if (channel.note & 0x7f)
		playNote(c, channel.note, kNoteUpdate);
}

} // namespace Dune

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

#include "audio/mods/paula.h"
#include "common/endian.h"
#include "common/util.h"

#include "dune/amiga_music.h"

namespace Dune {
namespace {

// The three original FLT4 modules use only these tracker effects. Dune's
// replay differs from modern ProTracker: Dxx always starts row zero, Fxx
// is speed (1..31), and the restart byte is honored. See AMIGA-AUDIO.md.
class AmigaMusicStream : public Audio::Paula {
public:
	AmigaMusicStream(const Common::Array<byte> &data, int rate) :
		Paula(true, rate, rate / 50, kFilterModeA500), _data(data),
		_tick(0), _speed(6), _position(0), _row(0) {
		memset(_channels, 0, sizeof(_channels));
		// CIA B timer A reload $0dfc, divided by four (hunk0:11a24,
		// 11c78). Use PAL CIA timing instead of rounding to 50 Hz.
		setTimerBaseValue(kPalCiaClock);
		setInterruptFreqUnscaled(4 * (0x0dfc + 1));
		// Real Amiga output: channels 0/3 left, 1/2 right.
		setChannelPanning(0, 0);
		setChannelPanning(1, 255);
		setChannelPanning(2, 255);
		setChannelPanning(3, 0);
		uint32 offset = 1084 + (highestPattern(data) + 1) * 1024;
		for (uint i = 0; i < 31; ++i) {
			const byte *header = &_data[20 + i * 30];
			Sample &sample = _samples[i];
			sample.offset = offset;
			sample.length = READ_BE_UINT16(header + 22) * 2;
			sample.volume = header[25];
			sample.repeat = READ_BE_UINT16(header + 26) * 2;
			sample.repeatLength = READ_BE_UINT16(header + 28) * 2;
			// Original hunk0:1286c clears four bytes at every sample start.
			for (uint j = 0; j < MIN<uint32>(4, sample.length); ++j)
				_data[offset + j] = 0;
			offset += sample.length;
		}
		startPaula();
	}

	static uint highestPattern(const Common::Array<byte> &data) {
		uint highest = 0;
		for (uint i = 952; i < 1080; ++i)
			highest = MAX<uint>(highest, data[i]);
		return highest;
	}

	static bool valid(const Common::Array<byte> &data) {
		if (data.size() < 1084 || READ_BE_UINT32(&data[1080]) != MKTAG('F', 'L', 'T', '4') ||
				!data[950] || data[950] > 128 || data[951] >= data[950])
			return false;
		const uint patterns = highestPattern(data) + 1;
		uint32 offset = 1084 + patterns * 1024;
		if (offset > data.size())
			return false;
		for (uint i = 0; i < 31; ++i) {
			const byte *header = &data[20 + i * 30];
			const uint length = READ_BE_UINT16(header + 22) * 2;
			const uint repeat = READ_BE_UINT16(header + 26) * 2;
			const uint repeatLength = READ_BE_UINT16(header + 28) * 2;
			if (header[25] > 64 || repeatLength > 32766 || length > data.size() - offset ||
					(repeatLength > 2 && (repeat > length || repeatLength > length - repeat)))
				return false;
			offset += length;
		}
		if (offset != data.size())
			return false;
		for (uint i = 1084; i < 1084 + patterns * 1024; i += 4) {
			const uint sample = (data[i] & 0xf0) | (data[i + 2] >> 4);
			const byte effect = data[i + 2] & 15;
			if (sample > 31 || (effect != 0 && effect != 1 && effect != 2 &&
					effect != 11 && effect != 12 && effect != 13 && effect != 15))
				return false;
			if (effect == 11 && data[i + 3] >= data[950])
				return false;
		}
		return true;
	}

private:
	struct Sample {
		uint32 offset, length, repeat, repeatLength;
		byte volume;
	};
	struct Channel {
		byte sample, effect, parameter, volume;
		uint16 period;
	};

	void interrupt() override {
		// The game's CIA timer calls the replay every fourth interrupt
		// (hunk0:11c78..11c94). Its first row follows six tracker ticks.
		if (++_tick >= _speed) {
			_tick = 0;
			playRow();
		} else {
			for (uint i = 0; i < 4; ++i)
				playEffect(i);
		}
	}

	void playRow() {
		const uint32 offset = 1084 + _data[952 + _position] * 1024 + _row * 16;
		int jump = -1;
		bool patternBreak = false;
		for (uint i = 0; i < 4; ++i) {
			const byte *note = &_data[offset + i * 4];
			Channel &channel = _channels[i];
			const byte sampleNumber = (note[0] & 0xf0) | (note[2] >> 4);
			const uint period = ((note[0] & 15) << 8) | note[1];
			channel.effect = note[2] & 15;
			channel.parameter = note[3];
			if (sampleNumber) {
				channel.sample = sampleNumber;
				const Sample &sample = _samples[sampleNumber - 1];
				channel.volume = sample.volume;
				if (!period) {
					// An instrument-only row changes the DMA repeat registers,
					// without restarting the active sample (M3 pattern0 row14).
					const bool looping = sample.repeatLength > 2;
					setChannelSampleStart(i, looping ? reinterpret_cast<const int8 *>(&_data[sample.offset + sample.repeat]) : nullptr);
					setChannelSampleLen(i, looping ? sample.repeatLength / 2 : 0);
				}
			}
			if (period) {
				channel.period = period;
				if (channel.sample) {
					const Sample &sample = _samples[channel.sample - 1];
					if (sample.length) {
						const int8 *pcm = reinterpret_cast<const int8 *>(&_data[sample.offset]);
						const bool looping = sample.repeatLength > 2;
						// With a nonzero repeat offset, the original DMA's
						// first span stops at the loop end (hunk0:12b6c).
						const uint length = looping && sample.repeat ? sample.repeat + sample.repeatLength : sample.length;
						setChannelData(i, pcm, looping ? pcm + sample.repeat : nullptr,
								length, looping ? sample.repeatLength : 0);
					} else {
						disableChannel(i);
					}
				}
				setChannelPeriod(i, channel.period);
			}
			switch (channel.effect) {
			case 11:
				jump = channel.parameter;
				patternBreak = true;
				break;
			case 12:
				channel.volume = MIN<byte>(channel.parameter, 64);
				break;
			case 13:
				patternBreak = true; // D03 is row zero here, not row three.
				break;
			case 15:
				_speed = CLIP<uint>(channel.parameter, 1, 31);
				break;
			default:
				break;
			}
			setChannelVolume(i, channel.volume);
		}
		if (++_row == 64 || patternBreak) {
			_row = 0;
			_position = jump >= 0 ? jump : _position + 1;
			if (_position >= _data[950])
				_position = _data[951];
		}
	}

	void playEffect(uint index) {
		Channel &channel = _channels[index];
		uint period = channel.period;
		if (!period)
			return;
		switch (channel.effect) {
		case 0:
			if (channel.parameter && _tick % 3) {
				// Standard Amiga periods, hunk0:13478. The original searches
				// the first equal-or-lower period, then transposes it.
				static const uint16 periods[] = {
					856,808,762,720,678,640,604,570,538,508,480,453,
					428,404,381,360,339,320,302,285,269,254,240,226,
					214,202,190,180,170,160,151,143,135,127,120,113
				};
				uint note = 0;
				while (note + 1 < ARRAYSIZE(periods) && period < periods[note])
					++note;
				note += _tick % 3 == 1 ? channel.parameter >> 4 : channel.parameter & 15;
				period = periods[MIN<uint>(note, ARRAYSIZE(periods) - 1)];
			}
			break;
		case 1:
			period = channel.period = MAX<int>(113, (int)period - channel.parameter);
			break;
		case 2:
			period = channel.period = MIN<uint>(856, period + channel.parameter);
			break;
		default:
			break;
		}
		setChannelPeriod(index, period);
	}

	Common::Array<byte> _data;
	Sample _samples[31];
	Channel _channels[4];
	uint _tick, _speed, _position, _row;
};

} // namespace

Audio::AudioStream *makeAmigaMusicStream(const Common::Array<byte> &data, int rate) {
	if (rate < 50 || !AmigaMusicStream::valid(data))
		return nullptr;
	return new AmigaMusicStream(data, rate);
}

} // namespace Dune

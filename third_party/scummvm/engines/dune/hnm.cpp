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
#include "common/events.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "audio/mixer.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/debug.h"
#include "dune/harness.h"
#include "dune/hnm.h"
#include "dune/resource.h"

namespace Dune {

namespace {

const uint kScreenWidth = 320;
const uint kScreenHeight = 200;
// Silent videos carry no timing of their own; the ones with a soundtrack
// average 919 bytes of 11111 Hz audio per frame, i.e. about 12 frames/second.
const uint32 kDefaultFrameMillis = 83;

bool isBlockTag(const byte *data, const char *tag) {
	return data[0] == (byte)tag[0] && data[1] == (byte)tag[1];
}

} // namespace

HnmPlayer::HnmPlayer(OSystem *system) :
		_system(system), _paletteDirty(false), _screen(kScreenWidth * kScreenHeight), _scale(0) {
	memset(_palette, 0, sizeof(_palette));
}

bool HnmPlayer::readPalette(const byte *data, uint32 size, uint32 &position) {
	while (position + 2 <= size) {
		const byte start = data[position];
		const byte rawCount = data[position + 1];
		position += 2;
		if (start == 0xff && rawCount == 0xff)
			return true;

		if (start == 0 && rawCount == 1) {
			// The original leaves colour 0 alone.
			position += 3;
			continue;
		}

		const uint count = rawCount ? rawCount : 256;
		if (start + count > 256 || position + count * 3 > size)
			return false;
		for (uint i = 0; i < count * 3; ++i) {
			const byte value = data[position++] & 0x3f;
			_palette[start * 3 + i] = value << 2;
		}
		_paletteDirty = true;
	}
	return false;
}

bool HnmPlayer::decodeFrame(const byte *block, uint32 size) {
	if (size < 4)
		return false;

	const uint16 header = READ_LE_UINT16(block);
	const uint width = header & 0x01ff;
	const uint height = block[2];
	const bool transparent = block[3] == 0xff;
	if (!width || !height)
		return true; // Hold the previous picture for one more frame.

	const byte *body = block + 4;
	uint32 bodySize = size - 4;

	if (header & 0x0200) {
		if (bodySize < 6)
			return false;
		const uint32 unpackedSize = READ_LE_UINT16(body);
		const uint32 packedSize = READ_LE_UINT16(body + 3);
		if (packedSize < 6 || packedSize > bodySize)
			return false;
		_unpacked.resize(unpackedSize);
		if (!Resource::unpackHSQ(body + 6, packedSize - 6, _unpacked.data(), unpackedSize))
			return false;
		body = _unpacked.data();
		bodySize = unpackedSize;
	}

	uint x = 0, y = 0;
	if (!(header & 0x0400)) {
		if (bodySize < 4)
			return false;
		x = READ_LE_UINT16(body);
		y = READ_LE_UINT16(body + 2);
		body += 4;
		bodySize -= 4;
	}

	// Half-resolution pictures (the speech and cutscene videos, the logo
	// backdrop) are exactly 160 pixels wide and shown pixel-doubled; smaller
	// pieces such as the logos themselves are placed at native resolution.
	_scale = (!transparent && width == kScreenWidth / 2) ? 2 : 1;

	const bool rle = (header & 0x8000) != 0;
	if (!rle && bodySize < width * height)
		return false;

	Common::Array<byte> line(width);
	uint32 position = 0;
	for (uint row = 0; row < height; ++row) {
		const byte *source;
		if (rle) {
			uint column = 0;
			while (column < width) {
				if (position >= bodySize)
					return false;
				const int8 command = (int8)body[position++];
				uint count = (uint)(command < 0 ? -command : command) + 1;
				if (command < 0) {
					if (position >= bodySize)
						return false;
					const byte value = body[position++];
					for (; count && column < width; --count)
						line[column++] = value;
				} else {
					if (position + count > bodySize)
						return false;
					for (uint i = 0; i < count; ++i) {
						if (column < width)
							line[column++] = body[position];
						++position;
					}
				}
			}
			source = line.data();
		} else {
			source = body + row * width;
		}

		for (uint repeatY = 0; repeatY < _scale; ++repeatY) {
			const uint targetY = (y + row) * _scale + repeatY;
			if (targetY >= kScreenHeight)
				break;
			byte *target = _screen.data() + targetY * kScreenWidth;
			for (uint column = 0; column < width; ++column) {
				const byte pixel = source[column];
				if (transparent && !pixel)
					continue;
				for (uint repeatX = 0; repeatX < _scale; ++repeatX) {
					const uint targetX = (x + column) * _scale + repeatX;
					if (targetX < kScreenWidth)
						target[targetX] = pixel;
				}
			}
		}
	}

	return true;
}

void HnmPlayer::present() {
	if (_paletteDirty) {
		_system->getPaletteManager()->setPalette(_palette, 0, 256);
		_paletteDirty = false;
	}
	if (_top) {
		// Shifted down, the rows below the picture's end fall off the screen.
		Common::Array<byte> shifted(kScreenWidth * kScreenHeight, 0);
		memcpy(shifted.data() + _top * kScreenWidth, _screen.data(), (kScreenHeight - _top) * kScreenWidth);
		_system->copyRectToScreen(shifted.data(), kScreenWidth, 0, 0, kScreenWidth, kScreenHeight);
		_system->updateScreen();
		return;
	}
	Graphics::Surface screen;
	screen.init(kScreenWidth, kScreenHeight, kScreenWidth, _screen.data(), Graphics::PixelFormat::createFormatCLUT8());
	debugOverlay(screen);
	_system->copyRectToScreen(_screen.data(), kScreenWidth, 0, 0, kScreenWidth, kScreenHeight);
	_system->updateScreen();
}

bool HnmPlayer::begin(const Common::Array<byte> &data) {
	_stream = &data;
	_streamOffset = 0;
	_streamFrame = 0;
	_scale = 0;
	memset(_screen.data(), 0, _screen.size());
	const byte *file = data.data();
	if (data.size() < 4)
		return false;
	const uint32 chunkSize = READ_LE_UINT16(file);
	uint32 position = 2;
	if (chunkSize < 4 || chunkSize > data.size() || !readPalette(file, chunkSize, position))
		return false;
	_streamOffset = chunkSize;
	return true;
}

bool HnmPlayer::step() {
	if (!_stream)
		return false;
	const byte *file = _stream->data();
	const uint32 fileSize = _stream->size();
	while (_streamOffset + 2 <= fileSize) {
		const uint32 chunkSize = READ_LE_UINT16(file + _streamOffset);
		if (chunkSize < 2 || _streamOffset + chunkSize > fileSize)
			return false;
		const byte *chunk = file + _streamOffset;
		_streamOffset += chunkSize;
		uint32 position = 2;
		while (position + 4 <= chunkSize) {
			const byte *block = chunk + position;
			const uint32 blockSize = READ_LE_UINT16(block + 2);
			if (isBlockTag(block, "sd") || isBlockTag(block, "pl") || isBlockTag(block, "pt") || isBlockTag(block, "kl") ||
					isBlockTag(block, "mm")) {
				if (blockSize < 4 || position + blockSize > chunkSize)
					return false;
				if (isBlockTag(block, "pl")) {
					uint32 palettePosition = 4;
					readPalette(block, blockSize, palettePosition);
				}
				position += blockSize;
				continue;
			}
			if (!decodeFrame(block, chunkSize - position))
				return false;
			++_streamFrame;
			return true;
		}
	}
	return false;
}

bool HnmPlayer::lastFrame(const Common::Array<byte> &data, byte *pixels, byte *palette) {
	const byte *file = data.data();
	const uint32 fileSize = data.size();
	if (fileSize < 4)
		return false;
	memset(_screen.data(), 0, _screen.size());
	memset(_palette, 0, sizeof(_palette));
	_scale = 0;

	uint32 chunkSize = READ_LE_UINT16(file);
	uint32 position = 2;
	if (chunkSize < 4 || chunkSize > fileSize || !readPalette(file, chunkSize, position))
		return false;
	uint32 offset = chunkSize;
	uint frames = 0;
	while (offset + 2 <= fileSize) {
		chunkSize = READ_LE_UINT16(file + offset);
		if (chunkSize < 2 || offset + chunkSize > fileSize)
			break;
		const byte *chunk = file + offset;
		position = 2;
		while (position + 4 <= chunkSize) {
			const byte *block = chunk + position;
			const uint32 blockSize = READ_LE_UINT16(block + 2);
			if (isBlockTag(block, "sd") || isBlockTag(block, "pl") || isBlockTag(block, "pt") || isBlockTag(block, "kl") ||
					isBlockTag(block, "mm")) {
				if (blockSize < 4 || position + blockSize > chunkSize)
					return false;
				if (isBlockTag(block, "pl")) {
					uint32 palettePosition = 4;
					readPalette(block, blockSize, palettePosition);
				}
				position += blockSize;
				continue;
			}
			if (!decodeFrame(block, chunkSize - position))
				return false;
			++frames;
			break;
		}
		offset += chunkSize;
	}
	memcpy(pixels, _screen.data(), _screen.size());
	memcpy(palette, _palette, sizeof(_palette));
	return frames > 0;
}

HnmPlayer::Result HnmPlayer::play(const Common::Array<byte> &data, const char *name, int dumpFrame) {
	debugSetScene(Common::String::format("video/%s", name));
	debugSetRoom(-1);
	debugSetAudioStream(Common::String::format("video:%s starting", name));
	const byte *file = data.data();
	const uint32 fileSize = data.size();
	if (fileSize < 4)
		return kError;

	memset(_screen.data(), 0, _screen.size());
	_scale = 0;

	uint32 offset = 0;
	uint32 chunkSize = READ_LE_UINT16(file);
	uint32 position = 2;
	if (chunkSize < 4 || chunkSize > fileSize || !readPalette(file, chunkSize, position)) {
		warning("Dune: %s has no valid HNM header", name);
		return kError;
	}
	offset = chunkSize;

	const bool timed = dumpFrame < 0;
	Audio::Mixer *mixer = _system->getMixer();
	Audio::QueuingAudioStream *audio = nullptr;
	Audio::SoundHandle audioHandle;
	uint32 audioRate = 0;
	uint32 audioBytes = 0;

	Result result = kFinished;
	const uint32 startTime = _system->getMillis();
	uint32 frame = 0;

	while (offset + 2 <= fileSize) {
		chunkSize = READ_LE_UINT16(file + offset);
		if (chunkSize < 2 || offset + chunkSize > fileSize)
			break;

		const byte *chunk = file + offset;
		position = 2;
		// When does this frame go on screen? Audio queued so far decides.
		const uint32 frameTime = audioRate ? (uint32)((uint64)audioBytes * 1000 / audioRate) : frame * kDefaultFrameMillis;

		while (position + 4 <= chunkSize) {
			const byte *block = chunk + position;
			const uint32 blockSize = READ_LE_UINT16(block + 2);
			const bool sound = isBlockTag(block, "sd");
			const bool palette = isBlockTag(block, "pl");
			if (sound || palette || isBlockTag(block, "pt") || isBlockTag(block, "kl") || isBlockTag(block, "mm")) {
				if (blockSize < 4 || position + blockSize > chunkSize) {
					result = kError;
					break;
				}
				if (palette) {
					uint32 palettePosition = 4;
					readPalette(block, blockSize, palettePosition);
				} else if (sound && timed) {
					const byte *samples = block + 4;
					uint32 sampleCount = blockSize - 4;
					if (!audio && sampleCount > 32 && !memcmp(samples, "Creative Voice File\x1a", 20)) {
						// Header, then one type-1 block: size[3], divisor, codec.
						const uint32 headerSize = READ_LE_UINT16(samples + 20);
						if (headerSize + 6 <= sampleCount && samples[headerSize] == 1 && samples[headerSize + 5] == 0) {
							audioRate = 1000000 / (256 - samples[headerSize + 4]);
							samples += headerSize + 6;
							sampleCount -= headerSize + 6;
							audio = Audio::makeQueuingAudioStream(audioRate, false);
							mixer->playStream(Audio::Mixer::kSFXSoundType, &audioHandle, audio);
						}
					}
					if (audio && sampleCount) {
						byte *copy = (byte *)malloc(sampleCount);
						memcpy(copy, samples, sampleCount);
						audio->queueBuffer(copy, sampleCount, DisposeAfterUse::YES, Audio::FLAG_UNSIGNED);
						audioBytes += sampleCount;
					}
				}
				position += blockSize;
				continue;
			}

			if (!decodeFrame(block, chunkSize - position)) {
				warning("Dune: %s frame %u could not be decoded", name, frame);
				result = kError;
			}
			break;
		}
		if (result != kFinished)
			break;

		if (timed) {
			// Wait for this frame's slot while staying responsive to input.
			Common::Event event;
			do {
				while (pollDuneEvent(_system, event)) {
					switch (event.type) {
					case Common::EVENT_QUIT:
					case Common::EVENT_RETURN_TO_LAUNCHER:
						result = kQuit;
						break;
					case Common::EVENT_LBUTTONDOWN:
					case Common::EVENT_RBUTTONDOWN:
						result = kSkipped;
						break;
					case Common::EVENT_KEYDOWN:
						if (event.kbd.keycode == Common::KEYCODE_ESCAPE || event.kbd.keycode == Common::KEYCODE_RETURN ||
								event.kbd.keycode == Common::KEYCODE_SPACE)
							result = kSkipped;
						break;
					default:
						break;
					}
				}
				if (result != kFinished)
					break;
				const uint32 elapsed = _system->getMillis() - startTime;
				if (elapsed >= frameTime)
					break;
				_system->delayMillis(MIN<uint32>(10, frameTime - elapsed));
			} while (true);
			if (result != kFinished)
				break;
		}

		if (_hook)
			_hook(_hookContext, frame, _screen.data());
		present();
		if (!timed && (int)frame == dumpFrame) {
			dumpScreen(_system, name);
			break;
		}

		++frame;
		offset += chunkSize;
	}

	if (audio) {
		if (result == kFinished) {
			// Let the queued tail of the soundtrack play out.
			audio->finish();
			while (mixer->isSoundHandleActive(audioHandle) && !audio->endOfData())
				_system->delayMillis(10);
		}
		mixer->stopHandle(audioHandle);
	}
	debugSetAudioStream(Common::String::format("video:%s stopped", name));

	debug(1, "Dune: %s played %u frames (result %d)", name, frame, (int)result);
	return result;
}

} // namespace Dune

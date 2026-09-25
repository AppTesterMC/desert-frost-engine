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

#include "common/system.h"
#include "common/util.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/attack.h"
#include "dune/sprite.h"

namespace Dune {

namespace {

const uint32 kTickMicros = 14978; // 3 x 4992.53 us, the PIT tick the original waits for
const uint kMaxTicksPerUpdate = 4;
const uint32 kMaskedLcgPrime = 0x0e56d;
const uint32 kLcgPrime = 0xcbd1;

const int16 kInitialPositions[4][2] = { { 125, 101 }, { 100, 101 }, { 239, 122 }, { 271, 125 } };
const int8 kInitialVelocities[4][2] = { { -6, 4 }, { -4, 6 }, { -4, -6 }, { -6, -4 } };

} // namespace

NightAttack::NightAttack(OSystem *system, Sprite &sheet, bool massive) :
		_system(system), _sheet(sheet), _frameCount(sheet.frameCount()), _massive(massive) {
	memset(_particles, 0, sizeof(_particles));
	loadFlashPalette(53, _flash53);
	loadFlashPalette(54, _flash54);
	loadFlashPalette(55, _flash55);
	// The sky starts as whatever the sheet's palette put at 128-155.
	byte palette[256 * 3];
	_system->getPaletteManager()->grabPalette(palette, 0, 256);
	memcpy(_skyPalette, palette + kSkyPaletteStart * 3, sizeof(_skyPalette));
	memcpy(_skyTarget, _skyPalette, sizeof(_skyTarget));
	reset();
}

void NightAttack::loadFlashPalette(uint16 record, byte *rgb) {
	byte colours[256 * 3];
	uint start, count;
	memset(rgb, 0, kSkyPaletteCount * 3);
	if (_sheet.getPaletteRecord(record, colours, start, count))
		memcpy(rgb, colours, MIN<uint>(count, kSkyPaletteCount) * 3);
}

void NightAttack::reset() {
	_rngSeed = 0x01d2;
	_maskedRngSeed = 0x0273;
	_randomBits = 0x7302;
	_tickAccumulator = 0;
	_timer0.value = 0; _timer0.limit = 0;
	_timer1.value = 0; _timer1.limit = 0;
	_timer4.value = 0; _timer4.limit = 0;
	_timer6.value = 0; _timer6.limit = 0;
	_timer7.value = 0; _timer7.limit = 17;
	_burstLo = _burstHi = 0;
	_particleCount = 0;
}

uint16 NightAttack::maskedRandom(uint16 mask) {
	const uint32 product = _rngSeed * kMaskedLcgPrime + 1;
	_rngSeed = product & 0xffff;
	return (uint16)(product >> 8) & mask;
}

uint16 NightAttack::randomWord() {
	const uint32 product = _maskedRngSeed * kLcgPrime + 1;
	_maskedRngSeed = product & 0xffff;
	return (uint16)(product >> 8);
}

void NightAttack::rotateRandomBits(uint count) {
	_randomBits = (uint16)((_randomBits << count) | (_randomBits >> (16 - count)));
}

void NightAttack::update(uint32 elapsedMillis) {
	_tickAccumulator += elapsedMillis * 1000;
	uint ticks = 0;
	while (_tickAccumulator >= kTickMicros && ticks < kMaxTicksPerUpdate) {
		step();
		_tickAccumulator -= kTickMicros;
		++ticks;
	}
	if (_tickAccumulator >= kTickMicros)
		_tickAccumulator = 0; // Dropped frames must not burst later.
}

void NightAttack::step() {
	_timer7.tick();
	if (_timer7.triggered())
		updateSkyFlash(_timer7.value == 16);

	if (_timer4.tick()) {
		if (_timer6.tick()) {
			const uint16 value = randomWord();
			_timer6.value = (int8)(value & 0x7f);
			_timer4.value = (int16)(value >> 8);
		} else {
			const uint16 value = randomWord();
			const int16 x = (int16)(((value & 0x80) << 1) | (value >> 8));
			const int16 y = (int16)(value & 0x7f);
			if (y >= 48 && y < 96 && x < 320)
				spawnParticle(kFirstAirBomb + (uint16)(y % 8), x, y, 0, 0);
		}
	}

	if (_timer0.tick())
		spawnAttackParticle();

	const uint count = _particleCount;
	for (uint i = 0; i < count && i < _particleCount;)
		updateParticle(i);
}

void NightAttack::spawnAttackParticle() {
	if (_timer1.tick()) {
		if ((_randomBits & 3) == 0) {
			_timer7.value = 11;
			if ((_randomBits & 0x0c) == 0)
				_timer7.value = 17;
		}
		uint16 ax = randomWord();
		if (_massive)
			ax &= 0xffef;
		uint16 cx = ax;
		const uint16 masked = maskedRandom(7);
		_timer1.value = (int8)(masked & 0xff);
		if ((masked & 0xff) >= 4)
			cx |= 0x4000;
		_burstLo = (byte)cx;
		_burstHi = (byte)(cx >> 8);
	}

	_timer0.value = 8;
	if ((_burstLo & 0x10) == 0) {
		const uint index = (_burstHi & 6) >> 1;
		finishSpawn((uint16)(index + 1) * 4, _burstLo, kInitialVelocities[index][0], kInitialVelocities[index][1]);
	} else {
		byte al = _burstHi & 0x3f;
		byte ah = _burstHi & 0xc0;
		if (ah & 0x40) {
			rotateRandomBits(1);
			if (_randomBits & 1) {
				byte cl = 0x0a;
				if (ah & 0x80)
					cl = (byte)(0 - cl);
				al = (byte)(al + cl);
				if (al & 0x80) {
					ah ^= 0x80;
					al = 0;
				}
				if (al >= 0x40) {
					al = 0x3f;
					ah ^= 0x80;
				}
				ah |= al;
				_burstHi = ah;
			}
		}
		al = (byte)(al + 0xe0);
		int8 dx, dy;
		travelHeadingDeltas(al, dx, dy);
		finishSpawn(0x14, _burstLo, dx, dy);
	}
}

void NightAttack::finishSpawn(uint16 spriteId, uint16 originPacked, int8 vx, int8 vy) {
	const uint origin = (originPacked & 0x0c) >> 2;
	if (spawnParticle(spriteId, kInitialPositions[origin][0], kInitialPositions[origin][1], vx, vy)) {
		_particles[_particleCount - 1].accumX = 0;
		_particles[_particleCount - 1].accumY = 0;
	}
}

bool NightAttack::spawnParticle(uint16 spriteId, int16 cx, int16 cy, int8 vx, int8 vy) {
	uint16 width, height;
	if (_particleCount >= kMaxParticles || spriteId >= _frameCount || !_sheet.frameSize(spriteId, width, height))
		return false;
	Particle &p = _particles[_particleCount++];
	p.x = (int16)CLIP<int32>((int32)cx - width / 2, -32768, 32767);
	p.y = (int16)CLIP<int32>((int32)cy - height / 2, -32768, 32767);
	p.width = width;
	p.height = height;
	p.spriteId = spriteId;
	p.velocityX = vx;
	p.velocityY = vy;
	p.flags = 0;
	p.accumX = p.accumY = 0;
	return true;
}

void NightAttack::removeParticle(uint index) {
	if (!_particleCount || index >= _particleCount)
		return;
	_particles[index].flags |= 0x80;
	for (uint i = index; i + 1 < _particleCount; ++i)
		_particles[i] = _particles[i + 1];
	--_particleCount;
}

// One axis of the tracer's sub-pixel motion: the magnitude accumulates in
// the low five bits and the carry above them becomes the pixel step.
void NightAttack::stepAxis(int8 &accumulator, int8 &velocity) {
	const byte magnitude = (byte)(velocity < 0 ? -velocity : velocity);
	const uint16 sum = (uint16)((byte)(magnitude + (byte)accumulator));
	const uint16 rotated = (uint16)((sum >> 5) | (sum << 11));
	accumulator = (int8)(rotated >> 11);
	int8 stepValue = (int8)rotated;
	velocity = velocity < 0 ? (int8)(0 - stepValue) : stepValue;
}

void NightAttack::updateParticle(uint &index) {
	Particle &p = _particles[index];
	uint16 spriteId = p.spriteId;
	int8 vx = p.velocityX, vy = p.velocityY;
	const byte lo = spriteId & 0xff;
	if (lo < 0x14) {
		spriteId >>= 2;
		rotateRandomBits(1);
		spriteId = (uint16)((spriteId << 1) | (_randomBits & 1));
		rotateRandomBits(1);
		spriteId = (uint16)((spriteId << 1) | (_randomBits & 1));
	} else if (lo < 0x1c) {
		int8 accumX = (int8)p.accumX, accumY = (int8)p.accumY;
		stepAxis(accumX, vy);
		stepAxis(accumY, vx);
		p.accumX = (byte)accumX;
		p.accumY = (byte)accumY;
		rotateRandomBits(3);
		spriteId = (uint16)((_randomBits & 7) + 0x14);
	} else {
		++spriteId;
		if ((spriteId & 0xff) > 0x2d) {
			removeParticle(index);
			++index;
			return;
		}
	}
	p.spriteId = spriteId;
	// The original stores the pair swapped: the first byte moves y.
	p.x = (int16)(p.x + vy);
	p.y = (int16)(p.y + vx);

	const int16 right = (int16)(p.x + (int16)p.width);
	const int16 bottom = (int16)(p.y + (int16)p.height);
	if ((uint16)p.x >= 320 || right < 0 || bottom < 0)
		removeParticle(index);
	++index;
}

void NightAttack::travelHeadingDeltas(byte value, int8 &dxOut, int8 &dyOut) {
	const byte bl = (byte)(value + 0x20);
	const byte bh = bl & 0x7f;
	uint16 bx;
	int16 dx;
	if (bh < 0x40) {
		bx = 0xffe0;
		if ((int8)bl >= 0) {
			dx = (int8)value;
		} else {
			byte al = (byte)(value - 0x80);
			al = (byte)(0 - al);
			bx = (uint16)(0 - (int16)bx);
			dx = (int8)al;
		}
	} else {
		dx = 0x20;
		byte al = (byte)(value - 0x40);
		if ((int8)bl < 0) {
			dx = (int16)(0 - dx);
			al = (byte)(al - 0x80);
			al = (byte)(0 - al);
		}
		bx = (uint16)(int16)(int8)al;
	}
	dxOut = (int8)(byte)bx;
	dyOut = (int8)(byte)dx;
}

void NightAttack::updateSkyFlash(bool bright) {
	if (!bright && _timer7.value != 10) {
		lerpSkyPalette();
		return;
	}
	memcpy(_skyPalette, bright ? _flash55 : _flash54, sizeof(_skyPalette));
	memcpy(_skyTarget, _flash53, sizeof(_skyTarget));
	applySkyPalette();
}

void NightAttack::lerpSkyPalette() {
	const int divisor = MAX<int>(_timer7.value, 1);
	for (uint i = 0; i < kSkyPaletteCount * 3; ++i) {
		const int from = _skyPalette[i] >> 2, to = _skyTarget[i] >> 2;
		const int mixed = from + (to - from) / divisor;
		_skyPalette[i] = (byte)CLIP(mixed << 2, 0, 255);
	}
	applySkyPalette();
}

void NightAttack::applySkyPalette() {
	_system->getPaletteManager()->setPalette(_skyPalette, kSkyPaletteStart, kSkyPaletteCount);
}

void NightAttack::draw(Graphics::Surface &view) {
	view.fillRect(Common::Rect(0, 0, view.w, view.h), 0);
	for (int x = 0; x < 320; x += 40) {
		_sheet.drawFrame(2, &view, x, 0);
		_sheet.drawFrame(3, &view, x, 81);
	}
	_sheet.drawFrame(49, &view, 0, 76);
	_sheet.drawFrame(1, &view, 0, 134);
	for (uint i = 0; i < _particleCount; ++i) {
		const Particle &p = _particles[i];
		if (!(p.flags & 0x80) && p.spriteId < _frameCount)
			_sheet.drawFrame(p.spriteId, &view, p.x, p.y);
	}
}

} // namespace Dune

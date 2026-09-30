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

#ifndef ENGINES_DUNE_ATTACK_H
#define ENGINES_DUNE_ATTACK_H

#include "common/array.h"
#include "common/scummsys.h"

class OSystem;

namespace Graphics {
struct Surface;
}

namespace Dune {

class Sprite;

/**
 * The night attack: green tracer fire and air bombs over a city silhouette
 * (ATTACK.HSQ), seen in the intro and whenever the player watches a battle.
 *
 * This is the original's particle simulation, transcribed from the
 * disassembly by madmoose (dune-rust, crates/dune/src/attack) and ported to
 * Swift by codingstyle (swift-dune, Game/Scenes/Attack.swift); both allowed
 * this reuse. Timers, two linear-congruential generators, a rotating bit
 * source and the byte-level particle rules are kept exactly, so the
 * pattern of lights matches the original for the same seeds. The sky
 * flashes by lerping colours 128-155 towards palette records 53-55 of the
 * sheet. One simulation tick is 3 PIT ticks (about 15 ms).
 */
class NightAttack {
public:
	NightAttack(OSystem *system, Sprite &sheet, bool massive = false);

	/** Advance the simulation by the wall time that passed. */
	void update(uint32 elapsedMillis);
	/** Draw the background and every live particle into a 320x152 view. */
	void draw(Graphics::Surface &view);
	/** Write the current sky colours (128-155) into the screen palette. */
	void applySkyPalette();
	/**
	 * The backdrop between the sky and the ground (the icon list at CD
	 * ds:11dd, patched by location_arrival_hostility_check 505f-5075): 0x31
	 * in the intro; in the game 0x2f for a sietch, 0x30 for the palace, a
	 * village or the Harkonnen palace, 0x33 for a fortress.
	 */
	void setBackdrop(uint16 sprite) { _backdrop = sprite; }
	/** set_massive_attack (CD 7317 / 7391): the denser fire during MASSIVE ATTACK. */
	void setMassive(bool massive) { _massive = massive; }
	/** The sky flash's timer (MASSIVE ATTACK's rounds force 0x0b or 0x11, CD 7370-738f). */
	void setSkyFlashTimer(int8 value) { _timer7.value = value; }

private:
	enum {
		kMaxParticles = 64,
		kSkyPaletteStart = 128,
		kSkyPaletteCount = 28,
		kFirstAirBomb = 28
	};

	struct Particle {
		int16 x, y;
		uint16 width, height;
		uint16 spriteId;
		int8 velocityX, velocityY;
		byte flags;
		byte accumX, accumY;
	};

	template<typename T>
	struct Timer {
		T value, limit;
		bool triggered() const { return value < limit; }
		bool tick() { value = (T)(value - 1); return triggered(); }
	};

	void reset();
	void step();
	void spawnAttackParticle();
	void finishSpawn(uint16 spriteId, uint16 originPacked, int8 vx, int8 vy);
	bool spawnParticle(uint16 spriteId, int16 cx, int16 cy, int8 vx, int8 vy);
	void removeParticle(uint index);
	void updateParticle(uint &index);
	uint16 maskedRandom(uint16 mask);
	uint16 randomWord();
	void rotateRandomBits(uint count);
	static void stepAxis(int8 &accumulator, int8 &velocity);
	static void travelHeadingDeltas(byte value, int8 &dx, int8 &dy);
	void updateSkyFlash(bool bright);
	void lerpSkyPalette();
	void loadFlashPalette(uint16 record, byte *rgb);

	OSystem *_system;
	Sprite &_sheet;
	uint16 _frameCount;
	bool _massive;
	uint16 _backdrop = 49;

	uint32 _rngSeed, _maskedRngSeed;
	uint16 _randomBits;
	uint32 _tickAccumulator;
	Timer<int8> _timer0, _timer1, _timer6, _timer7;
	Timer<int16> _timer4;
	byte _burstLo, _burstHi;

	Particle _particles[kMaxParticles];
	uint _particleCount;

	byte _skyPalette[kSkyPaletteCount * 3];
	byte _skyTarget[kSkyPaletteCount * 3];
	byte _flash53[kSkyPaletteCount * 3];
	byte _flash54[kSkyPaletteCount * 3];
	byte _flash55[kSkyPaletteCount * 3];
};

} // namespace Dune

#endif // ENGINES_DUNE_ATTACK_H

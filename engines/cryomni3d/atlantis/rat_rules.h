/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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
 */

#ifndef CRYOMNI3D_ATLANTIS_RAT_RULES_H
#define CRYOMNI3D_ATLANTIS_RAT_RULES_H

#include "common/array.h"
#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The rules of the rat's run (StartPuzzle 4, FUN_00438bf0) as atlantis.exe
// runs them, on a copy of its state: the 0x318 bytes at 0x004969c0 (the
// 30 records of 0x17 bytes at 0x004969c8 and the globals after them).  The
// original reads overlapping shorts and ints there, so the state is kept
// byte for byte and addressed by the original addresses.
class RatRules {
public:
	static const uint32 kBase = 0x004969c0;
	static const uint32 kSize = 0x318;      // the state the tests compare
	// The memory kept: up to 0x0049727f, because the rat's animation entry
	// (0x00496c7a + 6 * byte) is indexed by any byte value, and before the
	// first trajectory the byte is still 0x68 from the data section.
	static const uint32 kMemSize = 0x8c0;
	static const int kNumRecords = 30;

	byte mem[kMemSize];
	uint32 clockTicked;   // 0x00772d90
	int32 result;         // 0x004974c0

	RatRules();

	// FUN_004387ec: the reset before each rat.
	void reset();
	// FUN_004395f4: the click (FUN_0043500c first) and held-button rules.
	void clicks(int mx, int my, bool click, bool held, Common::Array<int> &sounds);
	// FUN_0043947c: the rat's animation step.
	void animateRat();
	// FUN_00439d58 for record i at time ticks (200 Hz).
	void updateRecord(int i, uint32 ticks, Common::Array<int> &sounds);
	// The updates of FUN_00438fd4: FUN_0043947c, then FUN_00439d58 for
	// records 0..27 in its order and conditions.
	void frameUpdates(uint32 ticks, Common::Array<int> &sounds);

	// Byte, short, int and float at an original address.
	byte b(uint32 a) const { return mem[a - kBase]; }
	void setB(uint32 a, byte v) { mem[a - kBase] = v; }
	int16 s(uint32 a) const { return (int16)(mem[a - kBase] | (mem[a - kBase + 1] << 8)); }
	void setS(uint32 a, int v) { mem[a - kBase] = (byte)v; mem[a - kBase + 1] = (byte)(v >> 8); }
	int32 i(uint32 a) const { return (int32)((uint32)mem[a - kBase] | ((uint32)mem[a - kBase + 1] << 8) | ((uint32)mem[a - kBase + 2] << 16) | ((uint32)mem[a - kBase + 3] << 24)); }
	void setI(uint32 a, int32 v) { for (int k = 0; k < 4; k++) mem[a - kBase + k] = (byte)((uint32)v >> (8 * k)); }
	float f(uint32 a) const;
	void setF(uint32 a, float v);

	// Record r's field at offset off (0x004969c8 + 0x17 * r + off).
	static uint32 rec(int r, int off) { return 0x004969c8 + 0x17 * r + off; }

	// Record fields.
	enum {
		kAbove = 0, kActive = 1, kPressed = 2, kKind = 5, kX = 7, kY = 9,
		kRest = 0xb, kPressedFrame = 0xd, kFrame = 0xf, kPartner = 0x11,
		kCounter = 0x13, kSound = 0x15
	};
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

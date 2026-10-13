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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_DISPATCH_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_DISPATCH_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The checks of the puzzle dispatcher atlantis.exe FUN_0043a1e0 before it
// runs a puzzle.  StartPuzzle 15 first empties the hand (OBJETENMAIN = 0,
// 0x0043a248); then puzzles 0..5, 8, 9 and above 14 do not start while an
// object is held, and return DAT_00496d0c (1).  Numbers above 16 have no
// entry and return 0.
struct PuzzleDispatch {
	enum { kNumPuzzles = 17, kRun = -1 };

	// The entry function of each StartPuzzle (FUN_0043a1e0's jump table).
	static uint32 entry(int id) {
		static const uint32 kEntries[kNumPuzzles] = {
			0x0044154c, 0x004441e8, 0x0044203c, 0x004459c0, 0x00438bf0, 0x0043fcb4,
			0x00446300, 0x0044659c, 0x0043b0f4, 0x0044763c, 0x0044763c, 0x0044763c,
			0x0044763c, 0x004404fc, 0x00446830, 0x0043d28c, 0x0043ee54
		};
		return (id >= 0 && id < kNumPuzzles) ? kEntries[id] : 0;
	}

	// heldObject is OBJETENMAIN, updated in place.  Returns kRun when the
	// puzzle starts, else the dispatcher's result.
	static int check(int id, int &heldObject) {
		if (id == 15)
			heldObject = 0;
		if ((id < 6 || id == 8 || id == 9 || id > 14) && heldObject != 0)
			return 1;
		if (id < 0 || id >= kNumPuzzles)
			return 0;
		return kRun;
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

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

#ifndef CRYOMNI3D_ATLANTIS_RANDOM_H
#define CRYOMNI3D_ATLANTIS_RANDOM_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The C rand of atlantis.exe (Watcom, @ 0x00453cd1).  srand (0x00453cf5)
// has no caller, so the seed starts at 1 at every launch.
inline int crtRandNext(uint32 &seed) {
	seed = seed * 1103515245u + 12345u;
	return (int)((seed >> 16) & 0x7fff);
}

// The puzzle LCG FUN_00447524 (s16 seed 0x007bec20): 0..n.
inline int puzzleLcgNext(uint16 &seed, int n) {
	int mask = 0;
	for (int v = (int16)n; v > 0; v = (int16)v >> 1)
		mask = mask * 2 + 1;
	int v;
	do {
		seed = (uint16)((int32)(int16)seed * -0x342f + 1);
		v = ((int)(int16)seed / 256) & mask;
	} while ((int16)v > n);
	return v;
}

// FUN_004422b8 (ogre puzzle): the queen's cell, two draws of
// FUN_00447524(6) per try until x + y >= 10 and (6 - x) + (6 - y) >= 2.
inline void ogreQueenCell(uint16 &seed, int &x, int &y) {
	do {
		x = puzzleLcgNext(seed, 6);
		y = puzzleLcgNext(seed, 6);
	} while (x + y < 10 || (6 - x) + (6 - y) < 2);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

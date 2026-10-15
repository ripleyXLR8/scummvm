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

#ifndef CRYOMNI3D_ATLANTIS_WARP_MOMENTUM_H
#define CRYOMNI3D_ATLANTIS_WARP_MOMENTUM_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The view momentum of the warp loop, atlantis.exe FUN_0041bfa0 step 4
// (0x0041c213..0x0041c2b5): with the mouse deltas dx = prevX - x and
// dy = prevY - y (0x006ce05c / 0x006ce060, floats),
//     vx = vx + dx / 3000.0; alpha += vx; vx *= 0.8
//     vy = vy + dy / 4000.0; beta  -= vy; vy *= 0.8
// in doubles (0x004bc2f7 = 3000.0, 0x004bc307 = 4000.0, 0x004bc2ff = 0.8)
// stored back to the floats 0x00496984 / 0x00496988 and the angles
// 0x00bcb59c / 0x00bcb598.  A delta d turns the view by d / 600 (d / 800)
// in total, spread over the following frames.  Nothing resets vx / vy.
// See atlantis-re: functions/graphics-FUN_0041bfa0.md.
struct WarpMomentum {
	float vx, vy;

	WarpMomentum() : vx(0.0f), vy(0.0f) {}

	void step(float dx, float dy, float &alpha, float &beta) {
		vx = (float)((double)dx / 3000.0 + (double)vx);
		alpha = vx + alpha;
		vx = (float)((double)vx * 0.8);
		vy = (float)((double)dy / 4000.0 + (double)vy);
		beta = beta - vy;
		vy = (float)((double)vy * 0.8);
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

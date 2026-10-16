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

// ANGLEWARPX / ANGLEWARPY of a view angle (FUN_0041bfa0 @ 0x0041c08b..
// 0x0041c10a): angle / pi * 180 at 53-bit precision with the float pi of
// 0x00bcb594 and the float 180.0 of 0x004bc2f3, stored to a float, then
// truncated (FUN_00451d6e, FISTP).  Emulated for every degree of both
// axes: atlantis-re tools/emu/graphics_anglewarp.py.
inline int angleWarpDegrees(float angle, float pi) {
	const float t = (float)((double)angle / (double)pi * 180.0);
	return (int)t;
}

// FUN_00451915's angle rule: beta clamped to +-betaMax (0.9 x the vertical
// limit), alpha brought into [0, 2 pi) by one float 2 pi; both kept in the
// view-angle globals.  The same code as Omni3DManagerAtlantis.
inline void warpWrapAngles(float &alpha, float &beta, float twoPi, double betaMax) {
	if (beta > betaMax)
		beta = (float)betaMax;
	else if (beta < -betaMax)
		beta = (float)-betaMax;
	if (!(alpha < twoPi))
		alpha = (float)((double)alpha - twoPi);
	else if (alpha < 0.f)
		alpha = (float)((double)twoPi + alpha);
}

// FUN_0041bfa0 steps 2..5 on the stored view angles (0x00bcb59c /
// 0x00bcb598): ANGLEWARPX / ANGLEWARPY from the angles as the frame starts,
// then the mouse deltas of the pointer motion (x right, y down):
// dx = -motion x; dy = -motion y in OMNI 3D MODE 1 (0x004960ac == 0),
// +motion y in MODE 2 (0x0041c1c3..0x0041c20d); the momentum; and, when the
// new angles differ from the previous frame's, FUN_00451915's wrap and clamp
// (0x0041c2bb..0x0041c2e9).  alpha / beta are updated in place.
struct WarpLookFrame {
	int angleWarpX, angleWarpY;
	float newAlpha, newBeta;   // before FUN_00451915 (the recentre test)
	bool moved;
};

inline WarpLookFrame warpLookFrame(WarpMomentum &m, float &alpha, float &beta, int motionX,
                                   int motionY, bool mode2, float prevAlpha, float prevBeta,
                                   float pi, float twoPi, double betaMax) {
	WarpLookFrame f;
	f.angleWarpX = angleWarpDegrees(alpha, pi);
	f.angleWarpY = angleWarpDegrees(beta, pi);
	m.step((float)-motionX, (float)(mode2 ? motionY : -motionY), alpha, beta);
	f.newAlpha = alpha;
	f.newBeta = beta;
	f.moved = alpha != prevAlpha || beta != prevBeta;
	if (f.moved)
		warpWrapAngles(alpha, beta, twoPi, betaMax);
	return f;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

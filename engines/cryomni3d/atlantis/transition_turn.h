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

#ifndef CRYOMNI3D_ATLANTIS_TRANSITION_TURN_H
#define CRYOMNI3D_ATLANTIS_TRANSITION_TURN_H

#include "common/array.h"
#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// atlantis.exe FUN_004212cc: before a transition video, the view turns to
// the transition's source angles (srcAlpha, srcBeta of the WAM record),
// zooming in by 3 degrees a frame when the CON variable ZOOM is set, then
// jumps to the destination angles.  The x87 runs at 53-bit precision and
// every intermediate the original stores as a float is stored as one here,
// so the angles match the original's bit for bit.
//
// Phase 1 runs while the stop tests fail: |alpha - srcAlpha| and
// |beta - srcBeta| both below 0.05, or one of two stall counters reaching
// 5.  A counter goes up on a frame whose truncated |difference| times 1000
// equals the value kept from an earlier frame (the truncated value times
// 100), which only happens when the truncated difference is 0 (FUN_00451d6e
// rounds toward zero: it sets RC = chop around FRNDINT).  The kept
// values start uninitialised in the exe (stack left-overs, not 0 under
// emulation); they are taken here as matching nothing on the first frame
// (see atlantis-re functions/graphics-FUN_004212cc.md).  Phase 2 is five more
// frames.  0x00496124 is assumed set (phase 1 runs).
class TransitionTurn {
public:
	struct Frame {
		float alpha, beta;
		bool fovSet;      // FUN_0045179a(fov, 50) before this frame
		float fov;
	};

	// pi is the float FUN_0045179a stores at 0x00bcb594.
	static void run(float pi, float alpha, float beta, float srcAlpha, float srcBeta, bool zoom,
	                Common::Array<Frame> &frames, float &endAlpha, float &endBeta) {
		const float twoPi = (float)((double)pi * 2.0);
		float fov = 75.187f;                         // 0x42965fbe
		float speedX = 0, speedY = 0;                // 0x006ce05c, 0x006ce060
		int keptA = -1, keptB = -1;                  // [EBP-0x18], [EBP-0x14]: never rA * 1000
		byte stallA = 0, stallB = 0;                 // [EBP-0x4], [EBP-0xc]
		for (int phase = 1; phase <= 2; phase++) {
			for (int n = 0; phase == 1 || n < 5; n++) {
				step(pi, twoPi, alpha, beta, srcAlpha, srcBeta, speedX, speedY);
				Frame f;
				f.fovSet = zoom;
				if (zoom)
					fov = (float)((double)fov + (double)-3.0f);
				f.fov = fov;
				f.alpha = alpha;
				f.beta = beta;
				frames.push_back(f);
				if (phase == 2)
					continue;
				float dA = (float)((double)alpha - (double)srcAlpha);
				if (0 > dA)
					dA = -dA;
				float dB = (float)((double)beta - (double)srcBeta);
				if (0 > dB)
					dB = -dB;
				const int rA = (int)dA, rB = (int)dB;    // FUN_00451d6e: FRNDINT with RC = chop
				if (rA * 1000 == keptA)
					stallA++;
				else
					keptA = rA * 100;
				if (rB * 1000 == keptB)
					stallB++;
				else
					keptB = rB * 100;
				if (dA < 0.05 && dB < 0.05)
					break;
				if (stallA == 5 || stallB == 5)
					break;
			}
		}
		endAlpha = alpha;
		endBeta = beta;
	}

private:
	// One step, 0x00421383..0x00421514.
	static void step(float pi, float twoPi, float &alpha, float &beta, float srcAlpha, float srcBeta,
	                 float &speedX, float &speedY) {
		float d = (float)(((double)alpha + (double)twoPi) - (double)srcAlpha);   // 0x006ce03c
		if (!(d < twoPi))
			d = (float)((double)d - (double)twoPi);
		if (0 > d)
			d = (float)((double)d + (double)twoPi);
		float t = (float)((double)d * 600.0);
		if (0 > t)
			t = -t;
		if (d < pi)
			speedX = -t;
		t = (float)(((double)twoPi - (double)d) * 600.0);
		if (0 > t)
			t = -t;
		if (!(d < pi))
			speedX = t;
		if (d == 0)
			speedX = 0;
		const float e = (float)((double)srcBeta - (double)beta);                 // 0x006ce040
		t = (float)(((double)beta - (double)srcBeta) * 600.0);
		if (0 > t)
			t = -t;
		if (0 > e)
			speedY = t;
		t = (float)(((double)beta - (double)srcBeta) * 600.0);
		if (0 > t)
			t = -t;
		if (!(0 > e))
			speedY = -t;
		alpha = (float)((double)speedX / 3000.0 + (double)alpha);
		beta = (float)((double)beta - (double)speedY / 4000.0);
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

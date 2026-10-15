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

#ifndef CRYOMNI3D_ATLANTIS_DIALOG_VIEW_H
#define CRYOMNI3D_ATLANTIS_DIALOG_VIEW_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The view behind a dialog line, from the section executor FUN_0042844c of
// atlantis.exe (atlantis-re functions/scripts-FUN_0042844c.md, "View
// angles"; emulated by tools/emu/video_dialog_line_view.py).
//
// The view angles 0x00bcb59c / 0x00bcb598 ("kept" here) only move on a line
// with explicit header angles.  The turn of a talking-head line is drawn
// into the projection grid alone: the globals are put back right after it
// (0x00428a7a..0x00428a85, 0x00428aec..0x00428af7), and after every line
// the view is set back to them (0x00428b52..0x00428b5e).  So each line
// turns from the same kept angles, never from the previous line's turn.
namespace DialogView {

struct Angles {
	double alpha, beta;   // radians
};

struct Line {
	Angles view;          // the panorama behind the talking head
	Angles kept;          // 0x00bcb59c / 0x00bcb598 during and after the line
	bool entryUpdate;     // the section-end angles become `kept` before the line
};

// What FUN_0042844c carries along one walk, through /go targets too
// (FUN_00425c04 hands the target back to the same loop).
struct Walk {
	Angles entry;         // EBP-0xc / EBP-8, restored at the end of the walk
	int clicPerso;        // ClicPerso at the start of the walk (0x004284c2), restored at its end
};

// The INITDIAL row (0x0077290c) of a line: the IncPerso count for a Seth
// line (0x00428a91); otherwise ClicPerso's low byte (0x00428924, the byte
// store of `local_40`), so a ClicPerso below 0x200 still picks row
// 0x200 + (ClicPerso & 0xff).  FUN_0042ee88 indexes the table with it.
inline int dialRow(bool sethPath, int clicPerso, uint8 incPerso) {
	return 0x200 + (sethPath ? incPerso : (clicPerso & 0xff));
}

inline double clampBeta(double b, double betaMax) {
	// FUN_00451915: beta kept when -max <= beta <= max, else the limit.
	if (b > betaMax)
		return betaMax;
	if (b < -betaMax)
		return -betaMax;
	return b;
}

inline double wrapAlpha(double a) {
	// FUN_00451915 @ 0x00451915: alpha >= 2 pi -> alpha - 2 pi, < 0 -> + 2 pi.
	const double twoPi = 2.0 * M_PI;
	if (a >= twoPi)
		return a - twoPi;
	if (a < 0.0)
		return a + twoPi;
	return a;
}

// kept: the angles before the line.  sethPath: the header starts with "Seth"
// and has no off / off0 / off1 flag (0x00428949..0x0042896c).  clicPerso:
// ClicPerso after the speaker lookup.  hasAngle, x, y: explicit header angles
// (radians); betaMax: FUN_00451915's pitch limit.
inline Line lineView(const Angles &kept, bool sethPath, int clicPerso,
                     bool hasAngle, double x, double y, double betaMax) {
	Line l;
	l.view = kept;
	l.kept = kept;
	l.entryUpdate = false;
	if (sethPath) {
		// 0x004289ef..0x00428a75: entry angles := globals; FOV of the cam;
		// grid at (alpha + pi, 0).
		l.entryUpdate = true;
		l.view.alpha = wrapAlpha(kept.alpha + M_PI);
		l.view.beta = 0.0;
	} else if (clicPerso >= 0x200) {
		// 0x00428a9d..0x00428ae7 (off lines too): entry angles := globals;
		// grid at (alpha, 0).
		l.entryUpdate = true;
		l.view.beta = 0.0;
	}
	if (hasAngle) {
		// 0x00428afc..0x00428b0e: FUN_00451915(X, Y) writes the globals too.
		l.view.alpha = wrapAlpha(x);
		l.view.beta = clampBeta(y, betaMax);
		l.kept = l.view;
	}
	return l;
}

} // End of namespace DialogView

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

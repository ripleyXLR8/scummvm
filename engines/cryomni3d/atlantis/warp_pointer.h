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

#ifndef CRYOMNI3D_ATLANTIS_WARP_POINTER_H
#define CRYOMNI3D_ATLANTIS_WARP_POINTER_H

#include "common/rect.h"

namespace CryOmni3D {
namespace Atlantis {

// The pointer that the warp loop FUN_0041bfa0 hit-tests at its exit
// (FUN_00422bb0 reads 0x004960ec / 0x004960f0) and the previous frame's
// view angles it compares with (0x0049698c / 0x00496990), atlantis.exe
// 714240.  Both live across warp loops:
// - the loop does not set the pointer at entry (previous position =
//   pointer, 0x0041bfa0 entry block);
// - every frame, when alpha or beta differs from the stored angles, the
//   pointer is recentred at (320, 240) (0x0041c2bb..0x0041c301), then the
//   angles are stored (0x0041c357 / 0x0041c363).  They start at -1.0 in the
//   image, so the first loop of a session always recentres;
// - the panorama load FUN_0044a424 leaves it at (0x140, 0x140)
//   (0x0044a70b / 0x0044a715, no SetCursorPos);
// - the WM_MOUSEMOVE handler (0x00419c14) sets it to the mouse position,
//   so after a dialog or a video during which the mouse moved it is the
//   last mouse position.
// The pointer is 0, 0 in the image.
// See atlantis-re: functions/graphics-FUN_0041bfa0.md,
// functions/video-FUN_0044a424.md, experiments/2026-10-15-70.
struct WarpPointer {
	Common::Point pos;
	float prevAlpha, prevBeta;

	WarpPointer() : pos(0, 0), prevAlpha(-1.0f), prevBeta(-1.0f) {}

	// FUN_0044a424 @ 0x0044a70b / 0x0044a715.
	void panoramaLoaded() { pos = Common::Point(0x140, 0x140); }
	// WM_MOUSEMOVE (0x00419c14).
	void mouseMoved(const Common::Point &p) { pos = p; }
	// The bar close FUN_0042c098 and the step-5 recentre.
	void recentre() { pos = Common::Point(320, 240); }

	// One warp frame: the pointer follows the motion (port: relative look,
	// clamped to the screen), then step 5 recentres it when the new angles
	// differ from the stored ones.  The stored angles are those the frame
	// ends with (after the clamp FUN_00451915, @ 0x0041c351..0x0041c363).
	// Returns true when recentred.
	bool frame(int dx, int dy, float newAlpha, float newBeta,
	           float endAlpha, float endBeta) {
		pos.x = (int16)CLIP<int>(pos.x + dx, 0, 639);
		pos.y = (int16)CLIP<int>(pos.y + dy, 0, 479);
		const bool moved = newAlpha != prevAlpha || newBeta != prevBeta;
		if (moved)
			recentre();
		prevAlpha = endAlpha;
		prevBeta = endBeta;
		return moved;
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

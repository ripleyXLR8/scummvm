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

#ifndef CRYOMNI3D_ATLANTIS_WARP_CURSOR_H
#define CRYOMNI3D_ATLANTIS_WARP_CURSOR_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The warp cursor of atlantis.exe FUN_00422fcc, called once per warp-loop
// frame by FUN_0041bfa0.  The animation state lives in globals of the exe;
// all its values are whole numbers, kept here as ints.
struct WarpCursorState {
	uint8 mode;      // 0x004968ac: 0 none, 1/4 click, 2 object, 3 talk, 5 navigate
	int frame;       // 0x004968b0: CURSEURS.SPR sprite of the next frame
	int step;        // 0x004968bc: +1 or -1
	int last;        // 0x004968b4
	uint8 first;     // 0x00496994
	bool clickAnim;  // 0x004968b8: set by the click handler FUN_004278a0

	WarpCursorState() : mode(0), frame(0), step(1), last(0), first(0), clickAnim(false) {}
};

struct WarpCursorInput {
	uint zone;       // FUN_00422bb0: zone id under the cursor
	uint id;         // FUN_00422d5c: object or character id under the cursor
	uint held;       // 0x611210[player]: the held object, 0 for none
	bool hidden;     // 0x0049616c: set while FUN_004212cc turns the view
	bool edgeScroll; // 0x004960c8 == 1 (edge-scroll look mode)
	bool idRule = true; // the 2001 build's ids 0x219 / 0x21a rule (exe_build.h)
};

struct WarpCursorResult {
	int sprite;      // CURSEURS.SPR sprite drawn this frame, -1 for none
	uint zone;       // 0x006cd7f8: the zone a click acts on
	uint id;         // 0x006cd7e8: the id a click acts on
};

// One frame of FUN_00422fcc.
WarpCursorResult warpCursorStep(WarpCursorState &st, const WarpCursorInput &in);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

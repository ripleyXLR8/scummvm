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

#include "cryomni3d/atlantis/warp_cursor.h"

namespace CryOmni3D {
namespace Atlantis {

static void startAnim(WarpCursorState &st, uint8 mode, int frame, int lastAdd, int firstAdd) {
	st.mode = mode;
	st.step = 1;
	st.frame = frame;
	st.last = frame + lastAdd;
	st.first = (uint8)(frame + firstAdd);
}

WarpCursorResult warpCursorStep(WarpCursorState &st, const WarpCursorInput &in) {
	WarpCursorResult res;
	res.sprite = -1;

	// Zone and id the cursor acts on (0x00422fe8..0x00423138).  While an
	// object is held, ordinary zones (below 1000) do not count; zones
	// 2001..2999 and above 3000 take the held object onto the id.
	uint zone = in.zone;
	uint id = in.id;
	if (zone < 1000 && in.held != 0)
		zone = 0;
	if (zone > 2000 && zone < 3000 && zone - 2000 == in.held) {
		id = zone - 2000;
		zone = 0;
	} else if (zone > 2000 && zone < 3000 && id == 0) {
		zone = 0;
	}
	if (zone > 3000) {
		id = (zone - 3000 == in.held) ? zone - 3000 : 0;
		zone = 0;
	}
	res.zone = zone;
	res.id = id;

	// Ids 0x219 and 0x21a show the navigate cursor (0x0042313d).
	if (in.idRule && (id == 0x219 || id == 0x21a)) {
		id = 0;
		zone = 100;
	}

	if (!st.clickAnim && !in.hidden) {
		if (id >= 0x100 && id <= 0x1ff) {
			if (st.mode != 2)
				startAnim(st, 2, 18, 4, 0);
		} else if (id < 0x201 && zone < 0x201) {
			if (zone == 0 || zone > 0x1ff)
				startAnim(st, 0, 0, 4, 0);   // every frame: nothing drawn
			else if (st.mode != 5)
				startAnim(st, 5, 9, 5, 0);
		} else if (st.mode != 3) {
			startAnim(st, 3, 26, 20, 0);
		}

		// Frame 0 is not drawn (0x0042330d).  Objects and navigation go back
		// and forth; the talk cursor loops 26..45.
		if (st.frame != 0) {
			res.sprite = st.frame;
			st.frame += st.step;
			if ((st.frame == st.last || st.frame == (int)st.first) && st.mode != 3)
				st.step = -st.step;
			if (st.frame == st.last && st.mode == 3)
				st.frame = 26;
		}
		// Edge-scroll mode draws sprite 5 when nothing animates (0x00423470).
		if (in.edgeScroll && st.frame == 0)
			res.sprite = 5;
	}

	// The click animation (0x0042353b): mode 4 (no zone) draws sprite 0 for
	// one frame, as its end test compares with (uint8)-3; mode 1 plays
	// 3..7..1.  Navigate (mode 5) continues from where it is.
	if (st.clickAnim) {
		if (zone == 0) {
			if (st.mode != 4 && st.mode != 1)
				startAnim(st, 4, 0, 4, -3);
		} else if (st.mode != 1 && st.mode != 4 && st.mode != 5) {
			startAnim(st, 1, 3, 4, -3);
		}
		res.sprite = st.frame;
		st.frame += st.step;
		if (st.frame == st.last)
			st.step = -1;
		if (st.frame <= (int)st.first) {
			st.clickAnim = false;
			st.mode = 0;
		}
	}

	return res;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

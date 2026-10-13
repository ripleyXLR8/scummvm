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

#ifndef CRYOMNI3D_ATLANTIS_SUBJECT_MENU_H
#define CRYOMNI3D_ATLANTIS_SUBJECT_MENU_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/menu_layout.h"

namespace CryOmni3D {
namespace Atlantis {

// The dialog subject menu of atlantis.exe: FUN_004261a0 draws n subjects
// in columns of four from x = 450 (the column moves right at subjects 4, 8
// and 12 only), and FUN_00425828 hit-tests with FUN_004211f0 from the grid
// origin the draw left in 0x006cd83c / 0x006cd840: the LAST column's x and
// the first column's y.  With five or more subjects the clickable grid is
// therefore one column to the right of the drawn one; its columns are
// n / 5 + 1, its rows min(n, 4), column-major, edges inclusive.  An index
// at or past n reads an empty slot of the record (subject 0).
struct SubjectMenu {
	struct Draw { int x, y, frame; };

	// The draws: border (frame 0) then icon (frame = the subject id).
	static void draws(const Common::Array<int> &ids, Common::Array<Draw> &out,
	                  int &gridX, int &gridY) {
		const int n = (int)ids.size();
		int x0 = 0x1c2;
		int y0 = (n <= 4) ? 180 - (n - 3) * 50 : 180;
		const int firstY = y0;
		for (int i = 0; i < n; i++) {
			if (i == 4 || i == 8 || i == 12) {
				x0 += 50;
				y0 = 180 - 50 * i;
			}
			const int y = (int16)(y0 + 50 * i);
			Draw b = { x0, y, 0 };
			Draw c = { x0 + 25, y + 25, ids[i] };
			out.push_back(b);
			out.push_back(c);
		}
		gridX = x0;
		gridY = firstY;
	}

	// FUN_004211f0 as called at 0x00425a53..0x00425a76: the cell index, -1
	// outside.
	static int hit(int n, int gridX, int gridY, int mx, int my) {
		return menuGridHit(gridX, gridY, 50, 50, n / 5 + 1, MIN(n, 4), true, mx, my);
	}

	// The subject byte FUN_00425828 reads for an index (0x00425a87..):
	// 0 past the n subjects; -1 past the 0xe1-byte record.
	static int subject(const Common::Array<int> &ids, int idx) {
		if (idx < 0)
			return -1;
		if (0xe + idx * 0xe >= 0xe1)
			return -1;
		return idx < (int)ids.size() ? ids[idx] : 0;
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

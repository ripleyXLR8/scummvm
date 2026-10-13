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

#include "common/util.h"

#include "cryomni3d/atlantis/ogre_rules.h"

namespace CryOmni3D {
namespace Atlantis {

// Cell edges at 0x0049743c: 7 column starts and the board's right edge,
// then 7 row starts and a value whose + 60 bounds the last row.
static const int16 kColEdge[8] = { 112, 173, 233, 294, 354, 415, 476, 536 };
static const int16 kRowEdge[8] = { 27, 88, 148, 209, 269, 330, 390, 451 };

static const int kCellSize = 60;
static const int kBoardX = 0x71, kBoardY = 0x1d;
static const int kAbortX0 = 0x32, kAbortX1 = 0x24e;

OgreRules::OgreRules() {
	memset(p, 0, sizeof(p));
	memset(occ, 0, sizeof(occ));
	memset(mask, 0, sizeof(mask));
	for (int i = 0; i < 3; i++) {
		pathX[i] = -1;
		pathY[i] = -1;
	}
}

int OgreRules::cellColumn(int mouseX) {
	int cell = 99;
	for (int i = 0; i < 7; i++) {
		if (kColEdge[i] > mouseX || kColEdge[7] <= mouseX)
			break;
		cell = i;
	}
	return cell;
}

int OgreRules::cellRow(int mouseY) {
	int cell = 99;
	for (int i = 0; i < 7; i++) {
		if (kRowEdge[i] > mouseY || kRowEdge[7] + kCellSize <= mouseY)
			break;
		cell = i;
	}
	return cell;
}

void OgreRules::buildMoveMask(int mouseX, int mouseY) {
	const int cx = cellColumn(mouseX);
	const int cy = cellRow(mouseY);
	if (cx >= 7 || cy >= 7)
		return;
	memset(mask, 0, sizeof(mask));
	const int hx = p[kHero].x, hy = p[kHero].y;
	const int dx = cx - hx, dy = cy - hy;
	const int dist = ABS(dx) + ABS(dy);
	if (dist >= 1 && dist <= 2) {
		int midX, midY;
		if (dx == 0 || dy == 0) {
			midX = hx + dx / 2;
			midY = hy + dy / 2;
		} else {
			// The elbow follows the cursor's offset from the hero's cell
			// centre (0x0044409f..0x0044410e).
			const int ex = mouseX - (kColEdge[hx] + 30);
			const int ey = mouseY - (kRowEdge[hy] + 30);
			midX = hx;
			midY = hy;
			if (ABS(ex) > ABS(ey))
				midX = cx;
			else
				midY = cy;
		}
		mask[midY * 7 + midX] = 1;
		mask[cy * 7 + cx] = 1;
		pathX[1] = midX;
		pathY[1] = midY;
		pathX[2] = cx;
		pathY[2] = cy;
	}
	mask[hy * 7 + hx] = 1;
	pathX[0] = hx;
	pathY[0] = hy;
}

bool OgreRules::pickUp(int mouseX, int mouseY) const {
	const int px = p[kHero].x * kCellSize, py = p[kHero].y * kCellSize;
	return mouseX > px + 0x78 && mouseX < px + 0x9f && mouseY > py + 0x1d && mouseY < py + 0x54;
}

bool OgreRules::abortClick(int mouseX) {
	return mouseX < kAbortX0 || mouseX > kAbortX1;
}

bool OgreRules::drop(int mouseX, int mouseY) {
	// C division truncates toward zero (0x00443c9b..0x00443cc2).
	const int cx = (mouseX - kBoardX) / kCellSize;
	const int cy = (mouseY - kBoardY) / kCellSize;
	if (occAt(cy * 7 + cx))
		return false;
	if (cx >= 7 || cy >= 7 || cx < 0 || cy < 0)
		return false;
	if (mask[cy * 7 + cx] != 1)
		return false;
	p[kHero].x = cx;
	p[kHero].y = cy;
	p[kHero].flag = 1;
	return true;
}

bool OgreRules::queenCaught() const {
	for (int i = 0; i < 3; i++)
		if (p[kQueen].x == pathX[i] && p[kQueen].y == pathY[i])
			return true;
	return false;
}

void OgreRules::ogreStep(int o, int &sx, int &sy) const {
	const Piece &ogre = p[o];
	sx = 1;
	sy = 1;
	if (p[kHero].x < ogre.x)
		sx = -1;
	else if (p[kHero].x == ogre.x)
		sx = 0;
	if (p[kHero].y < ogre.y)
		sy = -1;
	else if (p[kHero].y == ogre.y)
		sy = 0;
	if (ogre.x + sx > 6 || ogre.x + sx < 0)
		sx = 0;
	if (ogre.y + sy > 6 || ogre.y + sy < 0)
		sy = 0;
	if (occAt((ogre.y + sy) * 7 + ogre.x + sx)) {
		int nx = ogre.x, ny = ogre.y;   // see the header: the exe leaves these unset
		const int x0 = MAX(ogre.x - 1, 0), x1 = MIN(ogre.x + 1, 6);
		const int y0 = MAX(ogre.y - 1, 0), y1 = MIN(ogre.y + 1, 6);
		for (int yy = y0; yy <= y1; yy++)
			for (int xx = x0; xx <= x1; xx++)
				if (!occAt(yy * 7 + x0 + xx)) {
					nx = x0;
					ny = yy;
				}
		sx = nx - ogre.x;
		sy = ny - ogre.y;
	}
	if (ogre.x + sx > 6 || ogre.x + sx < 0)
		sx = 0;
	if (ogre.y + sy > 6 || ogre.y + sy < 0)
		sy = 0;
}

void OgreRules::moveOgre(int o, int sx, int sy) {
	Piece &ogre = p[o];
	occ[ogre.y * 7 + ogre.x] = 0;
	ogre.x += sx;
	ogre.y += sy;
	occ[ogre.y * 7 + ogre.x] = 1;
}

int OgreRules::moveOgres() {
	for (int o = kOgreA; o < kNumPieces; o++) {
		if (p[o].flag == 1) {
			int sx, sy;
			ogreStep(o, sx, sy);
			moveOgre(o, sx, sy);
		}
		if (p[o].x == p[kHero].x && p[o].y == p[kHero].y)
			return kResultLose;
	}
	return kResultContinue;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

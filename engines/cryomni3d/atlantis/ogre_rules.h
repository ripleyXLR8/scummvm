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

#ifndef CRYOMNI3D_ATLANTIS_OGRE_RULES_H
#define CRYOMNI3D_ATLANTIS_OGRE_RULES_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The rules of the ogre & hero game (StartPuzzle=2, place 290) as
// atlantis.exe runs them, without drawing: FUN_00443f94 (path and mask),
// FUN_00443c3c (pick-up, drop, catch, abort), FUN_00442360 (ogre turn) and
// FUN_00442a0c (queen walk).
class OgreRules {
public:
	struct Piece { int flag, x, y; };
	enum { kHero = 0, kQueen = 1, kOgreA = 2, kOgreB = 3, kNumPieces = 4 };
	enum { kResultContinue = 0, kResultLose = 2, kResultWin = 255 };

	// Pieces: hero, queen (flag 0: hidden), two ogres (0x495e4c).
	Piece p[kNumPieces];
	// Occupancy (0x7738b8).  The original's scans index past its 49 cells
	// into zero bytes; the padding stands for them.
	byte occ[64];
	byte mask[49];      // legal path cells while holding (0x773938)
	int pathX[3], pathY[3];   // origin, elbow, destination (0x773870)

	OgreRules();

	// FUN_00443ea0 / FUN_00443f18: the cell under a screen point through the
	// edge table at 0x0049743c, or 99 off the board.
	static int cellColumn(int mouseX);
	static int cellRow(int mouseY);
	// The edge table itself (0x0049743c, 0x0049744c): where cell (x, y) is
	// drawn by FUN_004432f8 / FUN_00443944.
	static int colEdge(int x);
	static int rowEdge(int y);

	// FUN_00443f94, run each frame while the hero is held.  Off the board the
	// mask and path are left as they are; on an illegal cell only the origin
	// is rewritten (the elbow and destination keep the last legal hover).
	void buildMoveMask(int mouseX, int mouseY);

	// FUN_00443c3c pick-up rectangle and abort strip.
	bool pickUp(int mouseX, int mouseY) const;
	static bool abortClick(int mouseX);

	// FUN_00443c3c drop: the hero lands if the cell is free, on the board and
	// marked in the mask.  Returns false when the drop is refused.
	bool drop(int mouseX, int mouseY);
	// The catch test after a drop: the queen on one of the three path cells.
	bool queenCaught() const;

	// FUN_00442360 for ogre o: its step (sx, sy).  When the target cell is
	// occupied the original scans the clamped 3x3 box testing
	// occ[yy * 7 + x0 + xx] and takes (x0, yy) for each free hit; with no
	// free hit it uses uninitialised locals, which no state reachable from
	// the start needs (exhaustive search); the ogre then stays.
	void ogreStep(int o, int &sx, int &sy) const;
	// Apply a step: free the old cell, move, take the new cell.
	void moveOgre(int o, int sx, int sy);
	// Ogre turn without animation: ogre A then B, stopping at a catch.
	int moveOgres();

	// FUN_00442a0c: rand32767 returns 0..32767 (the original's rand()).
	template<class Rand>
	void walkQueen(Rand &rand32767) {
		int sx = 1, sy = 1;
		for (int tries = 0; ; tries++) {
			if (rand32767() > 16000) sx = -sx;
			if (rand32767() > 16000) sy = -sy;
			if (p[kQueen].x + sx > 6 || p[kQueen].x + sx < 0) sx = -sx;
			if (p[kQueen].y + sy > 6 || p[kQueen].y + sy < 0) sy = -sy;
			if (tries >= 200) {
				sx = 0;
				sy = 0;
				break;
			}
			const int qx = p[kQueen].x + sx, qy = p[kQueen].y + sy;
			bool onPath = false;
			for (int i = 0; i < 3; i++)
				if (qx == pathX[i] && qy == pathY[i])
					onPath = true;
			if (!onPath)
				break;
		}
		p[kQueen].x += sx;
		p[kQueen].y += sy;
	}

private:
	byte occAt(int idx) const { return (idx >= 0 && idx < (int)sizeof(occ)) ? occ[idx] : 0; }
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

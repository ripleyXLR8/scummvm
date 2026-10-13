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

#ifndef CRYOMNI3D_ATLANTIS_TAQUIN_GAME_H
#define CRYOMNI3D_ATLANTIS_TAQUIN_GAME_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The three 3x3 sliding puzzles (StartPuzzle 6, 7 and 14; FUN_00446300,
// FUN_0044659c, FUN_00446830).  They share their code and differ in the
// cell positions, the pictures and where the frame boundary FUN_0043318c
// sits in the loop.  Tile 7 is missing: with object 322 in hand its cell
// empties, and the board is solved when cells 0..7 read 1 2 3 4 5 6 _ 8.
class TaquinGame {
public:
	explicit TaquinGame(int variant);   // 6, 7 or 14
	virtual ~TaquinGame() {}

	int16 board[9];      // 0x00496f38
	int16 cellX[9], cellY[9];
	int16 movingCell;    // 0x00496fc8, 99 when none
	int16 moveDx, moveDy;   // 0x00496fca, 0x00496fcc
	// The shared puzzle-cursor animation (0x00496ea8 mode, 0x00496eaa
	// frame, 0x00496eac step).
	int16 cursorMode, cursorFrame, cursorStep;
	int32 objectInHand;  // the OBJETENMAIN variable
	int32 result;        // 0x004974c0: 1 solved, 2 left
	bool withPiece;      // 0x007bead0: object 322 was in hand

	// The entry: 255 when solved, 2 when left.  Its picture is the "b"
	// one when withPiece is set.
	int run();

protected:
	// The input when the puzzle starts, and FUN_0043318c: the boundaries
	// where the player's input is read.
	virtual void startInput(int &mx, int &my, bool &click) = 0;
	virtual void boundary(int &mx, int &my, bool &click) = 0;
	virtual void drawBackground() = 0;
	virtual void drawSprite(char bank, int x, int y, int frame) = 0;   // 'P' PUZTAQn, 'C' CURSEURS
	virtual void drawStandardCursor(int mx, int my) = 0;   // FUN_0042d558
	virtual void present() = 0;
	virtual void playSound(int slot) = 0;   // tak1..tak4
	virtual bool aborted() const { return false; }

private:
	int _variant;
	int _mx, _my;
	bool _click;

	void readBoundary();
	void draw(int16 extra);   // FUN_00446cf8
	void frame(int16 extra);  // FUN_00446ac8
	void click();             // FUN_00447048
	void error();             // FUN_0044727c
	void cursor();            // FUN_00446f28, FUN_00445d70
	void winAnimation();      // FUN_00446c54
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

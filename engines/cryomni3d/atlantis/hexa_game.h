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

#ifndef CRYOMNI3D_ATLANTIS_HEXA_GAME_H
#define CRYOMNI3D_ATLANTIS_HEXA_GAME_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/meta_game.h"

namespace CryOmni3D {
namespace Atlantis {

// The hexagon (StartPuzzle 13, FUN_004404fc): three pieces dropped on a
// class map (puzhexam.bmp: 0 off the board, 1 the board, 0x1f the frame),
// turned in sixths; the puzzle is won with the pieces turned 5, 3, 5 and
// laid out inside a box, piece 1 leftmost, piece 0 at the top, piece 2 at
// the bottom.  The same engine as the metal pieces (MetaGame), with its
// own drop test, no board turn and its own sounds.
class HexaGame {
public:
	// A piece record (0x004971ce, 12 bytes).
	struct Piece {
		int16 base, cls, orient, x, y, unkA;
	};
	static const int kNumPieces = 3;
	static const uint32 kMapSize = MetaGame::kMapSize;

	Piece pieces[kNumPieces];
	int32 grabX, grabY, repeat, unkF4, held;   // 0x007bebe8..0x007bebf8
	int16 cursorMode, cursorFrame, cursorStep;
	int32 objectInHand;   // OBJETENMAIN
	int32 flagTemp6;      // FLAGTEMP6
	int32 result;
	Common::Array<byte> map;
	Common::Array<MetaGame::Coverage> coverage;

	HexaGame();
	virtual ~HexaGame() {}

	bool loadCoverage(const byte *spr, uint32 size) { return MetaGame::readCoverage(spr, size, coverage); }

	// FUN_00448ad0 then FUN_004404fc: 255 won, 2 left.
	int run();

	bool placePiece(int i, int x, int y);   // FUN_00440efc
	bool won() const;                       // FUN_00441210

protected:
	virtual void startInput(int &mx, int &my, bool &click, bool &right) = 0;
	virtual void boundary(int &mx, int &my, bool &click, bool &right) = 0;   // FUN_0043318c
	virtual void reloadMap() = 0;           // FUN_004472cc
	virtual void drawBackground() = 0;
	virtual void drawSprite(char bank, int x, int y, int frame) = 0;
	virtual void drawStandardCursor(int mx, int my) = 0;
	virtual void present() = 0;
	virtual void playSound(int slot) = 0;   // hexa1..hexa5
	virtual bool aborted() const { return false; }

private:
	int _mx, _my;
	bool _click, _right;

	void readBoundary();
	void setUp();             // FUN_004406fc
	void rightButton();       // FUN_00440bfc
	void leftClick();         // FUN_00441390
	bool drop(int i, int x, int y);   // FUN_00441184
	void refuse();            // FUN_004414fc
	void draw();              // FUN_00440aa8
	void cursor();            // FUN_00440cb8
	void winAnimation();      // FUN_00440898
	void frame();
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

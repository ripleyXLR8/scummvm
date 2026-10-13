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

#ifndef CRYOMNI3D_ATLANTIS_META_GAME_H
#define CRYOMNI3D_ATLANTIS_META_GAME_H

#include "common/array.h"
#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The metal pieces (StartPuzzle 9..12, FUN_0044763c with variant 1..4):
// the four stages of one board.  Pieces are dropped on an 8-bit class map
// (pzmetam.bmp: 0 the centre, 1..4 the zones, 0x1f the frame, 10..16 the
// pieces' marks); stage v is won with its first v + 3 pieces in zone v.
// The object state lives on between the stages, as in the exe's memory.
class MetaGame {
public:
	// A piece record (0x00496fe0, 14 bytes).
	struct Piece {
		int16 base;    // first PZMETAP1 frame of its 8 orientations
		int16 unk2;
		int16 cls;     // map class it lies on, -1 when absent
		int16 orient;  // 0..7
		int16 x, y;
		int16 unkC;
	};
	static const int kNumPieces = 7;
	static const uint32 kMapSize = 640 * 480;

	Piece pieces[kNumPieces];
	int16 count;          // 0x00496fdc: pieces present
	int16 picture;        // 0x007bec1e: 1..4
	int16 variant;        // 0x007bec1c
	int32 grabX, grabY;   // 0x007bebe8, 0x007bebec
	int32 repeat;         // 0x007bebf0: right-button repeat counter
	int32 unkF4;          // 0x007bebf4
	int32 held;           // 0x007bebf8: piece + 1, 0 when none
	int16 cursorMode, cursorFrame, cursorStep;   // 0x00496ea8..0x00496eac
	int32 objectInHand;   // OBJETENMAIN
	int32 broughtFlag;    // the variable named at 0x004be584
	int32 result;
	Common::Array<byte> map;   // the class map (pzmetam.bmp from +0x436)

	// Per frame of pzmetap1.spr: the covered offsets from the drop point
	// (see the note: non-zero raw pixels, blended pixels with a non-zero
	// colour and factor).
	struct Coverage { Common::Array<int16> dx, dy; };
	Common::Array<Coverage> coverage;

	MetaGame();
	virtual ~MetaGame() {}

	// The coverage of every frame, from the SPR file.
	bool loadCoverage(const byte *spr, uint32 size);

	// FUN_00448ad0 (no state) then FUN_0044763c(v): 255 won, 2 left.  The
	// map must hold the file's pixels.
	int run(int v);

	// Pieces of the rules, public for the tests.
	bool placePiece(int i, int x, int y);   // FUN_00448404
	void turnBoard(bool advance);           // FUN_00448158

protected:
	virtual void startInput(int &mx, int &my, bool &click, bool &right) = 0;
	virtual void boundary(int &mx, int &my, bool &click, bool &right) = 0;   // FUN_0043318c
	virtual void loadPicture(int n) = 0;    // FUN_00447834's picture 1..4
	virtual void reloadMap() = 0;           // FUN_004472cc: the map from the file
	virtual void drawBackground() = 0;
	virtual void drawSprite(char bank, int x, int y, int frame) = 0;   // 'P' pieces, 'C' CURSEURS
	virtual void drawStandardCursor(int mx, int my) = 0;   // FUN_0042d558
	virtual void present() = 0;
	virtual void playSound(int slot) = 0;   // meta1..meta5
	virtual int currentPlace() = 0;         // 0x006cd7c0
	virtual bool aborted() const { return false; }

private:
	int _mx, _my;
	bool _click, _right;

	void readBoundary();
	void setPicture(int n);   // FUN_00447834
	void setUp();             // FUN_004478d4
	void rightButton();       // FUN_00448084
	void leftClick();         // FUN_004487ec
	bool drop(int i, int x, int y);   // FUN_00448688
	bool won() const;         // FUN_00448714
	void refuse();            // FUN_00448a80
	void draw();              // FUN_00447cec
	void cursor();            // FUN_00447e40, FUN_00448958
	void winAnimation();      // FUN_00447ad0
	void frame();             // a pass of the win animation
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

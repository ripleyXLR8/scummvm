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

#ifndef CRYOMNI3D_ATLANTIS_MURIA_GAME_H
#define CRYOMNI3D_ATLANTIS_MURIA_GAME_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// Muria (StartPuzzle 16, FUN_0043ee54): twelve stone pieces of three kinds
// lie below two broken tablets with twelve slots of the same kinds; a
// piece goes only on a slot of its kind, and the puzzle is won when piece
// i sits on slot i for all twelve.  A kind-1 piece placed on its own slot
// sets FLAGTEMP3 to 1021 + slot and runs the chapter's /sel scan.
class MuriaGame {
public:
	// A piece record (0x00497290, 8 bytes): kind, home x, y, slot (-1 free,
	// -2 hidden during the win animation).
	struct Piece {
		int16 kind, x, y, slot;
	};
	// A slot record (0x004972f0, 12 bytes): kind, draw x, y, click x, y,
	// the piece on it (-1 empty).
	struct Slot {
		int16 kind, x, y, cx, cy, piece;
	};
	static const int kNumPieces = 12;
	static const int kNumSlots = 12;

	Piece pieces[kNumPieces];
	Slot slots[kNumSlots];
	int32 hand;          // 0x007bebf8: the piece carried + 1
	int16 cursorMode, cursorFrame, cursorStep;   // 0x00496ea8..
	int32 result;        // 0x004974c0
	bool click;          // 0x0049611c
	int32 flagTemp3;     // the CON variable FLAGTEMP3

	MuriaGame();
	virtual ~MuriaGame() {}

	// FUN_0043ee54: 255 won, 2 left.
	int run();

	int slotAt(int mx, int my) const;   // FUN_0043f750: the slot under the mouse, -1 none
	bool won() const;                   // FUN_0043f6cc

protected:
	virtual void startInput(int &mx, int &my, bool &clicked) = 0;
	virtual void boundary(int &mx, int &my, bool &clicked) = 0;   // FUN_0043318c
	virtual void loadPicture(const char *name) = 0;             // FUN_00447478
	virtual void drawBackground() = 0;                          // FUN_00410f36
	virtual void drawSprite(char bank, int x, int y, int frame) = 0;   // 'P' puzmuria.spr, 'C' CURSEURS
	virtual void present() = 0;                                 // FUN_0041afd0
	virtual void playSound(int slot) = 0;                       // muria1..5 = 0..4
	virtual void scriptHook() = 0;                              // FUN_004277c8 after FLAGTEMP3 is set
	virtual void stopSounds() {}                                // FUN_0043d0e0
	virtual bool aborted() const { return false; }

private:
	int _mx, _my;

	void readBoundary();
	void setUp();          // FUN_0043f08c
	void handleClick();    // FUN_0043f850
	void refuse();         // FUN_0043faa0
	void placed(int n);    // FUN_0043faf0
	void draw();           // FUN_0043f328
	void cursor();         // FUN_0043f4d4
	void pass();
	void winAnimation();   // FUN_0043f110
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

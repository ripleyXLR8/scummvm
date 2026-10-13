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

#ifndef CRYOMNI3D_ATLANTIS_RAT_GAME_H
#define CRYOMNI3D_ATLANTIS_RAT_GAME_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/rat_rules.h"

namespace CryOmni3D {
namespace Atlantis {

// The rat's run (StartPuzzle 4): FUN_00438bf0 and its trajectory functions,
// transcribed with their frame loops.  Input, drawing, sound, rand and the
// 200 Hz timer go through the virtual functions, so the same code runs the
// puzzle and replays the emulated original in the tests.
class RatGame {
public:
	// One trajectory file (formats/puzzle-trj.md) and its position.
	struct Slot {
		Common::Array<byte> b;
		Common::Array<float> x, y;
		int16 pos;
		Slot() : pos(0) {}
	};
	static const int kNumSlots = 30;
	static const char *const kSlotFiles[kNumSlots];

	RatRules rules;
	Slot slots[kNumSlots];
	int32 wheelY;         // 0x00772d94, set by FUN_00435d24

	RatGame();
	virtual ~RatGame() {}

	// Parses a TRJ file into slot k (false on a short file).
	bool loadSlot(int k, const byte *data, uint32 size);

	// FUN_00438bf0 after the loads: returns the result (255 win, 2 lost,
	// 1 left by an edge click, 0xa when the clock ran out and was not
	// turned into 2).
	int run();

protected:
	// FUN_0043318c, the frame boundary: read the player's input.
	virtual void readInput(int &mx, int &my, bool &click, bool &held) = 0;
	// Drawing: the background copies (0 and 1: FUN_00438fd4's two copies of
	// the background; 2: the frame copy before the present), a sprite of
	// PZFPIECE ('P') or PZFRAT ('R'), the present.
	virtual void drawBackground(int which) = 0;
	virtual void drawSprite(char bank, int x, int y, int frame) = 0;
	virtual void present() = 0;
	virtual void playSound(int slot) = 0;
	virtual int randomValue() = 0;   // FUN_00453cd1
	// The 200 Hz timer 0x006ce118, and FUN_00438fd4's wait until it is 5
	// ticks past the given value.
	virtual uint32 ticks() = 0;
	virtual void waitUntil(uint32 t) = 0;
	virtual bool aborted() const { return false; }

private:
	int _mx, _my;
	bool _click, _held;

	bool resultSet() const { return rules.result != 0 || aborted(); }
	void frame();                 // the frame block of every loop
	void updateAndDraw();         // FUN_00438fd4
	void drawRatsLeft();          // FUN_004388d8
	bool stepSlot(int k);         // FUN_0043504c
	bool nearJump() const;        // FUN_00437f6c
	void runSlot(int k);          // a plain trajectory loop
	void firstRat();              // FUN_004382a4
	void nextRat();               // FUN_00438548
	void loseRat();               // FUN_004389c4

	void t0001();   // 0x004352d4
	void t0003();   // 0x00435424
	void t0002();   // 0x00435574
	void t0004();   // 0x004356bc
	void t0005();   // 0x00435a88
	void t0009();   // 0x00435bd8
	void wheelHold();   // 0x00435d24
	void wheel();   // 0x00435f54
	void t0006();   // 0x0043610c
	void t0007();   // 0x00436240
	void t0007a();  // 0x004363c0
	void t0007b();  // 0x004364f4
	void t0008();   // 0x0043663c
	void ride();    // 0x00436770
	void t0013();   // 0x0043696c
	void t0014();   // 0x00436ab0
	void t0015();   // 0x00436be4
	void t0010();   // 0x00436d24
	void t0011();   // 0x00436e58
	void t0011a();  // 0x00436fc8
	void t0011b();  // 0x00437108
	void t0012();   // 0x00437244
	void t0020();   // 0x00437384
	void t00025();  // 0x00437608
	void t0021();   // 0x00437898
	void t0023();   // 0x00437b38
	void t0025();   // 0x00437e3c
	void t0022();   // 0x00438010
	void t0099();   // the jump, shared by 0x00437608, 0x00437b38, 0x00438010
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

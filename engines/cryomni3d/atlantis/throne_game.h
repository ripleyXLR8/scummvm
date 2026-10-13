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

#ifndef CRYOMNI3D_ATLANTIS_THRONE_GAME_H
#define CRYOMNI3D_ATLANTIS_THRONE_GAME_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The throne lock (StartPuzzle 5, FUN_0043fcb4): four rings of 8 positions
// turned by four buttons, and a lever that opens when all rings are at 0.
// Input, drawing, sound and the timer go through the virtual functions.
class ThroneGame {
public:
	// The 9 parts at 0x00497216, 12 bytes each: rings 0..3, buttons 4..7,
	// lever 8.
	struct Part {
		int16 base;    // first PUZTRONE frame
		int16 count;   // frames (ring positions)
		int16 init;    // position at the start
		int16 cur;     // current position
		int16 x, y;
	};
	static const int kNumParts = 9;
	Part parts[kNumParts];
	// The shared puzzle-cursor animation (0x00496ea8 mode, 0x00496eaa frame,
	// 0x00496eac step).
	int16 cursorMode, cursorFrame, cursorStep;
	int32 counter;   // 0x007bebf0
	int32 active;    // 0x007bebf8: the part being animated, 0 when none
	int32 result;    // 0x004974c0: 1 opened, 2 left

	ThroneGame();
	virtual ~ThroneGame() {}

	// FUN_0043fcb4 after the loads: 255 when opened, 2 when left.
	int run();

	// The passes' pieces, public for the tests.
	void update();             // FUN_004403b4
	void click(int mx, int my, int &sound);   // FUN_00440270
	bool solved() const;       // FUN_004401f4

protected:
	virtual void readInput(int &mx, int &my, bool &click) = 0;
	virtual void drawBackground() = 0;
	virtual void drawSprite(char bank, int x, int y, int frame) = 0;   // 'P' PUZTRONE, 'C' CURSEURS
	virtual void present() = 0;
	virtual void playSound(int slot) = 0;   // trone1..trone4
	virtual void waitTicks(uint32 n) = 0;   // of the 200 Hz timer
	virtual bool aborted() const { return false; }

private:
	void errorCursor(int &sound);   // FUN_004404ac
	void drawCursor(int mx, int my);   // FUN_004400ec, FUN_00448958
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

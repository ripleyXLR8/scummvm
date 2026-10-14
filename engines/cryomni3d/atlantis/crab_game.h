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

#ifndef CRYOMNI3D_ATLANTIS_CRAB_GAME_H
#define CRYOMNI3D_ATLANTIS_CRAB_GAME_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The crab race (StartPuzzle 15, FUN_0043d28c): the player picks one of
// four crabs and puts it down on the start line; the two crabs then run a
// 1280-pixel track on their own (one pixel a pass, random turns from the
// puzzle LCG) over three scrolling pictures, and the first past the finish
// line wins the round.  Rounds go on to three wins (255) or three losses
// (2); leaving the choice screen counts as three losses (1).
class CrabGame {
public:
	// A crab record (0x0049739a, 16 bytes).  Only kind, x, y, dir, interval
	// and reload are reset by a round: phase and timer carry over, from the
	// previous round and from the previous run (static data).
	struct Crab {
		int16 kind, x, y, dir, phase, timer, interval, reload;
	};
	static const int kNumCrabs = 2;

	Crab crabs[kNumCrabs];
	int32 state;         // 0x007bead0: 0 the choice, 1 the introductions, 2..4 the panel shown
	int32 hand;          // 0x007bebf8: the box chosen + 1 until the crab is put down
	int32 lastComment;   // 0x007beaac
	int16 cursorMode, cursorFrame, cursorStep;   // 0x00496ea8..
	int32 result;        // 0x004974c0
	uint16 lcgSeed;      // 0x007bec20, seeded by the dispatcher
	bool click;          // 0x0049611c

	CrabGame();
	virtual ~CrabGame() {}

	// FUN_0043d28c: 255 three wins, 2 three losses, 1 left.
	int run();

	int lcg(int n);   // FUN_00447524: 0..n

protected:
	virtual void startInput(int &mx, int &my, bool &clicked) = 0;
	virtual void boundary(int &mx, int &my, bool &clicked) = 0;   // FUN_0043318c
	virtual void loadPicture(const char *name) = 0;             // FUN_00447478
	virtual void drawBackground() = 0;                          // FUN_00410f36
	// 'A' crabe0.spr (the portraits, never seen: see drawFrame), 'B'
	// crabe.spr, 'C' CURSEURS.
	virtual void drawSprite(char bank, int x, int y, int frame) = 0;
	virtual void present() = 0;                                 // FUN_0041afd0
	virtual void playSound(int slot) = 0;                       // 0 crabe1, 1 crabe2
	virtual void playComment(int n) = 0;                        // FUN_0043cf70, n >= 1
	virtual bool commentBusy() = 0;                             // the comment sample's status is not 2 or 8
	virtual void stopSounds() {}                                // FUN_0043d0e0
	virtual int rand32767() = 0;                                // FUN_00453cd1
	virtual bool aborted() const { return false; }

private:
	int _mx, _my;

	void readBoundary();
	void setUp();                        // FUN_0043d494
	void handleClick();                  // FUN_0043ebf8
	void refuse();                       // FUN_0043ed98
	void raceSetUp();                    // FUN_0043d500
	void panelPicture();                 // FUN_0043d614
	void raceTick();                     // FUN_0043dbec
	void moveCrab(int i);                // FUN_0043dd38
	int frameOf(int i, bool advance);    // FUN_0043dad0
	int screenX(int x) const;            // FUN_0043e2ec
	void drawFrame();                    // FUN_0043e350
	void drawCursor();                   // FUN_0043e5a4
	void pass();
	void finishAndStart();               // FUN_0043e7d0
	void raceComment();                  // FUN_0043d91c
	int chooseComment() const;           // FUN_0043d994
	void comment(int n, bool force);     // FUN_0043ede8
	void scoreComment(int won, int lost);   // FUN_0043d6b0
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

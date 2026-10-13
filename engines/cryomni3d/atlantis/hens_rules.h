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

#ifndef CRYOMNI3D_ATLANTIS_HENS_RULES_H
#define CRYOMNI3D_ATLANTIS_HENS_RULES_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The rules of puzzle 1 (StartPuzzle=1, CHAPI034, atlantis.exe FUN_004441e8):
// hens put on a network of levers.  Two hens on the ends a and b of a lever
// balance when their weights add up to the lever's sum; a weighing passes
// the tilt of hen 9 along the balanced levers, and wins when it reaches hen
// 1 against the button's direction.  Four won weighings solve the puzzle.
//
// The exe reads every short of its tables as the high half of the dword two
// bytes before it, so the target t is at (0x00496d92 + 4t, 0x00496d94 + 4t)
// and the buttons at (0x00496dd2, 0x00496dd4) and (0x00496dd6, 0x00496dd8).
class HensRules {
public:
	enum { kNumHens = 10, kNumTargets = 16, kNumMarks = 4, kMoving = 0x80 };
	enum Sound { kSoundNone, kSoundPick, kSoundHome, kSoundDrop,
	             kSoundEven, kSoundWin, kSoundLose };

	struct Hen {
		int16 target;   // 0 at home, 1..15 on a target, >= 0x80 falling home
		int16 weight;
		int16 homeX, homeY;
		int16 tilt;     // -1, 0, 1
	};
	struct Mark {       // 0x00496e90: the won weighings, drawn as puzpoul0 frame 4 + i
		int16 shown, x, y;
	};

	Hen hens[kNumHens];   // 0x00496e2c, 10 bytes each
	Mark marks[kNumMarks];
	int16 carried;        // 0x007bebf8: the hen held, 0 for none
	int16 won;            // 0x00496d18
	int16 marked;         // 0x00496d1a: the loop ends at 4
	bool leave;           // 0x004974c0 = 1

	HensRules() : animator(nullptr) { reset(); }

	// FUN_004441e8's set-up, with the records of the image.
	void reset() {
		static const Hen kHens[kNumHens] = {
			{ 0, 0, 0, 0, 0 }, { 1, 41, 0, 0, 0 }, { 0, 20, 163, 447, 0 },
			{ 0, 24, 216, 456, 0 }, { 0, 38, 119, 428, 0 }, { 0, 12, 369, 460, 0 },
			{ 0, 36, 478, 454, 0 }, { 0, 22, 414, 453, 0 }, { 0, 11, 340, 463, 0 },
			{ 9, 33, 0, 0, 1 }
		};
		for (int n = 0; n < kNumHens; n++)
			hens[n] = kHens[n];
		for (int i = 0; i < kNumMarks; i++) {
			marks[i].shown = 0;
			marks[i].x = 307;
			marks[i].y = 399;
		}
		carried = 0;
		won = 0;
		marked = 0;
		leave = false;
	}

	static void targetPos(int t, int &x, int &y) {
		static const int16 kTargets[kNumTargets][2] = {
			{ 0, 0 }, { 298, 150 }, { 260, 198 }, { 245, 239 }, { 307, 254 },
			{ 359, 258 }, { 392, 222 }, { 434, 263 }, { 430, 295 }, { 393, 320 },
			{ 283, 199 }, { 336, 203 }, { 346, 175 }, { 364, 164 }, { 400, 276 },
			{ 341, 300 }
		};
		x = kTargets[t][0];
		y = kTargets[t][1];
	}

	// 0x00496dda: (a, b, sum), ended by a >= 99.
	struct Lever { byte a, b, sum; };
	enum { kNumLevers = 26 };
	static const Lever *levers() {
		static const Lever kLevers[kNumLevers] = {
			{ 8, 7, 33 }, { 8, 9, 44 }, { 14, 5, 45 }, { 14, 7, 35 }, { 14, 9, 44 },
			{ 15, 4, 57 }, { 15, 5, 46 }, { 15, 9, 55 }, { 7, 6, 58 }, { 6, 5, 48 },
			{ 6, 11, 58 }, { 5, 4, 50 }, { 5, 9, 69 }, { 5, 11, 58 }, { 4, 3, 62 },
			{ 4, 10, 60 }, { 4, 11, 58 }, { 11, 1, 65 }, { 11, 12, 30 }, { 2, 1, 61 },
			{ 2, 3, 44 }, { 2, 4, 74 }, { 10, 1, 52 }, { 10, 2, 24 }, { 12, 1, 53 },
			{ 12, 13, 20 }
		};
		return kLevers;
	}

	// The top corners end the puzzle (result 2).
	static bool inCorner(int x, int y) {
		return y >= 0 && y < 0xdc && ((x >= 0 && x < 0xa0) || (x > 0x21c && x < 0x280));
	}

	// FUN_004457bc: can hen n go on target t?  Not if another hen is there,
	// nor if a lever from t would be overloaded: the lever's sum less the
	// hens already at its other end must leave room for n's weight.
	bool canDrop(int n, int t) const {
		for (int m = 1; m < kNumHens; m++)
			if (m != n && hens[m].target == t)
				return false;
		const Lever *lv = levers();
		for (int k = 0; k < kNumLevers; k++) {
			if (t != lv[k].a && t != lv[k].b)
				continue;
			const int other = (t == lv[k].a) ? lv[k].b : lv[k].a;
			int room = lv[k].sum;
			for (int m = 1; m < kNumHens; m++)
				if (m != n && hens[m].target == other)
					room -= hens[m].weight;
			if (room < hens[n].weight)
				return false;
		}
		return true;
	}

	// FUN_00445614: hen m, on a balanced lever with hen n and not yet tilted,
	// tilts the other way; recursively, in the exe's order.
	void propagate(int n) {
		const Lever *lv = levers();
		for (int m = 1; m < 9; m++) {
			if (m == n || hens[m].target == 0)
				continue;
			const int sum = hens[n].weight + hens[m].weight;
			for (int k = 0; k < kNumLevers; k++) {
				if (lv[k].a == hens[n].target && lv[k].b == hens[m].target &&
				        lv[k].sum == sum && hens[m].tilt == 0) {
					hens[m].tilt = -hens[n].tilt;
					propagate(m);
				}
				if (lv[k].a == hens[m].target && lv[k].b == hens[n].target &&
				        lv[k].sum == sum && hens[m].tilt == 0) {
					hens[m].tilt = -hens[n].tilt;
					propagate(m);
				}
			}
		}
	}

	// Receives the frames of the weighing animations: FUN_00444460(a, b, c)
	// with the records as the exe has them at that frame.
	class Animator {
	public:
		virtual ~Animator() {}
		virtual void frame(int a, int b, int c) = 0;
	};
	Animator *animator;

	// FUN_00444d80: dir is 1 for the first button, -1 for the second.  a is
	// the tilt of the balance, reversed at +-15.
	Sound weigh(int dir) {
		for (int n = 1; n < 9; n++)
			hens[n].tilt = 0;
		propagate(9);
		int a = 0;
		if (hens[1].tilt == 0) {
			// poule4: the balance swings once, nothing happens.
			do {
				a += dir;
				draw(a, 0, 0);
				if (ABS(a) == 15)
					dir = -dir;
			} while (a != 0);
			return kSoundEven;
		}
		if (hens[1].tilt * dir > 0) {
			// Sample 0x007739e8, which only puzzle 4 loads (flip7.wav): the
			// hens fall home.
			for (int c = 0x12, b = 0x6d, v = 0x88; c < 0x2f; c++, b++) {
				a += dir;
				if (c > 0x23) {
					for (int n = 2; n < 9; n++)
						hens[n].target = v;
					v += 4;
				}
				draw(a, b, c);
				if (ABS(a) == 15)
					dir = -dir;
			}
			for (int n = 2; n < 9; n++)
				hens[n].target = 0;
			return kSoundLose;
		}
		// poule5: won.
		a = -8;
		for (int i = 1; i < 0x16; i++) {
			a += dir;
			draw(a, i, 0);
			if (ABS(a) == 15)
				dir = -dir;
		}
		won++;
		for (int i = 1; i < 0x12; i++)
			draw(0, 0, i);
		Mark &m = marks[marked];
		m.shown++;
		for (int y = 0x18d; y < 0x1a0; y += 5) {
			m.y = y;
			draw(0, 0, 0);
		}
		for (int i = 1; i < 0x40; i++) {
			if (i > 0x19) {
				m.shown = (i / 4) % 2;
				m.x = marked * 20 + 0x106;
				m.y = 0x1c9;
			}
			draw(0, 0, 0);
		}
		m.shown = 1;
		marked++;
		return kSoundWin;
	}

	// FUN_00444a4c.  Returns the exe's value (the hen picked, the target + 16
	// of a drop, else 0); the sounds played are put in snd[0..1] (a weighing
	// after a pick-up cannot happen, but both buttons are tested).
	int click(int x, int y, Sound snd[2]) {
		snd[0] = snd[1] = kSoundNone;
		if (inCorner(x, y))
			leave = true;
		if (carried == 0) {
			for (int n = 2; n < 9; n++) {
				const int r = hens[n].weight;
				int dx, dy;
				if (hens[n].target == 0) {
					dx = ABS(x - hens[n].homeX);
					dy = ABS(y - hens[n].homeY) * 2;
				} else {
					int tx, ty;
					targetPos(hens[n].target, tx, ty);
					dx = ABS(x - tx);
					dy = ABS(y - ty);
				}
				if (dx < r && dy < r) {
					snd[0] = kSoundPick;
					carried = n;
					return n;
				}
			}
			int s = 0;
			if (ABS(x - 356) < 12 && ABS(y - 360) < 12)
				snd[s++] = weigh(1);
			if (ABS(x - 420) < 12 && ABS(y - 360) < 12)
				snd[s++] = weigh(-1);
			return 0;
		}
		for (int t = 1; t < kNumTargets; t++) {
			int tx, ty;
			targetPos(t, tx, ty);
			if (ABS(x - tx) < 12 && ABS(y - ty) < 12 && canDrop(carried, t)) {
				snd[0] = kSoundDrop;
				hens[carried].target = t;
				carried = 0;
				return t + 16;
			}
		}
		snd[0] = kSoundHome;
		hens[carried].target = 0;
		carried = 0;
		return 0;
	}

	bool solved() const { return marked >= 4; }

private:
	void draw(int a, int b, int c) {
		if (animator)
			animator->frame(a, b, c);
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

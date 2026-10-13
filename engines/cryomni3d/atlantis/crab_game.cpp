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

#include "cryomni3d/atlantis/crab_game.h"

namespace CryOmni3D {
namespace Atlantis {

// The choice boxes (0x004973ba): x, y, and the crab's turn interval A and
// timer reload B.  A box is 200 x 140 from (x, y).
struct CrabBox {
	int16 x, y, a, b;
};
static const CrabBox kBoxes[4] = {
	{ 59, 63, 140, 100 }, { 300, 50, 190, 100 }, { 309, 237, 50, 50 }, { 161, 205, 80, 50 }
};
// The jump points (0x004973fa): direction, x, y.
struct CrabPoint {
	int16 dir, x, y;
};
static const CrabPoint kPoints[6] = {
	{ 0x10, 254, 346 }, { 0x10, 299, 150 }, { 0x10, 640, 215 },
	{ 0x11, 640, 274 }, { 0x11, 932, 182 }, { 0x11, 958, 327 }
};
// The score comments (0x004973da) by wins, then losses.
static const int16 kScoreComments[4][4] = {
	{ 0, 13, 17, 0 }, { 21, 14, 18, 0 }, { 22, 15, 19, 0 }, { 23, 16, 20, 0 }
};
static const char *const kPanels[3] = { "crbfond1.tga", "crbfond2.tga", "crbfond3.tga" };

CrabGame::CrabGame()
	: state(0), hand(0), lastComment(-1), cursorMode(0), cursorFrame(0), cursorStep(0), result(0),
	  lcgSeed(0), click(false), _mx(0), _my(0) {
	// The static records: kinds 1 and 2, all else 0.
	memset(crabs, 0, sizeof(crabs));
	crabs[0].kind = 1;
	crabs[1].kind = 2;
}

int CrabGame::lcg(int n) {
	int mask = 0;
	for (int v = (int16)n; v > 0; v = (int16)v >> 1)
		mask = mask * 2 + 1;
	int v;
	do {
		lcgSeed = (uint16)((int32)(int16)lcgSeed * -0x342f + 1);
		v = ((int)(int16)lcgSeed / 256) & mask;
	} while ((int16)v > n);
	return v;
}

void CrabGame::readBoundary() {
	bool c;
	boundary(_mx, _my, c);
	click = c;
}

void CrabGame::setUp() {
	loadPicture("crbpani.tga");
	hand = 0;
}

void CrabGame::refuse() {
	if (cursorFrame == 2)
		return;
	playSound(1);
	cursorFrame = 2;
	cursorMode = 2;
	cursorStep = 1;
}

void CrabGame::handleClick() {
	if (state != 0) {
		if (hand > 0) {
			// The start: a triangle with its apex at (88, 229), widening
			// leftwards down to y 303.
			const int dx = 88 - _mx, dy = _my - 229;
			if (dx >= 0 && dy >= 0 && dy < 75 && 3 * dx > dy) {
				crabs[0].x = (int16)_mx;
				crabs[0].y = (int16)_my;
				hand = 0;
				playSound(0);
			} else {
				refuse();
			}
		} else if (_my < 40) {
			result = 2;
		}
		return;
	}
	if (hand != 0)
		return;
	// Of two boxes under the mouse the later one wins.
	for (int i = 0; i < 4; i++)
		if (_mx > kBoxes[i].x && kBoxes[i].x + 200 > _mx && _my > kBoxes[i].y && kBoxes[i].y + 140 > _my) {
			hand = i + 1;
			playSound(0);
		}
	if (_my < 40)
		result = 1;
	else if (hand == 0)
		refuse();
}

void CrabGame::panelPicture() {
	if (state >= 0 && state <= 4)
		loadPicture(kPanels[state < 3 ? 0 : state - 2]);
}

void CrabGame::raceSetUp() {
	panelPicture();
	int k;
	do {
		k = lcg(3);
	} while (k == hand - 1);
	crabs[0].kind = (int16)(hand - 1);
	crabs[1].kind = (int16)k;
	crabs[0].x = crabs[1].x = 93;
	crabs[0].y = crabs[1].y = 206;
	crabs[0].dir = crabs[1].dir = 3;
	crabs[0].interval = kBoxes[hand - 1].a;
	crabs[1].interval = kBoxes[k].a;
	crabs[0].reload = kBoxes[hand - 1].b;
	crabs[1].reload = kBoxes[k].b;
	// crabe.spr replaces crabe0.spr from here on (bank 'B').
}

void CrabGame::moveCrab(int i) {
	Crab &c = crabs[i];
	const int16 t = c.timer;
	c.timer = (int16)(t + 1);
	const int r = lcg(255);
	int dir = c.dir;
	int x = c.x, y = c.y;
	const int wheel = (c.kind == 1) ? 6 : 5;
	bool turned = false;
	if (c.interval < t && r < 0x14) {
		bool done = false;
		if ((dir == 2 || dir == 4) && (r & 3) != 0) {
			dir = 3;
			done = true;
		}
		if (!done)
			dir += (r & 1) ? 1 : -1;
		if (dir == 0)
			dir = wheel;
		if (dir > wheel)
			dir = 1;
		c.dir = (int16)dir;
		c.timer = 0;
		turned = true;
	}
	int point = 0;
	switch (dir) {
	case 1:
		y--;
		break;
	case 2:
		y--;
		x++;
		break;
	case 3:
		x++;
		break;
	case 4:
		y++;
		x++;
		break;
	case 5:
		y++;
		break;
	case 6:
		x--;
		break;
	case 0x10:
	case 0x11:
		if (c.phase < 7) {
			x += (dir == 0x10) ? 0x12 : -0x12;
		} else {
			// The draws only advance the LCG: the direction is then set.
			do {
				c.dir = (int16)lcg(6);
			} while (c.dir == 0);
			c.dir = (dir == 0x10) ? 3 : 6;
		}
		break;
	default:
		break;
	}
	bool ok = (y > 0x73 && y < 0x184);
	if (x < 0xb1 - (y - 0x6e) / 4 && (dir < 2 || dir > 4))
		ok = false;
	if (x > 0x43c && y > 0x183 - (x - 0x43c) / 3 && dir > 2 && dir < 6)
		ok = false;
	if (y < 0xc5 && x > 0x3ff && y < x - 0x38a && dir < 4)
		ok = false;
	if (dir < 0x10)
		for (int k = 0; k < 6; k++)
			if (ABS(kPoints[k].y - y) * 2 + ABS(kPoints[k].x - x) < 0x28) {
				x = kPoints[k].x;
				y = kPoints[k].y;
				point = kPoints[k].dir;
			}
	const Crab &o = crabs[i != 0 ? 0 : 1];
	if (ABS(o.y - y) * 2 + ABS(o.x - x) < 0x32 && hand == 0)
		ok = false;
	if (ok) {
		c.x = (int16)x;
		c.y = (int16)y;
		if (point != 0) {
			c.dir = (int16)point;
			c.phase = 7;
			c.timer = 0;
		}
	} else {
		if (turned)
			c.timer = c.reload;
		if (c.phase == 0)
			c.phase = 7;
		else
			c.phase--;
	}
}

void CrabGame::raceTick() {
	if (hand == 0)
		moveCrab(0);
	moveCrab(1);
	int n[5] = { 0, 0, 0, 0, 0 };
	for (int i = 0; i < kNumCrabs; i++) {
		const int x = crabs[i].x;
		if (x < 0x262)
			n[2]++;
		if (x > 0x15e && x < 0x3a2)
			n[3]++;
		if (x > 0x29e)
			n[4]++;
	}
	if (n[state] < 2) {
		int k = state;
		for (int j = 2; j < 5; j++)
			if (n[j] == 2)
				k = j;
		if (k == state) {
			if (n[3] != 0)
				k = 3;
			if (n[4] != 0)
				k = 4;
		}
		if (k != state) {
			state = k;
			panelPicture();
		}
	}
}

int CrabGame::frameOf(int i, bool advance) {
	Crab &c = crabs[i];
	int phase = c.phase;
	if (advance) {
		phase++;
		if (phase > 7)
			phase = 0;
		c.phase = (int16)phase;
	}
	int f = c.kind * 0x38 + 1 + phase;
	switch (c.dir) {
	case 1:
	case 5:
		f += 0x10;
		break;
	case 2:
		f += 0x28;
		break;
	case 3:
		f += 0x18;
		break;
	case 4:
		f += 0x20;
		break;
	case 6:
		f += 0x30;
		break;
	case 0x10:
		f += 8;
		break;
	default:
		break;
	}
	return (int16)f;
}

int CrabGame::screenX(int x) const {
	if (state == 3)
		return x - 0x140;
	if (state == 4)
		return x - 0x280;
	return x;
}

void CrabGame::drawFrame() {
	drawBackground();
	if (state != 0) {
		const int first = (state > 1 && crabs[0].y > crabs[1].y) ? 1 : 0;
		const int second = (first != 0 && hand == 0) ? 0 : 1;
		if (state > 1 && hand == 0) {
			const int f = frameOf(first, true);
			drawSprite('B', screenX(crabs[first].x), crabs[first].y, f);
		}
		const int f = frameOf(second, true);
		drawSprite('B', screenX(crabs[second].x), crabs[second].y, f);
		if (state == 4)
			drawSprite('B', 0x1d5, 0x9a, 0);
		return;
	}
	// The portraits are drawn on the locked surface itself, which the pass
	// then overwrites with the work buffer: they are never seen.
	for (int i = 0; i < 4; i++)
		if (hand - 1 != i)
			drawSprite('A', kBoxes[i].x, kBoxes[i].y, i);
}

void CrabGame::drawCursor() {
	if (hand != 0 && state != 0)
		drawSprite('B', _mx, _my, frameOf(0, true));
	if (cursorMode >= 2) {
		// FUN_00448958, the shared cursor animation.
		drawSprite('C', _mx, _my, cursorFrame);
		cursorFrame += cursorStep;
		if (cursorFrame == 4)
			cursorStep = -1;
		if (cursorFrame <= -1) {
			cursorMode++;
			cursorStep = 1;
		}
		if (cursorMode == 5)
			cursorMode = 0;
		return;
	}
	const int f = (_my < 40) ? 10 : 5;
	if (!(state >= 2 && f == 5))
		drawSprite('C', _mx, _my, f);
}

void CrabGame::pass() {
	drawFrame();
	drawCursor();
	drawBackground();
	present();
}

void CrabGame::finishAndStart() {
	if (state != 0) {
		int x = crabs[0].x, y = crabs[0].y;
		if (x > 0x49b && (x - 0x49b) * 3 + 0xc5 > y)
			result = 255;
		x = crabs[1].x;
		y = crabs[1].y;
		if (x > 0x49b && (x - 0x49b) * 3 + 0xc5 > y)
			result = 2;
		return;
	}
	if (hand == 0)
		return;
	raceSetUp();
	state++;
	// The two introductions, each played to its end with passes drawn
	// meanwhile; the clicks of those passes are dropped.
	for (int n = 0; n < 3 && !aborted(); n++) {
		bool busy = commentBusy();
		while (busy && !aborted()) {
			pass();
			if (n == 0)
				click = false;
			readBoundary();
			busy = commentBusy();
		}
		if (n == 0)
			comment(0x19, true);
		else if (n == 1) {
			click = false;
			comment(0x1a, true);
		} else {
			click = false;
		}
	}
	state++;
}

int CrabGame::chooseComment() const {
	const int x0 = crabs[0].x, x1 = crabs[1].x, d = x0 - x1, dir1 = crabs[1].dir;
	if (crabs[0].dir == 0x11 || dir1 == 0x10)
		return 8;
	if (dir1 == 6)
		return 5;
	if (dir1 == 3)
		return 7;
	if (dir1 == 2 || dir1 == 4)
		return 2;
	if (x1 > 0x44c && x1 > x0)
		return 12;
	if (x1 < 0x3e8 && x1 > x0)
		return 11;
	if (d > 0x1f4)
		return 4;
	if (d > 0x12c)
		return 9;
	if (d > 0xc8)
		return 3;
	if (d < 0xc8)
		return 6;
	return 1;
}

void CrabGame::comment(int n, bool force) {
	if (force) {
		if (n >= 1)
			playComment(n);
		return;
	}
	const bool busy = commentBusy();
	const int r = rand32767();
	if (r < 0x3e80 && !busy && n >= 1)
		playComment(n);
}

void CrabGame::raceComment() {
	if (state <= 2 || result != 0)
		return;
	if (commentBusy())
		return;
	const int c = chooseComment();
	if (c == lastComment)
		return;
	comment(c, false);
	lastComment = c;
}

void CrabGame::scoreComment(int won, int lost) {
	// Two identical waits for the comment sample, with the status read
	// before the boundary of each pass.
	for (int n = 0; n < 2; n++) {
		bool busy = commentBusy();
		while (busy && !aborted()) {
			pass();
			click = false;
			busy = commentBusy();
			readBoundary();
		}
	}
	if (won >= 0 && won < 4 && lost >= 0 && lost < 4)
		comment(kScoreComments[won][lost], true);
}

int CrabGame::run() {
	bool c;
	startInput(_mx, _my, c);
	click = c;
	int won = 0, lost = 0;
	lastComment = -1;
	do {
		setUp();
		result = 0;
		state = 0;
		do {
			if (click)
				handleClick();
			if (state > 1)
				raceTick();
			pass();
			click = false;
			finishAndStart();
			raceComment();
			readBoundary();
		} while (result == 0 && !aborted());
		if (aborted())
			return 1;
		if (result == 255)
			won++;
		else
			lost++;
		if (result == 1)
			lost = 3;
		else
			scoreComment(won, lost);
	} while (won < 3 && lost < 3);
	stopSounds();
	if (won == 3)
		result = 255;
	else if (lost == 3 && result != 1)
		result = 2;
	stopSounds();
	return result;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

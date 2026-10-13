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

#include "cryomni3d/atlantis/taquin_game.h"

namespace CryOmni3D {
namespace Atlantis {

// The start board of all three (0x00496f14, 0x00496f26, 0x00497188) and
// the target 0x00496f4a (cell 8 is not compared).
static const int16 kStart[9] = { 8, 1, 4, 6, 2, 3, 7, 9, 5 };
static const int16 kTarget[9] = { 1, 2, 3, 4, 5, 6, 0, 8, 9 };
// The cell positions (0x00496f80/92, 0x00496fa4/b6, 0x0049719a/ac).
static const int16 kX6[9] = { 241, 301, 360, 241, 301, 360, 241, 301, 360 };
static const int16 kY6[9] = { 152, 152, 152, 211, 211, 211, 271, 271, 271 };
static const int16 kX7[9] = { 220, 296, 372, 220, 296, 372, 220, 296, 372 };
static const int16 kY7[9] = { 106, 106, 106, 182, 182, 182, 259, 259, 259 };
static const int16 kX14[9] = { 247, 302, 357, 247, 302, 357, 247, 302, 357 };
static const int16 kY14[9] = { 118, 119, 119, 173, 173, 173, 228, 229, 229 };

TaquinGame::TaquinGame(int variant)
	: movingCell(99), moveDx(0), moveDy(0), cursorMode(0), cursorFrame(0), cursorStep(0),
	  objectInHand(0), result(0), withPiece(false), _variant(variant), _mx(0), _my(0), _click(false) {
	const int16 *xs = variant == 7 ? kX7 : variant == 14 ? kX14 : kX6;
	const int16 *ys = variant == 7 ? kY7 : variant == 14 ? kY14 : kY6;
	for (int i = 0; i < 9; i++) {
		board[i] = kStart[i];
		cellX[i] = xs[i];
		cellY[i] = ys[i];
	}
}

void TaquinGame::readBoundary() {
	int mx, my;
	bool c;
	boundary(mx, my, c);
	_mx = mx;
	_my = my;
	if (c)
		_click = true;   // FUN_0041a298 only ever sets the flag
}

void TaquinGame::draw(int16 extra) {
	drawBackground();
	for (int i = 0; i < 9; i++) {
		const int16 v = board[i];
		if (v > 0 && movingCell != i)
			drawSprite('P', cellX[i], cellY[i], v - 1);
		if (v > 0 && movingCell == i) {
			// The moved tile is drawn once, 30 pixels back toward its cell.
			drawSprite('P', (int16)(cellX[i] + moveDx), (int16)(cellY[i] + moveDy), v - 1);
			movingCell = 99;
		}
	}
	if (extra > 0)
		drawSprite('P', cellX[6], cellY[6], extra);
	// The win test is made here, on every drawing.
	if (result == 0) {
		result = 1;
		for (int i = 0; i < 8; i++)
			if (board[i] != kTarget[i])
				result = 0;
	}
}

void TaquinGame::frame(int16 extra) {
	draw(extra);
	drawBackground();
	present();
	_click = false;
	readBoundary();
}

void TaquinGame::error() {
	playSound(3);
	if (cursorFrame == 2)
		return;
	cursorFrame = 2;
	cursorMode = 2;
	cursorStep = 1;
}

static bool outside(int mx, int my) {
	return mx < 0x50 || mx > 0x230 || my < 0x32 || my > 0x1ae;
}

void TaquinGame::click() {
	const uint32 col = (uint32)(_mx - cellX[0]) / (uint32)(cellX[1] - cellX[0]);
	const uint32 row = (uint32)(_my - cellY[0]) / (uint32)(cellY[3] - cellY[0]);
	if (col >= 3 || row >= 3) {
		if (outside(_mx, _my))
			result = 2;
		else
			error();
		return;
	}
	int e = 99;
	for (int i = 0; i < 9; i++)
		if (board[i] == 0)
			e = i;
	const int c = (int)(row * 3 + col);
	if (e >= 99) {
		error();
		return;
	}
	const int d = ABS(c - e);
	if (!(d == 3 || (d == 1 && e / 3 == c / 3))) {
		error();
		return;
	}
	playSound(2);
	board[e] = board[c];
	board[c] = 0;
	movingCell = (int16)e;
	moveDy = 0;
	moveDx = 0;
	switch (c - e + 3) {
	case 0: moveDy = -30; break;
	case 2: moveDx = -30; break;
	case 4: moveDx = 30; break;
	case 6: moveDy = 30; break;
	default: break;
	}
}

void TaquinGame::cursor() {
	if (cursorMode == 2) {
		// FUN_00445d70: frames 2..6 and back to -1, then the mode ends.
		drawSprite('C', _mx, _my, cursorFrame);
		cursorFrame += cursorStep;
		if (cursorFrame == 6)
			cursorStep = -1;
		if (cursorFrame <= -1)
			cursorMode = 0;
		return;
	}
	if (outside(_mx, _my))
		drawSprite('C', _mx, _my, 11);
	else
		drawStandardCursor(_mx, _my);
}

void TaquinGame::winAnimation() {
	movingCell = 99;
	playSound(2);
	if (!withPiece)
		return;
	for (int i = 0; i < 15; i++)
		frame(0);
	for (int i = 14; i > 8; i -= 2)
		frame((int16)i);
	for (int i = 0; i < 20; i++)
		frame(6);
}

int TaquinGame::run() {
	startInput(_mx, _my, _click);
	withPiece = (objectInHand == 0x142);
	playSound(0);
	// FUN_00446bbc.
	objectInHand = 0;
	movingCell = 99;
	if (withPiece) {
		for (int i = 0; i < 15; i++)
			frame(0);
		board[6] = 0;
		for (int i = 9; i < 15; i += 3)
			frame((int16)i);
	}
	result = 0;
	do {
		if (_variant == 7)
			readBoundary();
		if (_click)
			click();
		draw(0);
		cursor();
		drawBackground();
		present();
		_click = false;
		if (_variant != 7)
			readBoundary();
	} while (result == 0 && !aborted());
	if (result == 1) {
		winAnimation();
		return 255;
	}
	return 2;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

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

#include "common/debug.h"
#include "common/util.h"

#include "cryomni3d/atlantis/hexa_game.h"

namespace CryOmni3D {
namespace Atlantis {

static const HexaGame::Piece kHexaPieces[HexaGame::kNumPieces] = {
	{ 0, 0, 0, 60, 50, 2 }, { 6, 0, 0, 110, 50, 3 }, { 12, 0, 0, 190, 50, 1 }
};
// The start table (0x004971f0): class, orientation, x, y.
static const int16 kHexaStart[HexaGame::kNumPieces][4] = {
	{ 1, 4, 220, 300 }, { 1, 4, 309, 120 }, { 1, 2, 400, 300 }
};
static const int16 kDropDx[17] = { 0, -1, 0, 0, 1, -1, -1, 1, 1, -2, 2, 0, 0, -2, 2, 2, -2 };
static const int16 kDropDy[17] = { 0, 0, 1, -1, 0, 1, -1, 1, -1, 0, 0, 2, -2, 2, -2, 2, -2 };

HexaGame::HexaGame()
	: grabX(0), grabY(0), repeat(0), unkF4(0), held(0), cursorMode(0), cursorFrame(0), cursorStep(0),
	  objectInHand(0), flagTemp6(0), result(0), _mx(0), _my(0), _click(false), _right(false) {
	memcpy(pieces, kHexaPieces, sizeof(pieces));
}

void HexaGame::readBoundary() {
	int mx, my;
	bool c, r;
	boundary(mx, my, c, r);
	_mx = mx;
	_my = my;
	if (c)
		_click = true;
	if (r)
		_right = true;
}

bool HexaGame::placePiece(int i, int x, int y) {
	Piece &pc = pieces[i];
	const int f = (int16)(pc.base + pc.orient);
	Common::Array<uint32> cells;
	if (f >= 0 && (uint)f < coverage.size()) {
		const MetaGame::Coverage &c = coverage[f];
		for (uint k = 0; k < c.dx.size(); k++) {
			const int px = x + c.dx[k], py = y + c.dy[k];
			if (px >= 0 && px < 640 && py >= 0 && py < 480)
				cells.push_back((uint32)(py * 640 + px));
		}
	}
	uint16 hist[32];
	memset(hist, 0, sizeof(hist));
	for (uint k = 0; k < cells.size(); k++)
		hist[map[cells[k]] & 0x1f]++;
	int16 best = 0;
	int m = 0;
	for (int c = 0; c < 32; c++)
		if (best < (int16)hist[c]) {
			best = (int16)hist[c];
			m = c;
		}
	// s1 and h0 are read as dwords (hist[1] | hist[2] << 16, hist[0] |
	// hist[1] << 16); only their low words count below.
	const int32 s1 = (int32)((uint32)hist[1] | ((uint32)hist[2] << 16));
	int32 s2 = 0;
	for (int c = 10; c < 0x13; c++)
		s2 += hist[c];
	const int32 h0 = (int32)((uint32)hist[0] | ((uint32)hist[1] << 16));
	const int16 t = (int16)(s1 + h0 + s2);
	int p0, p1, p2;
	if (t != 0) {
		p0 = (int16)h0 * 100 / t;
		p1 = (int16)s1 * 100 / t;
		p2 = (int16)s2 * 100 / t;
		// A single pixel off the board or on another piece counts.
		if ((int16)h0 > 0 && p0 == 0)
			p0 = 1;
		if ((int16)s2 > 0 && p2 == 0)
			p2 = 1;
	} else {
		p1 = p0 = 0;
		p2 = 100;
	}
	const bool onBoard = (p0 == 0 && p2 == 0);
	if (!(onBoard || p1 + p2 == 0))
		return false;
	const byte mark = (byte)(10 + (i & 7));
	for (uint k = 0; k < cells.size(); k++) {
		byte &b = map[cells[k]];
		if (onBoard ? (byte)(b - 1) < 4 : b == 0)
			b = mark;
	}
	pc.cls = (int16)m;
	pc.x = (int16)x;
	pc.y = (int16)y;
	return true;
}

bool HexaGame::won() const {
	const Piece &a = pieces[0], &b = pieces[1], &c = pieces[2];
	if (!(a.orient == 5 && b.orient == 3 && c.orient == 5))
		return false;
	if (a.x < 0xdc || b.x < 0xdc || c.x < 0xdc)
		return false;
	if (a.x > 0x190 || b.x > 0x190 || c.x > 0x190)
		return false;
	if (a.y < 0xa0 || b.y < 0xa0 || c.y < 0xa0)
		return false;
	if (a.y > 0x14a || b.y > 0x14a || c.y > 0x14a)
		return false;
	if (b.x > a.x || b.x > c.x)
		return false;
	if (a.y > b.y || a.y > c.y)
		return false;
	if (c.y < a.y || c.y < b.y)
		return false;
	return true;
}

void HexaGame::setUp() {
	// The entry sets OBJETENMAIN to 0x192 and then tests it, so the pieces
	// are always there and FlagTemp6 is always 1.
	int count = 0;
	objectInHand = 0x192;
	if (objectInHand == 0x192) {
		count++;
		flagTemp6 = 1;
	}
	if (flagTemp6 == 2)
		count++;
	for (int i = 0; i < kNumPieces; i++) {
		pieces[i].orient = kHexaStart[i][1];
		pieces[i].cls = count ? kHexaStart[i][0] : -1;
		if (pieces[i].cls >= 0)
			placePiece(i, kHexaStart[i][2], kHexaStart[i][3]);
	}
	objectInHand = 0;
}

void HexaGame::refuse() {
	if (cursorFrame == 2)
		return;
	cursorFrame = 2;
	cursorMode = 2;
	cursorStep = 1;
	playSound(3);
}

bool HexaGame::drop(int i, int x, int y) {
	for (int k = 0; k < 17; k++)
		if (placePiece(i, x + kDropDx[k], y + kDropDy[k]))
			return true;
	return false;
}

void HexaGame::rightButton() {
	if (_right) {
		if (repeat == 0 && held) {
			Piece &pc = pieces[held - 1];
			grabY = 0;
			grabX = 0;
			pc.orient++;
			if (pc.orient >= 6)
				pc.orient = 0;
			playSound(0);
		}
		repeat++;
		if (repeat > 0x32)
			repeat = 0;
		_right = false;
	} else {
		repeat = 0;
	}
}

void HexaGame::leftClick() {
	if (held) {
		const int p = held - 1;
		const bool ok = drop(p, _mx + grabX, _my + grabY);
		// Port-only log, to replay a player's game in the emulated original.
		debug(1, "hexa: drop piece %d orientation %d at %d,%d (mouse %d,%d) %s", p, pieces[p].orient,
		      _mx + grabX, _my + grabY, _mx, _my, ok ? "accepted" : "refused");
		if (ok) {
			held = 0;
			playSound(2);
			if (won()) {
				result = 1;
				debug(1, "hexa: won with (%d %d,%d) (%d %d,%d) (%d %d,%d)", pieces[0].orient, pieces[0].x,
				      pieces[0].y, pieces[1].orient, pieces[1].x, pieces[1].y, pieces[2].orient,
				      pieces[2].x, pieces[2].y);
			}
		} else {
			refuse();
		}
		return;
	}
	const byte b = map[(uint32)(_my * 640 + _mx) % kMapSize];
	if (b >= 0xa && b <= 0x10) {
		const int p = b - 0xa;
		held = p + 1;
		grabX = pieces[p].x - _mx;
		grabY = pieces[p].y - _my;
		const byte cls = (byte)pieces[p].cls;
		for (uint32 k = 0; k < kMapSize; k++)
			if (map[k] == b)
				map[k] = cls;
		playSound(1);
	} else if (b == 0x1f) {
		result = 2;
	} else {
		refuse();
	}
}

void HexaGame::draw() {
	drawBackground();
	for (int i = 0; i < kNumPieces; i++)
		if (held - 1 != i && pieces[i].cls >= 0)
			drawSprite('P', pieces[i].x, pieces[i].y, (int16)(pieces[i].base + pieces[i].orient));
}

void HexaGame::cursor() {
	if (held) {
		const Piece &pc = pieces[held - 1];
		drawSprite('P', (int16)(_mx + grabX), (int16)(_my + grabY), (int16)(pc.base + pc.orient));
	}
	if (cursorMode >= 2) {
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
	if (held) {
		drawStandardCursor(_mx, _my);
		return;
	}
	const byte b = map[(uint32)(_my * 640 + _mx) % kMapSize];
	drawSprite('C', _mx, _my, b == 0x1f ? 10 : 5);
}

void HexaGame::frame() {
	draw();
	drawBackground();
	present();
	_click = false;
	readBoundary();
}

void HexaGame::winAnimation() {
	playSound(4);
	for (int n = 0; n < 0x10; n++)
		for (int i = 0; i < kNumPieces; i++) {
			pieces[i].cls = -1;
			frame();
			pieces[i].cls = 1;
			frame();
		}
}

int HexaGame::run() {
	startInput(_mx, _my, _click, _right);
	reloadMap();
	setUp();
	result = 0;
	do {
		readBoundary();
		rightButton();
		if (_click)
			leftClick();
		draw();
		cursor();
		drawBackground();
		present();
		_click = false;
	} while (result == 0 && !aborted());
	if (result == 1) {
		winAnimation();
		return 255;
	}
	return 2;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

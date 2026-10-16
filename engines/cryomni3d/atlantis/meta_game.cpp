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

#include "common/endian.h"
#include "common/util.h"

#include "cryomni3d/atlantis/meta_game.h"

namespace CryOmni3D {
namespace Atlantis {

// The piece records as in atlantis.exe's data (0x00496fe0).
static const MetaGame::Piece kPieces[MetaGame::kNumPieces] = {
	{  0, 15, 0, 0,  60, 50, 2 }, {  8, 15, 0, 0, 110, 50, 3 }, { 16, 19, 0, 0, 190, 50, 4 },
	{ 24, 23, 0, 0, 300, 50, 6 }, { 40, 15, 0, 0, 400, 50, 5 }, { 32, 15, 0, 0, 450, 50, 4 },
	{ 24, 23, 0, 0, 530, 50, 1 }
};
// The stage tables (0x00497040 + 0x38 * (v - 1)): class, orientation, x, y.
static const int16 kStages[4][MetaGame::kNumPieces][4] = {
	{ { 1, 4, 317, 391 }, { 0, 4, 321, 207 }, { 0, 2, 291, 242 }, { 0, 6, 351, 256 },
	  { -1, 0, 226, 343 }, { -1, 0, 421, 364 }, { -1, 0, 257, 143 } },
	{ { 1, 4, 317, 391 }, { 1, 6, 334, 407 }, { 1, 5, 366, 421 }, { 1, 0, 286, 409 },
	  { 0, 0, 320, 240 }, { -1, 0, 421, 364 }, { -1, 0, 257, 143 } },
	{ { 2, 7, 460, 293 }, { 2, 3, 440, 225 }, { 2, 4, 492, 259 }, { 2, 5, 480, 212 },
	  { 2, 1, 447, 259 }, { 0, 0, 320, 240 }, { -1, 0, 257, 143 } },
	{ { 3, 4, 318, 138 }, { 3, 6, 367, 58 }, { 3, 5, 304, 106 }, { 3, 4, 317, 57 },
	  { 3, 2, 271, 76 }, { 3, 1, 350, 90 }, { 0, 0, 320, 240 } }
};
// The turn centres by picture (x: shorts from 0x00497120, y: from 0x0049712a).
static const int16 kCentreX[5] = { 0, 319, 320, 320, 319 };
static const int16 kCentreY[5] = { 0, 239, 239, 240, 240 };
// pzmetam.bmp holds two bytes after its 640x480 map (0x436 + 0x4b000 + 2 =
// 0x4b438 bytes, the size FUN_004472cc allocates).
static const uint32 kMapFileTail = 2;
// The drop offsets (shorts from 0x00497134 and 0x00497158; 17 entries).
static const int16 kDropDx[17] = { 0, -1, 0, 0, 1, -1, -1, 1, 1, -2, 2, 0, 0, -2, 2, 2, -2 };
static const int16 kDropDy[17] = { 0, 0, 1, -1, 0, 1, -1, 1, -1, 0, 0, 2, -2, 2, -2, 2, -2 };

MetaGame::MetaGame()
	: count(0), picture(0), variant(0), grabX(0), grabY(0), repeat(0), unkF4(0), held(0),
	  cursorMode(0), cursorFrame(0), cursorStep(0), objectInHand(0), broughtFlag(0), result(0),
	  _mx(0), _my(0), _click(false), _right(false) {
	memcpy(pieces, kPieces, sizeof(pieces));
}

bool MetaGame::readCoverage(const byte *d, uint32 size, Common::Array<Coverage> &coverage) {
	if (size < 4)
		return false;
	uint32 p = 0;
	const uint16 flags = READ_LE_UINT16(d);
	p = 2;
	uint16 table[256];
	memset(table, 0, sizeof(table));
	if (flags & 4) {
		if (p + 512 > size)
			return false;
		for (int i = 0; i < 256; i++)
			table[i] = READ_LE_UINT16(d + p + 2 * i);
		p += 512;
	}
	if (flags & 8)
		p += 512;
	if (p + 2 > size)
		return false;
	const uint16 n = READ_LE_UINT16(d + p);
	p += 2;
	const uint32 base = p;
	if (base + 4 * n > size)
		return false;
	coverage.resize(n);
	for (uint f = 0; f < n; f++) {
		uint32 q = base + READ_LE_UINT32(d + base + 4 * f);
		if (q + 8 > size)
			return false;
		const uint16 w = READ_LE_UINT16(d + q), h = READ_LE_UINT16(d + q + 2);
		const int16 xo = (int16)READ_LE_UINT16(d + q + 4), yo = (int16)READ_LE_UINT16(d + q + 6);
		q += 8;
		if (q + 4 * h > size)
			return false;
		const uint32 pix = q + 4 * h;
		Coverage &c = coverage[f];
		for (uint ry = 0; ry < h; ry++) {
			uint32 r = pix + READ_LE_UINT32(d + q + 4 * ry);
			uint rx = 0;
			while (rx < w && r < size) {
				const byte b = d[r++];
				const int type = b >> 6, cnt = b & 0x3f;
				if (type == 0) {
					rx += cnt;
				} else if (type == 1) {
					for (int k = 0; k < cnt && r < size; k++, rx++)
						if (table[d[r++]] != 0) {
							c.dx.push_back((int16)(rx - xo));
							c.dy.push_back((int16)(ry - yo));
						}
				} else {
					for (int k = 0; k < cnt && r + 1 < size; k++, rx++) {
						const byte factor = d[r] & 0x1f;
						const uint16 col = table[d[r + 1]];
						r += 2;
						if (factor != 0 && col != 0) {
							c.dx.push_back((int16)(rx - xo));
							c.dy.push_back((int16)(ry - yo));
						}
					}
				}
			}
		}
	}
	return true;
}

void MetaGame::readBoundary() {
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

void MetaGame::setPicture(int n) {
	if (n > 4)
		n = 1;
	picture = (int16)n;
	loadPicture(n);
}

bool MetaGame::placePiece(int i, int x, int y) {
	Piece &pc = pieces[i];
	const int f = (int16)(pc.base + pc.orient);
	// The covered words of the piece drawn at (x, y), clipped to the screen.
	Common::Array<uint32> cells;
	if (f >= 0 && (uint)f < coverage.size()) {
		const Coverage &c = coverage[f];
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
	// The class with the largest count (the first, signed 16-bit compare).
	int16 best = 0;
	int m = 0;
	for (int c = 0; c < 32; c++)
		if (best < (int16)hist[c]) {
			best = (int16)hist[c];
			m = c;
		}
	int32 s1 = 0, s2 = 0;
	for (int c = 1; c < 5; c++)
		s1 += hist[c];
	for (int c = 10; c < 0x13; c++)
		s2 += hist[c];
	// The dword reads of hist[31] and hist[0] also take the next word (m,
	// hist[1]); only the low 16 bits are used below.
	s2 += (int32)((uint32)hist[31] | ((uint32)(uint16)m << 16));
	const int32 h0 = (int32)((uint32)hist[0] | ((uint32)hist[1] << 16));
	const int16 t = (int16)(s1 + h0 + s2);
	int p0, p1, p2;
	if (t != 0) {
		p0 = (int16)h0 * 100 / t;
		p1 = (int16)s1 * 100 / t;
		p2 = (int16)s2 * 100 / t;
	} else {
		p1 = p0 = 0;
		p2 = 100;
	}
	const bool inZone = (p0 == 0 && p2 < 3);
	if (!(inZone || p1 + p2 == 0))
		return false;
	// FUN_00410e35: the piece's mark, on the zone (1..4) or on the centre (0).
	const byte mark = (byte)(10 + (i & 7));
	for (uint k = 0; k < cells.size(); k++) {
		byte &b = map[cells[k]];
		if (inZone ? (byte)(b - 1) < 4 : b == 0)
			b = mark;
	}
	pc.cls = (int16)m;
	pc.x = (int16)x;
	pc.y = (int16)y;
	return true;
}

void MetaGame::turnBoard(bool advance) {
	const int old = picture;
	if (advance)
		setPicture((int16)(old + 1));
	const int nw = picture;
	const int32 offset = (kCentreX[nw] + kCentreY[old] - 0x50) * 0x280 + (kCentreX[old] + 1 - kCentreY[nw]);
	Common::Array<byte> work(kMapSize, 0x1f);
	uint32 o = 640;
	int32 src = offset;
	for (int row = 1; row < 0x1df; row++) {
		o += 80;
		int32 s = src;
		for (int k = 0; k < 480; k++) {
			byte v = 0;
			if (s >= 0 && (uint32)s < kMapSize) {
				v = map[s];
			} else if ((uint32)s >= kMapSize + kMapFileTail) {
				// Past the map's end: the original's pool (FUN_004475b4, a
				// bump allocator) puts the work buffer (0x96000 bytes,
				// FUN_0044763c @ 0x00447688) right after pzmetam.bmp
				// (0x4b438 bytes, FUN_004472cc), so these reads take the
				// work bytes written so far.  Only a turn in place of
				// picture 3 (the set-up of a place % 10 == 5) reads there:
				// s = kMapSize + 81..557, in the work buffer's first row
				// of 0x1f.  Every turn reads s >= 80 (no read before the
				// map).
				const uint32 w = (uint32)s - kMapSize - kMapFileTail;
				if (w < o)
					v = work[w];
			}
			work[o++] = v;
			s -= 640;
		}
		o += 80;
		src++;
	}
	map = work;
	for (int i = 0; i < kNumPieces; i++) {
		Piece &pc = pieces[i];
		if (pc.cls < 0)
			continue;
		pc.orient += 2;
		if (pc.orient >= 8)
			pc.orient -= 8;
		const int dx = pc.x - kCentreX[old], dy = pc.y - kCentreY[old];
		pc.x = (int16)(kCentreX[nw] - dy);
		pc.y = (int16)(kCentreY[nw] + dx);
	}
}

void MetaGame::setUp() {
	if (objectInHand == 0x13c + variant) {
		count++;
		broughtFlag = 1;
	}
	objectInHand = 0;
	const int r = currentPlace() % 10;
	int pic = 1;
	if (r == 5)
		pic = 3;
	else if (r == 6)
		pic = 4;
	else if (r == 8)
		pic = 2;
	if (variant == 1)
		count = 4;
	for (int i = 0; i < kNumPieces; i++) {
		const int16 *e = kStages[variant - 1][i];
		pieces[i].orient = e[1];
		pieces[i].cls = (count > i) ? e[0] : -1;
		if (pieces[i].cls >= 0)
			placePiece(i, e[2], e[3]);
	}
	setPicture(pic);
	for (int k = 1; k < pic; k++)
		turnBoard(false);
}

void MetaGame::refuse() {
	if (cursorFrame == 2)
		return;
	cursorFrame = 2;
	cursorMode = 2;
	cursorStep = 1;
	playSound(4);
}

bool MetaGame::drop(int i, int x, int y) {
	for (int k = 0; k < 17; k++)
		if (placePiece(i, x + kDropDx[k], y + kDropDy[k]))
			return true;
	return false;
}

bool MetaGame::won() const {
	for (int i = 0; i < variant + 3; i++)
		if (pieces[i].cls != variant)
			return false;
	return true;
}

void MetaGame::rightButton() {
	if (_right) {
		if (repeat == 0) {
			if (held) {
				Piece &pc = pieces[held - 1];
				grabY = 0;
				grabX = 0;
				pc.orient++;
				if (pc.orient >= 8)
					pc.orient = 0;
			} else {
				turnBoard(true);
			}
			playSound(0);
		}
		repeat++;
		if (repeat > 0x32)
			repeat = 0;
	} else {
		repeat = 0;
	}
	_right = false;
}

void MetaGame::leftClick() {
	if (held) {
		const int p = held - 1;
		if (drop(p, _mx + grabX, _my + grabY)) {
			held = 0;
			playSound(2);
			if (won())
				result = 1;
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
		// FUN_00410e10: the mark goes back to the piece's class.
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

void MetaGame::draw() {
	drawBackground();
	for (int i = 0; i < kNumPieces; i++)
		if (held - 1 != i && pieces[i].cls >= 0)
			drawSprite('P', pieces[i].x, pieces[i].y, (int16)(pieces[i].base + pieces[i].orient));
}

void MetaGame::cursor() {
	if (held) {
		const Piece &pc = pieces[held - 1];
		drawSprite('P', (int16)(_mx + grabX), (int16)(_my + grabY), (int16)(pc.base + pc.orient));
	}
	if (cursorMode >= 2) {
		// FUN_00448958: three bounces of the frame between 4 and -1.
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

void MetaGame::frame() {
	draw();
	drawBackground();
	present();
	_click = false;
	readBoundary();
}

void MetaGame::winAnimation() {
	playSound(3);
	for (int n = 0; n < 0x10; n++)
		for (int i = 0; i < variant + 3; i++) {
			pieces[i].cls = -1;
			frame();
			pieces[i].cls = variant;
			frame();
		}
}

int MetaGame::run(int v) {
	variant = (int16)v;
	startInput(_mx, _my, _click, _right);
	reloadMap();
	setUp();
	result = 0;
	do {
		rightButton();
		if (_click)
			leftClick();
		draw();
		cursor();
		drawBackground();
		present();
		_click = false;
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

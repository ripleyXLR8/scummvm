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

#include "cryomni3d/atlantis/muria_game.h"

namespace CryOmni3D {
namespace Atlantis {

static const MuriaGame::Piece kPieces[MuriaGame::kNumPieces] = {
	{ 1, 570, 403, -1 }, { 1, 212, 407, -1 }, { 1, 444, 436, -1 }, { 1, 48, 369, -1 },
	{ 2, 112, 437, -1 }, { 2, 327, 390, -1 }, { 2, 363, 439, -1 }, { 2, 181, 368, -1 },
	{ 3, 280, 439, -1 }, { 3, 479, 382, -1 }, { 3, 124, 368, -1 }, { 3, 523, 454, -1 }
};
static const MuriaGame::Slot kSlots[MuriaGame::kNumSlots] = {
	{ 1, 130, 69, 186, 184, -1 }, { 1, 233, 68, 267, 184, -1 }, { 1, 361, 68, 379, 184, -1 }, { 1, 438, 69, 457, 184, -1 },
	{ 2, 148, 204, 175, 227, -1 }, { 2, 237, 204, 262, 227, -1 }, { 2, 356, 205, 380, 227, -1 }, { 2, 444, 203, 468, 227, -1 },
	{ 3, 140, 250, 164, 270, -1 }, { 3, 236, 250, 261, 270, -1 }, { 3, 360, 249, 385, 270, -1 }, { 3, 455, 247, 478, 269, -1 }
};

MuriaGame::MuriaGame()
	: hand(0), cursorMode(0), cursorFrame(0), cursorStep(0), result(0), click(false), flagTemp3(0), _mx(0), _my(0) {
	memcpy(pieces, kPieces, sizeof(pieces));
	memcpy(slots, kSlots, sizeof(slots));
}

void MuriaGame::readBoundary() {
	bool c;
	boundary(_mx, _my, c);
	click = c;
}

void MuriaGame::setUp() {
	for (int i = 0; i < kNumPieces; i++)
		pieces[i].slot = -1;
	for (int i = 0; i < kNumSlots; i++)
		slots[i].piece = -1;
	loadPicture("puzmuria.tga");
	hand = 0;
}

int MuriaGame::slotAt(int mx, int my) const {
	// The nearest click point within 40; the top row is aimed 50 pixels
	// higher, with its vertical distance halved.
	int best = 999, found = -1;
	for (int i = 0; i < kNumSlots; i++) {
		int d;
		if (i < 4)
			d = ABS(my + 0x32 - slots[i].cy) / 2 + ABS(mx - slots[i].cx);
		else
			d = ABS(mx - slots[i].cx) + ABS(my - slots[i].cy);
		if (d < best) {
			best = d;
			found = i;
		}
	}
	return best < 0x28 ? found : -1;
}

bool MuriaGame::won() const {
	for (int i = 0; i < kNumPieces; i++)
		if (pieces[i].slot != i)
			return false;
	return true;
}

void MuriaGame::refuse() {
	if (cursorFrame == 2)
		return;
	cursorFrame = 2;
	cursorMode = 2;
	cursorStep = 1;
	playSound(2);
}

void MuriaGame::placed(int n) {
	// Two passes drawn, then FLAGTEMP3 = n and the chapter's /sel scan.
	for (int k = 0; k < 2; k++) {
		draw();
		cursor();
		drawBackground();
		present();
	}
	flagTemp3 = n;
	scriptHook();
}

void MuriaGame::handleClick() {
	if (hand != 0) {
		const int p = hand - 1;
		if (_my > 0x15e) {
			// Below the tablets: the piece goes back home.
			pieces[p].slot = -1;
			hand = 0;
			playSound(4);
			return;
		}
		const int s = slotAt(_mx, _my);
		if (s >= 0 && slots[s].piece == -1 && slots[s].kind == pieces[p].kind) {
			pieces[p].slot = (int16)s;
			slots[s].piece = (int16)p;
			hand = 0;
			playSound(0);
			if (s < 4 && s == p)
				placed(s + 0x3fd);
		} else {
			refuse();
		}
		return;
	}
	const int s = slotAt(_mx, _my);
	if (s >= 0) {
		if (slots[s].piece != -1) {
			// The piece on the slot is taken back.
			hand = slots[s].piece;
			slots[s].piece = -1;
			pieces[hand].slot = -1;
			hand++;
			playSound(1);
		} else {
			refuse();
		}
	} else {
		// The nearest free piece within 35 of its home.
		int best = 999, found = -1;
		for (int i = 0; i < kNumPieces; i++) {
			if (pieces[i].slot != -1)
				continue;
			const int d = ABS(_mx - pieces[i].x) + ABS(_my - pieces[i].y);
			if (d < best) {
				best = d;
				found = i;
			}
		}
		if (best < 0x23) {
			hand = found + 1;
			playSound(1);
		}
	}
	if (_my < 40)
		result = 2;
	else if (hand == 0)
		refuse();
}

void MuriaGame::draw() {
	drawBackground();
	for (int i = 0; i < kNumPieces; i++) {
		const int s = pieces[i].slot;
		if (hand - 1 == i || s == -2)
			continue;
		if (s == -1)
			drawSprite('P', pieces[i].x, pieces[i].y, i + 12);
		else
			drawSprite('P', slots[s].x, slots[s].y, s % 4 + 4 * i + 0x18);
	}
}

void MuriaGame::cursor() {
	if (hand != 0)
		drawSprite('P', _mx, _my, (int16)hand - 1);
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
	drawSprite('C', _mx, _my, _my < 40 ? 10 : 5);
}

void MuriaGame::pass() {
	draw();
	cursor();
	drawBackground();
	present();
}

void MuriaGame::winAnimation() {
	// Sound 3, then pieces 4..11 blink in turn, eight times, a pass per
	// half-step (no cursor).
	playSound(3);
	for (int n = 0; n < 8; n++)
		for (int i = 4; i < kNumPieces; i++) {
			pieces[i].slot = -2;
			draw();
			drawBackground();
			present();
			click = false;
			readBoundary();
			pieces[i].slot = (int16)i;
			draw();
			drawBackground();
			present();
			click = false;
			readBoundary();
		}
}

int MuriaGame::run() {
	bool c;
	startInput(_mx, _my, c);
	click = c;
	setUp();
	result = 0;
	do {
		if (click)
			handleClick();
		pass();
		click = false;
		if (won())
			result = 1;
		readBoundary();
	} while (result == 0 && !aborted());
	flagTemp3 = 0;
	if (result == 1) {
		winAnimation();
		stopSounds();
		return 255;
	}
	stopSounds();
	return 2;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

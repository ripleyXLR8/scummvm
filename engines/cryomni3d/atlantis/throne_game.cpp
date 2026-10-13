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

#include "cryomni3d/atlantis/throne_game.h"

namespace CryOmni3D {
namespace Atlantis {

// The parts as in atlantis.exe's data (0x00497216).
static const ThroneGame::Part kParts[ThroneGame::kNumParts] = {
	{  0, 8, 4, 0, 390, 148 }, {  8, 8, 1, 0, 389, 283 },
	{ 16, 8, 5, 0, 254, 149 }, { 24, 8, 2, 0, 254, 283 },
	{ 32, 2, 0, 0, 447,   4 }, { 34, 2, 0, 0, 447, 405 },
	{ 36, 2, 0, 0, 112,   4 }, { 38, 2, 0, 0, 112, 405 },
	{ 40, 4, 0, 0, 321, 435 }
};

ThroneGame::ThroneGame()
	: cursorMode(0), cursorFrame(0), cursorStep(0), counter(0), active(0), result(0) {
	memcpy(parts, kParts, sizeof(parts));
}

bool ThroneGame::solved() const {
	for (int k = 0; k < 4; k++)
		if (parts[k].cur != 0)
			return false;
	return true;
}

void ThroneGame::update() {
	const int32 a = active;
	if (a == 0)
		return;
	counter++;
	if (a == 8) {
		// The lever falls back after 14 passes, then the rings are checked.
		if (counter == 0xe) {
			parts[8].cur = 0;
			active = 0;
			if (solved())
				result = 1;
		}
		return;
	}
	// A button: its ring turns one step on the 8th pass, the button rises
	// three passes later.
	if (counter > 7 && counter < 0x64) {
		Part &ring = parts[a - 4];
		ring.cur++;
		if (ring.cur >= ring.count)
			ring.cur = 0;
		counter = 0x64;
	}
	if (counter > 0x66) {
		parts[a].cur = 0;
		active = 0;
	}
}

void ThroneGame::errorCursor(int &sound) {
	if (cursorFrame == 2)
		return;
	cursorFrame = 2;
	cursorMode = 2;
	cursorStep = 1;
	sound = 2;
}

void ThroneGame::click(int mx, int my, int &sound) {
	sound = -1;
	if (active != 0) {
		errorCursor(sound);
		return;
	}
	for (int k = 4; k < 8; k++) {
		const uint32 dx = (uint32)(mx - parts[k].x), dy = (uint32)(my - parts[k].y);
		if (dx < 0x52 && dy < 0x22) {
			parts[k].cur++;
			active = k;
			counter = 0;
			sound = 0;
		}
	}
	const uint32 d = (uint32)(ABS(mx - parts[8].x) + ABS(my - parts[8].y));
	if (d < 0x32) {
		parts[8].cur++;
		active = 8;
		counter = 0;
		sound = 1;
	}
	if (active == 0) {
		if (mx >= 0x230 || mx < 0x50)
			result = 2;
		else
			errorCursor(sound);
	}
}

void ThroneGame::drawCursor(int mx, int my) {
	if (cursorMode >= 2) {
		// FUN_00448958: three bounces of the frame between 4 and -1.
		drawSprite('C', mx, my, cursorFrame);
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
	drawSprite('C', mx, my, (mx >= 0x230 || mx < 0x50) ? 10 : 5);
}

int ThroneGame::run() {
	for (int k = 0; k < kNumParts; k++)
		parts[k].cur = parts[k].init;
	active = 0;
	result = 0;
	do {
		int mx, my;
		bool clicked;
		readInput(mx, my, clicked);
		update();
		if (clicked) {
			int sound;
			click(mx, my, sound);
			if (sound >= 0)
				playSound(sound);
		}
		drawBackground();
		for (int k = 0; k < kNumParts; k++)
			drawSprite('P', parts[k].x, parts[k].y, (int16)(parts[k].base + parts[k].cur));
		drawCursor(mx, my);
		drawBackground();
		present();
	} while (result == 0 && !aborted());
	if (result == 1) {
		playSound(3);
		for (int i = 0; i < 5; i++)
			waitTicks(100);
		return 255;
	}
	return 2;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

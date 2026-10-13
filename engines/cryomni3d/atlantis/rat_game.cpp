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

#include "cryomni3d/atlantis/rat_game.h"

namespace CryOmni3D {
namespace Atlantis {

// FUN_00434dfc: slot k reads PUZZLES\<name> (names at 0x004bdf48, 12 bytes).
const char *const RatGame::kSlotFiles[RatGame::kNumSlots] = {
	"T0001.TRJ", "T0002.TRJ", "T0003.TRJ", "T0004.TRJ", "T0005.TRJ", "T0006.TRJ",
	"T0007.TRJ", "T0007A.TRJ", "T0007B.TRJ", "T0008.TRJ", "T0009.TRJ", "T0010.TRJ",
	"T0011.TRJ", "T0011A.TRJ", "T0011B.TRJ", "T0012.TRJ", "T0013.TRJ", "T0014.TRJ",
	"T0015.TRJ", "T0020.TRJ", "T0021.TRJ", "T0022.TRJ", "T0023.TRJ", "T0024.TRJ",
	"T0025.TRJ", "T0099.TRJ", "T00025.TRJ", "T1000.TRJ", "T0004bb.TRJ", "T0000.TRJ"
};

// Addresses of the state (see RatRules).
static const uint32 kEnded = 0x004969c2, kStarted = 0x004969c3;
static const uint32 kRatByte = 0x00496ca5, kRatX = 0x00496ca6, kRatY = 0x00496caa, kRatMode = 0x00496ca4;
static const uint32 kRats = 0x00496cae, kLossAnim = 0x00496cc4, kAmbientAt = 0x00496cc8;
static const int32 kWin = 255, kLost = 2;   // [0x00496d14], [0x00496d10]

enum { kAbove = RatRules::kAbove, kActive = RatRules::kActive, kPressed = RatRules::kPressed,
       kX = RatRules::kX, kY = RatRules::kY, kRest = RatRules::kRest,
       kPressedFrame = RatRules::kPressedFrame, kFrame = RatRules::kFrame };
static uint32 rec(int r, int off) { return RatRules::rec(r, off); }

// FUN_00451d6e then FISTP: truncation toward zero.
static int chop(double v) { return (int)v; }

RatGame::RatGame() : wheelY(0), _mx(0), _my(0), _click(false), _held(false) {
}

bool RatGame::loadSlot(int k, const byte *d, uint32 size) {
	if (size < 2)
		return false;
	const int n = (int16)READ_LE_UINT16(d);
	if (n < 0 || size < 2 + 9 * (uint32)n)
		return false;
	Slot &s = slots[k];
	s.b.resize(n);
	s.x.resize(n);
	s.y.resize(n);
	for (int i = 0; i < n; i++) {
		s.b[i] = d[2 + i];
		uint32 ux = READ_LE_UINT32(d + 2 + n + 4 * i), uy = READ_LE_UINT32(d + 2 + 5 * n + 4 * i);
		memcpy(&s.x[i], &ux, 4);
		memcpy(&s.y[i], &uy, 4);
	}
	s.pos = 0;
	return true;
}

void RatGame::frame() {
	readInput(_mx, _my, _click, _held);
	Common::Array<int> snd;
	rules.clicks(_mx, _my, _click, _held, snd);
	for (uint i = 0; i < snd.size(); i++)
		playSound(snd[i]);
	updateAndDraw();
	drawBackground(2);
	present();
}

void RatGame::updateAndDraw() {
	RatRules &r = rules;
	Common::Array<int> snd;
	r.animateRat();
	drawBackground(0);
	drawBackground(1);
	const uint32 now = ticks();
	auto drawRec = [&](int k) {
		drawSprite('P', r.s(rec(k, kX)), r.s(rec(k, kY)), r.s(rec(k, kFrame)));
	};
	for (int k = 0; k < 10; k++) {
		if (r.b(rec(k, kActive)) == 1)
			r.updateRecord(k, now, snd);
		if (r.b(rec(k, kAbove)) == 0)
			drawRec(k);
	}
	if (r.b(rec(10, kActive)) == 0)
		r.updateRecord(10, now, snd);
	if (r.b(rec(10, kAbove)) == 0)
		drawRec(10);
	for (int k = 11; k < 18; k++) {
		if (r.b(rec(k, kActive)) == 1)
			r.updateRecord(k, now, snd);
		if (r.b(rec(k, kAbove)) == 0)
			drawRec(k);
	}
	if (r.b(rec(18, kActive)) == 1)
		r.updateRecord(18, now, snd);
	if (r.b(rec(18, kAbove)) == 0)
		drawRec(18);
	for (int k = 19; k < 28; k++) {
		if (r.b(rec(k, kActive)) == 1)
			r.updateRecord(k, now, snd);
		if (r.b(rec(k, kAbove)) == 0)
			drawRec(k);
	}
	// The rat: (int16) of the truncated coordinates, the animation's frame.
	drawSprite('R', (int16)chop(r.f(kRatX)), (int16)chop(r.f(kRatY)),
	           r.s(0x00496c7e + 6 * r.b(kRatByte)));
	for (int k = 1; k < 30; k++)
		if (r.b(rec(k, kAbove)) == 1)
			drawRec(k);
	drawRec(28);
	drawRec(29);
	if (r.b(kRats) != 0)
		drawRatsLeft();
	// Each frame lasts at least 5 ticks (0x0077375c).
	waitUntil(ticks() + 5);
	if (ticks() > (uint32)r.i(kAmbientAt)) {
		if (randomValue() % 3 == 0 && r.b(kStarted) == 1 && r.i(kLossAnim) == 0)
			playSound(16 + (ticks() & 3));
		r.setI(kAmbientAt, (int32)(ticks() + 400));
	}
}

void RatGame::drawRatsLeft() {
	int x = rules.b(kRats) * 0x17 + 0x46;
	do {
		drawSprite('R', x, 0x1d6, 2);
		x -= 0x17;
	} while (x > 0x46);
}

bool RatGame::stepSlot(int k) {
	Slot &s = slots[k];
	if (k == 0 || k == 0x13 || k == 0x1a || k == 0x19 || k == 0x14 || k == 0x17 ||
	        k == 0x16 || k == 0x18 || k == 0x15)
		s.pos += 1;
	else if (k == 6)
		s.pos += (slots[6].pos < 0x32) ? 3 : 2;
	else if (k == 9)
		s.pos += 3;
	else
		s.pos += 2;
	if (s.pos >= (int)s.b.size())
		return false;
	rules.setB(kRatByte, s.b[s.pos]);
	rules.setF(kRatX, s.x[s.pos]);
	rules.setF(kRatY, s.y[s.pos]);
	return true;
}

bool RatGame::nearJump() const {
	const int dx = ABS(chop((double)rules.f(kRatX) + -200.0));
	const int dy = ABS(chop((double)rules.f(kRatY) + -300.0));
	const float d = (float)sqrt((double)((float)dx * (float)dx + (float)dy * (float)dy));
	return d < 20.0f;
}

void RatGame::runSlot(int k) {
	slots[k].pos = 0;
	bool more = true;
	do {
		frame();
		more = stepSlot(k);
	} while (!resultSet() && more);
}

int RatGame::run() {
	RatRules &r = rules;
	r.result = 0;
	r.setI(0x00496cb0, 0);
	r.setF(0x00496cb4, 98.0f);
	r.setB(kRats, 5);
	r.reset();
	r.setS(0x00496abd, r.s(0x00496ab9));
	r.setB(kEnded, 0);
	firstRat();
	if (r.result == kLost) {
		r.setB(kRats, r.b(kRats) - 1);
		r.result = 0;
		loseRat();
	}
	do {
		if (aborted())
			break;
		r.setI(0x00496cb0, 0);
		r.setF(0x00496cb4, 98.0f);
		r.reset();
		r.setS(0x00496abd, r.s(0x00496ab9));
		r.setB(kStarted, 0);
		nextRat();
		if (r.result == kLost) {
			r.setB(kRats, r.b(kRats) - 1);
			r.result = 0;
			loseRat();
			if (r.b(kRats) == 0)
				r.result = kLost;
		}
	} while (r.result == 0 && r.b(kEnded) == 0);
	if (r.b(kEnded) == 1)
		r.result = kLost;
	return r.result;
}

void RatGame::firstRat() {
	RatRules &r = rules;
	r.setB(kStarted, 0);
	r.setF(kRatX, -100.0f);
	r.setF(kRatY, -100.0f);
	r.setB(kRatMode, 0);
	do {
		frame();
	} while (r.b(kStarted) == 0 && r.result == 0 && !aborted());
	runSlot(0x1b);   // 0x004351ac: T1000, the drop
	r.setF(kRatY, r.f(kRatY) + 6.0f);
	r.setB(kRats, r.b(kRats) - 1);
	if (r.result != 0 || aborted())
		return;
	for (int k = 0; k < 10; k++)
		r.setB(rec(k, kActive), 1);
	for (int k = 11; k < 29; k++)
		r.setB(rec(k, kActive), 1);
	do {
		frame();
	} while (r.result == 0 && r.b(rec(1, kPressed)) == 0 && !aborted());
	r.setB(rec(1, kActive), 0);
	r.setB(kRatMode, 0);
	t0001();
}

void RatGame::nextRat() {
	RatRules &r = rules;
	r.setB(kStarted, 0);
	r.setF(kRatX, -100.0f);
	r.setF(kRatY, -100.0f);
	r.setB(kRatMode, 0);
	r.setB(kStarted, 0);
	do {
		frame();
	} while (r.b(kStarted) == 0 && r.result == 0 && !aborted());
	if (r.result != 0 || aborted())
		return;
	runSlot(0x1b);   // 0x004351ac: T1000, the drop
	r.setF(kRatY, r.f(kRatY) + 6.0f);
	for (int k = 0; k < 10; k++)
		r.setB(rec(k, kActive), 1);
	for (int k = 11; k < 29; k++)
		r.setB(rec(k, kActive), 1);
	do {
		frame();
	} while (r.result == 0 && r.b(rec(1, kPressed)) == 0 && !aborted());
	r.setB(rec(1, kActive), 0);
	r.setB(kRatMode, 0);
	t0001();
}

void RatGame::loseRat() {
	RatRules &r = rules;
	r.setI(kLossAnim, 1);
	const int x = (r.b(kRats) + 1) * 0x17 + 0x46;
	int count = 0, shown = 0;
	uint32 next = ticks() + 0xc8;
	playSound(14);
	do {
		readInput(_mx, _my, _click, _held);
		Common::Array<int> snd;
		r.clicks(_mx, _my, _click, _held, snd);
		for (uint i = 0; i < snd.size(); i++)
			playSound(snd[i]);
		updateAndDraw();
		if (ticks() > next) {
			next = ticks() + 0xc8;
			count += 5;
			shown = 1 - shown;
		}
		if (shown)
			drawSprite('R', x, 0x1d6, 2);
		drawBackground(2);
		present();
	} while (r.result == 0 && count < 0xf && !aborted());
	r.setI(kLossAnim, 0);
}

void RatGame::t0001() {
	slots[0].pos = 0;
	rules.setB(rec(3, kActive), 0);
	bool more = true;
	do {
		frame();
		more = stepSlot(0);
	} while (!resultSet() && more);
	rules.setB(rec(1, kActive), 1);
	if (resultSet())
		return;
	if (rules.b(rec(2, kPressed)) == 1)
		t0002();
	else
		t0003();
}

void RatGame::t0003() {
	rules.setB(rec(25, kAbove), 1);
	rules.setB(rec(2, kActive), 0);
	runSlot(2);
	rules.setB(rec(2, kActive), 1);
	if (rules.result == 0)
		rules.result = kLost;
}

void RatGame::t0002() {
	rules.setB(rec(2, kActive), 0);
	runSlot(1);
	rules.setB(rec(3, kActive), 1);
	rules.setB(rec(2, kActive), 1);
	if (!resultSet())
		t0004();
}

void RatGame::t0004() {
	RatRules &r = rules;
	// The rat waits at the end of T0002 + (10, 18) until record 3 opens.
	const Slot &s1 = slots[1];
	r.setF(kRatX, s1.x[s1.x.size() - 1] + 10.0f);
	r.setF(kRatY, s1.y[s1.y.size() - 1] + 18.0f);
	r.setB(rec(21, kPressed), 1);
	do {
		frame();
	} while (r.b(rec(3, kPressed)) == 0 && !resultSet());
	if (resultSet())
		return;
	r.setB(rec(3, kAbove), 1);
	r.setB(rec(2, kActive), 0);
	slots[0x1c].pos = 0;
	r.setS(rec(12, kFrame), r.s(rec(12, kPressedFrame)));
	bool more = true;
	do {
		frame();
		more = stepSlot(0x1c);
		if (slots[0x1c].pos == 0x1a)
			r.setS(rec(12, kFrame), r.s(rec(12, kRest)));
	} while (!resultSet() && more);
	if (resultSet())
		return;
	slots[3].pos = 0;
	r.setB(rec(2, kActive), 0);
	more = true;
	do {
		frame();
		more = stepSlot(3);
	} while (!resultSet() && more);
	if (resultSet())
		return;
	r.setB(rec(2, kActive), 1);
	if (r.b(rec(4, kPressed)) == 0)
		t0005();
	else
		t0009();
}

void RatGame::t0005() {
	rules.setB(rec(27, kAbove), 1);
	rules.setB(rec(4, kActive), 0);
	runSlot(4);
	rules.setB(rec(4, kActive), 1);
	if (rules.result == 0)
		rules.result = kLost;
}

void RatGame::t0009() {
	rules.setB(rec(4, kActive), 0);
	runSlot(0xa);
	rules.setB(rec(4, kActive), 1);
	if (!resultSet())
		wheel();
}

void RatGame::wheelHold() {
	RatRules &r = rules;
	wheelY = 0;
	const int base = 0xfd;
	do {
		frame();
		const uint16 f = (uint16)(r.s(rec(20, kFrame)) - 0xba);
		if (f >= 1 && f <= 9)
			r.setF(kRatY, (float)(base + f - 1));
		wheelY = chop(r.f(kRatY));
	} while (r.b(rec(9, kPressed)) == 1);
}

void RatGame::wheel() {
	RatRules &r = rules;
	bool held = false;
	r.setB(kRatByte, 0);
	r.setI(kRatX, 0x43b00000);   // 352.0
	r.setI(kRatY, 0x437d0000);   // 253.0
	do {
		frame();
		r.setI(kRatX, 0x43b00000);
		r.setI(kRatY, 0x437d0000);
		if (r.b(rec(9, kPressed)) == 1) {
			wheelHold();
			held = true;
		}
	} while (!resultSet() && !held);
	if (resultSet())
		return;
	playSound(13);
	if (wheelY < 0x100) {
		playSound(10);
		t0006();
	} else if (wheelY < 0x105) {
		playSound(11);
		t0007();
	} else {
		playSound(12);
		t0008();
	}
}

void RatGame::t0006() {
	runSlot(5);
	if (!resultSet())
		wheel();
}

void RatGame::t0007() {
	RatRules &r = rules;
	Slot &s = slots[6];
	s.pos = 0;
	bool bumped = false, more = true;
	do {
		frame();
		if (s.pos > (int)s.b.size() - 0x23 && !bumped) {
			bumped = true;
			r.setS(rec(13, kFrame), r.s(rec(13, kFrame)) + 1);
		}
		more = stepSlot(6);
	} while (!resultSet() && more);
	r.setS(rec(13, kFrame), r.s(rec(13, kFrame)) - 1);
	if (resultSet())
		return;
	if (r.b(rec(8, kPressed)) == 1)
		t0007a();
	else
		t0007b();
}

void RatGame::t0007a() {
	runSlot(7);
	if (!resultSet())
		wheel();
}

void RatGame::t0007b() {
	rules.setB(rec(27, kAbove), 1);
	runSlot(8);
	rules.setS(rec(24, kFrame), rules.s(rec(24, kFrame)) + 1);
	if (rules.result == 0)
		rules.result = kLost;
}

void RatGame::t0008() {
	runSlot(9);
	if (!resultSet())
		ride();
}

void RatGame::ride() {
	RatRules &r = rules;
	const Slot &s = slots[9];
	r.setF(kRatX, s.x[s.x.size() - 1]);
	r.setF(kRatY, s.y[s.y.size() - 1] + 12.0f);
	r.setB(kRatByte, 0);
	int state = 1;
	do {
		if (r.b(0x00496b7f) == 2)
			r.setF(kRatX, (float)(1.0 + r.f(kRatX)));
		else if (r.b(0x00496b7f) == 1)
			r.setF(kRatX, (float)(r.f(kRatX) - 1.0));
		frame();
		// The original compares the float's bits as an int.
		if (r.i(kRatX) < 0x43d78000)
			state = 2;
		else if (r.i(kRatX) > 0x4401c000)
			state = 3;
	} while (!resultSet() && state == 1);
	if (resultSet())
		return;
	if (state == 2) {
		t0013();
		return;
	}
	r.setI(0x00496cc0, 1);
	switch (r.b(0x00496b68)) {
	case 1: t0010(); break;
	case 2: t0011(); break;
	case 3: t0012(); break;
	default: break;
	}
}

void RatGame::t0013() {
	runSlot(0x10);
	if (resultSet())
		return;
	if (rules.b(rec(8, kPressed)) == 1)
		t0014();
	else
		t0015();
}

void RatGame::t0014() {
	runSlot(0x11);
	if (!resultSet())
		wheel();
}

void RatGame::t0015() {
	rules.setB(rec(27, kAbove), 1);
	runSlot(0x12);
	if (rules.result == 0)
		rules.result = kLost;
}

void RatGame::t0010() {
	runSlot(0xb);
	if (!resultSet())
		t0020();
}

void RatGame::t0011() {
	RatRules &r = rules;
	runSlot(0xc);
	if (resultSet())
		return;
	const int32 dir = r.i(0x00496cbc);
	r.setS(rec(18, kFrame), r.s(rec(18, kFrame)) + (int16)dir);
	r.setB(rec(18, kPressed), (byte)(r.b(rec(18, kPressed)) + (byte)dir));
	const byte st = r.b(rec(18, kPressed));
	if (st == 1)
		t0011a();
	else if (st == 3)
		t0011b();
}

void RatGame::t0011a() {
	rules.setB(rec(27, kAbove), 1);
	runSlot(0xd);
	if (rules.result == 0)
		rules.result = kLost;
}

void RatGame::t0011b() {
	rules.setB(rec(27, kAbove), 1);
	runSlot(0xe);
	if (!resultSet())
		t0020();
}

void RatGame::t0012() {
	rules.setB(rec(27, kAbove), 1);
	runSlot(0xf);
	if (rules.result == 0)
		rules.result = kLost;
}

void RatGame::t0020() {
	RatRules &r = rules;
	int n = 0;
	r.setF(kRatX, -100.0f);
	r.setF(kRatY, -100.0f);
	do {
		frame();
		n++;
	} while (!resultSet() && n < 0x32);
	if (resultSet())
		return;
	slots[0x13].pos = 0;
	if (r.b(rec(16, kPressed)) == 1) {
		r.setB(rec(16, kActive), 0);
		r.setB(rec(5, kActive), 0);
	}
	bool more = true;
	do {
		frame();
		more = stepSlot(0x13);
	} while (!resultSet() && more);
	if (resultSet())
		return;
	r.setB(rec(16, kActive), 1);
	r.setB(rec(5, kActive), 1);
	if (r.b(rec(16, kPressed)) == 1)
		t00025();
	else
		t0021();
}

void RatGame::t0099() {
	slots[0x19].pos = 0;
	bool more = true;
	do {
		frame();
		more = stepSlot(0x19);
		if (slots[0x19].pos == 0x1a)
			playSound(15);
	} while (!resultSet() && more);
	rules.result = kWin;   // even when the clock or an edge click set it
}

void RatGame::t00025() {
	slots[0x1a].pos = 0;
	bool more = true, jump = false;
	do {
		frame();
		more = stepSlot(0x1a);
		if (rules.b(rec(6, kPressed)) == 1 && nearJump()) {
			jump = true;
			more = false;
		}
	} while (!resultSet() && more);
	if (resultSet())
		return;
	if (jump)
		t0099();
	else
		rules.result = kLost;
}

void RatGame::t0021() {
	RatRules &r = rules;
	slots[0x14].pos = 0;
	bool more = true, toT0023 = false;
	do {
		frame();
		more = stepSlot(0x14);
		if (r.b(rec(16, kPressed)) == 1) {
			toT0023 = true;
			more = false;
		}
	} while (!resultSet() && more && r.b(rec(16, kPressed)) == 0);
	if (resultSet())
		return;
	if (toT0023) {
		t0023();
		return;
	}
	slots[0x17].pos = 0;
	more = true;
	do {
		frame();
		more = stepSlot(0x17);
		if (slots[0x17].pos == 0x1a)
			playSound(15);
	} while (!resultSet() && more && r.b(rec(16, kPressed)) == 0);
	if (resultSet())
		return;
	if (r.b(rec(16, kPressed)) == 0)
		t0025();
	else
		t0022();
}

// T0023 starts at the first point whose x is not above the rat's.
void RatGame::t0023() {
	RatRules &r = rules;
	r.setB(rec(26, kAbove), 1);
	r.setB(rec(23, kAbove), 1);
	Slot &s = slots[0x16];
	s.pos = 0;
	while (s.x[s.pos] > r.f(kRatX))
		s.pos++;
	r.setF(kRatX, s.x[s.pos]);
	r.setF(kRatY, s.y[s.pos]);
	bool more = true, jump = false;
	do {
		frame();
		more = stepSlot(0x16);
		if (r.b(rec(6, kPressed)) == 1 && nearJump()) {
			jump = true;
			more = false;
		}
	} while (!resultSet() && more);
	if (resultSet())
		return;
	if (jump)
		t0099();
	else
		r.result = kLost;
}

void RatGame::t0025() {
	runSlot(0x18);
	rules.result = kLost;
}

void RatGame::t0022() {
	RatRules &r = rules;
	slots[0x15].pos = 0;
	r.setB(rec(26, kAbove), 1);
	r.setB(rec(23, kAbove), 1);
	bool more = true, jump = false;
	do {
		frame();
		more = stepSlot(0x15);
		if (r.b(rec(6, kPressed)) == 1 && nearJump()) {
			jump = true;
			more = false;
		}
	} while (!resultSet() && more);
	if (resultSet())
		return;
	if (jump)
		t0099();
	else
		r.result = kLost;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

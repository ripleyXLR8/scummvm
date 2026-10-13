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

#ifndef CRYOMNI3D_ATLANTIS_SNAKES_RULES_H
#define CRYOMNI3D_ATLANTIS_SNAKES_RULES_H

#include "common/array.h"
#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The rules of puzzle 8 (StartPuzzle=8, CHAPI034, atlantis.exe FUN_0043b0f4):
// four pieces put in eight holes linked in pairs; snake k starts from the
// partner of piece k's hole and is steered through the maze of
// PUZZLES\CHEMIN00.PCX (0 wall, 1 corridor, 6 junction) back to its piece.
// Paths never share a junction.  Direction codes: 0 up, 1 left, 2 right,
// 3 down; a click direction d (FUN_0043aad0's argument): 0 right, 1 left,
// 2 up, 3 down.
class SnakesRules {
public:
	enum { kW = 640, kH = 480, kNumHoles = 8, kNumSnakes = 4, kMaxSegments = 0xc8 };
	enum Sound { kSoundNone, kSoundPick, kSoundPlace, kSoundMove, kSoundWin };

	struct Hole { byte used; int32 x, y; byte partner, dir; };
	struct Piece { byte placed; int32 x, y; };
	struct Snake {
		int32 active;
		int32 startX, startY, x, y;
		byte len;
		byte dirs[kMaxSegments];
		byte piece;
	};
	struct Draw { int x, y, frame; };

	Common::Array<byte> map;   // 0x007beab8, 640 x 480
	Hole holes[kNumHoles];     // 0x004974c4
	Piece pieces[kNumSnakes];  // 0x00497894
	Snake snakes[kNumSnakes];  // 0x0049751c
	int carried;               // 0x007bebf8: piece + 1, 0 for none
	int result;                // 0x004974c0

	SnakesRules() : carried(0), result(0) {
		memset(snakes, 0, sizeof(snakes));
		setup();
	}

	// FUN_0043a53c: a 640 x 480 PCX, the 128-byte header skipped, 0xC0 runs.
	bool decodeMap(const byte *pcx, uint32 size) {
		map.resize(kW * kH);
		uint32 pos = 0x80, out = 0;
		while (out < (uint32)kW * kH) {
			if (pos >= size)
				return false;
			const byte b = pcx[pos];
			if ((b & 0xc0) == 0xc0) {
				const uint n = b & 0x3f;
				if (++pos >= size)
					return false;
				for (uint i = 0; i < n && out < (uint32)kW * kH; i++)
					map[out++] = pcx[pos];
			} else {
				map[out++] = b;
			}
			pos++;
		}
		return true;
	}

	// The entry's set-up.  It leaves the snakes' places and piece indexes
	// as the last run left them (0 on a first run).
	void setup() {
		static const Hole kHoles[kNumHoles] = {
			{ 0, 175, 205, 1, 2 }, { 0, 455, 355, 0, 1 }, { 0, 225, 65, 3, 2 },
			{ 0, 455, 215, 2, 1 }, { 0, 295, 165, 5, 3 }, { 0, 365, 295, 4, 1 },
			{ 0, 175, 325, 7, 0 }, { 0, 425, 75, 6, 3 }
		};
		for (int h = 0; h < kNumHoles; h++)
			holes[h] = kHoles[h];
		for (int k = 0; k < kNumSnakes; k++) {
			snakes[k].active = 0;
			snakes[k].len = 0;
			pieces[k].placed = 0;
			pieces[k].x = 0x28 + 0x14 * k;
			pieces[k].y = 0x1c0;
		}
		carried = 0;
		result = 0;
	}

	bool allPlaced() const {
		for (int k = 0; k < kNumSnakes; k++)
			if (!pieces[k].placed)
				return false;
		return true;
	}

	byte at(int x, int y) const {
		const int i = y * kW + x;
		return (i >= 0 && i < (int)map.size()) ? map[i] : 0;
	}

	// The direction code's segment byte at index i (dirs[i - 1]), with the
	// exe's index 0 being the length byte itself.
	byte segment(const Snake &s, int i) const { return i == 0 ? s.len : s.dirs[i - 1]; }

	static void step(int code, int &dx, int &dy) {
		dx = (code == 2) - (code == 1);
		dy = (code == 3) - (code == 0);
	}

	// Walks from (x, y) along code to the first non-corridor cell.
	void walk(int code, int &x, int &y) const {
		int dx, dy;
		step(code, dx, dy);
		do {
			x += dx;
			y += dy;
		} while (at(x, y) == 1);
	}

	static int codeOf(int d) {
		static const int kCode[4] = { 2, 1, 0, 3 };
		return kCode[d];
	}

	// FUN_0043a8e8: is (x, y) a node of some snake's path?
	bool onAPath(int x, int y) const {
		for (int k = 0; k < kNumSnakes; k++) {
			int cx = snakes[k].startX, cy = snakes[k].startY;
			for (int i = 0; i < snakes[k].len; i++) {
				walk(snakes[k].dirs[i], cx, cy);
				if (cx == x && cy == y)
					return true;
			}
		}
		return false;
	}

	// FUN_0043aad0.
	Sound move(int k, int d) {
		Snake &s = snakes[k];
		const int code = codeOf(d);
		static const int kReverse[4] = { 1, 2, 3, 0 };   // the code d undoes
		if (s.len > 1) {
			if (segment(s, s.len) == kReverse[d]) {
				s.len--;
				walk(code, s.x, s.y);
				return kSoundMove;
			}
		}
		if (s.len == 1 && segment(s, 1) == kReverse[d])
			return kSoundNone;
		int dx, dy;
		step(code, dx, dy);
		if (at(s.x + dx, s.y + dy) != 1)                  // FUN_0043a788
			return kSoundNone;
		int ex = s.x, ey = s.y;
		walk(code, ex, ey);                                // FUN_0043a820
		if (onAPath(ex, ey))
			return kSoundNone;
		const Sound snd = s.len > 0 ? kSoundMove : kSoundNone;
		s.x = ex;
		s.y = ey;
		if (s.len < kMaxSegments)
			s.dirs[s.len] = (byte)code;
		s.len++;
		return snd;
	}

	// FUN_0043a748: a click near the left or right edge leaves
	// (DAT_00496d0c); the click is still processed.
	static bool atEdge(int x) { return x < 0x1e || x > 0x26c; }

	// FUN_0043c35c: phase 1.
	Sound place(int x, int y) {
		if (atEdge(x))
			result = 1;
		if (carried == 0) {
			for (int k = 0; k < kNumSnakes; k++) {
				const Piece &p = pieces[k];
				if (p.x - 10 < x && x < p.x + 10 && p.y - 10 < y && y < p.y + 10 && !p.placed) {
					carried = k + 1;
					return kSoundPick;
				}
			}
			return kSoundNone;
		}
		for (int h = 0; h < kNumHoles; h++) {
			const Hole &hole = holes[h];
			if (!(hole.x - 10 < x && x < hole.x + 10 && hole.y - 10 < y && y < hole.y + 10 && !hole.used))
				continue;
			const int k = carried - 1;
			const Hole &other = holes[hole.partner];
			pieces[k].placed = 1;
			pieces[k].x = hole.x;
			pieces[k].y = hole.y;
			holes[h].used = 1;
			holes[hole.partner].used = 1;
			Snake &s = snakes[k];
			s.active = 1;
			s.x = s.startX = other.x;
			s.y = s.startY = other.y;
			s.dirs[0] = other.dir;
			s.piece = (byte)k;
			static const int kFirstMove[4] = { 2, 1, 0, 3 };
			move(k, kFirstMove[s.dirs[0] & 3]);
			carried = 0;
			return kSoundPlace;
		}
		return kSoundNone;
	}

	// FUN_0043b040: the nearest head, by the exe's float distance.
	int nearest(int x, int y) const {
		float best = 2000.0f;
		int k = 0;
		for (int i = 0; i < kNumSnakes; i++) {
			const float dy = (float)(y - snakes[i].y), dx = (float)(x - snakes[i].x);
			const float dist = (float)sqrt((double)(dy * dy + dx * dx));
			if (dist < best) {
				k = i;
				best = dist;
			}
		}
		return k;
	}

	// FUN_0043af08: the click direction from the head (0 offsets count as 1;
	// the exe compares the float dx / dy with +-1.0).
	static int direction(int dx, int dy) {
		if (dy == 0)
			dy = 1;
		if (dx == 0)
			dx = 1;
		if (dx < 0 && dy < 0)
			return ABS(dx) < ABS(dy) ? 2 : 1;
		if (dx < 0)
			return ABS(dx) < ABS(dy) ? 3 : 1;
		if (dy < 0)
			return ABS(dx) < ABS(dy) ? 2 : 0;
		return ABS(dx) > ABS(dy) ? 0 : 3;
	}

	// FUN_0043c6bc: phase 2.
	Sound steer(int x, int y) {
		if (atEdge(x))
			result = 1;
		const int k = nearest(x, y);
		const int d = direction(x - snakes[k].x, y - snakes[k].y);
		const Snake &s = snakes[k];
		if (at(s.x, s.y) == 6)
			return move(k, d);
		static const int kBack[4] = { 3, 0, 1, 2 };     // last code -> the d that undoes it
		const byte last = segment(s, s.len);
		if (last <= 3 && d == kBack[last])
			return move(k, d);
		return kSoundNone;
	}

	// FUN_0043bebc's state part: the carried piece follows the mouse, then
	// the win test (every head on its piece).
	bool frame(int mouseX, int mouseY) {
		if (carried) {
			pieces[carried - 1].x = mouseX;
			pieces[carried - 1].y = mouseY;
		}
		for (int k = 0; k < kNumSnakes; k++) {
			const Piece &p = pieces[snakes[k].piece & 3];
			if (p.x != snakes[k].x || p.y != snakes[k].y)
				return false;
		}
		result = 255;                     // DAT_00496d14
		return true;
	}

	// FUN_0043bebc's sprite draws on serpnew.spr: the active snakes
	// (FUN_0043b49c), the pieces, the carried piece again.
	void draws(Common::Array<Draw> &out) const {
		for (int k = 0; k < kNumSnakes; k++)
			if (snakes[k].active)
				drawSnake(k, out);
		for (int k = 0; k < kNumSnakes; k++)
			add(out, pieces[k].x, pieces[k].y, k * 15);
		if (carried)
			add(out, pieces[carried - 1].x, pieces[carried - 1].y, (carried - 1) * 15);
	}

	// FUN_0043c114's draws for one step: the pieces when shown, then the
	// active snakes.
	void drawsWin(bool piecesShown, Common::Array<Draw> &out) const {
		if (piecesShown)
			for (int k = 0; k < kNumSnakes; k++)
				add(out, pieces[k].x, pieces[k].y, k * 15);
		for (int k = 0; k < kNumSnakes; k++)
			if (snakes[k].active)
				drawSnake(k, out);
	}

private:
	static void add(Common::Array<Draw> &out, int x, int y, int frame) {
		Draw d = { (int16)x, (int16)y, frame };
		out.push_back(d);
	}

	// FUN_0043b49c: bodies every 10 pixels, then the nodes (corners from the
	// previous code), the tail at the start and the head.
	void drawSnake(int k, Common::Array<Draw> &out) const {
		const Snake &s = snakes[k];
		if (s.len == 0)
			return;
		const int f = k * 15;
		int x = s.startX, y = s.startY;
		for (int i = 0; i < s.len; i++) {
			const int code = s.dirs[i];
			if (code > 3)
				continue;
			int ex = x, ey = y;
			walk(code, ex, ey);
			int dx, dy;
			step(code, dx, dy);
			const bool vertical = (code == 0 || code == 3);
			int n = (ABS(vertical ? ey - y : ex - x) - 10) / 10;
			x += 10 * dx;
			y += 10 * dy;
			if (i == 0) {
				x += 10 * dx;
				y += 10 * dy;
				n--;
			}
			if (s.len - 1 == i)
				n--;
			for (; n > 0; n--) {
				add(out, x, y, f + (vertical ? 1 : 2));
				x += 10 * dx;
				y += 10 * dy;
			}
			x = ex;
			y = ey;
		}
		x = s.startX;
		y = s.startY;
		int prev = -1;
		for (int i = 0; i < s.len; i++) {
			const int code = s.dirs[i];
			if (code > 3)
				continue;
			int frame = -1;
			switch (code) {
			case 0: frame = prev == 1 ? 9 : prev == 2 ? 10 : prev != -1 ? 1 : -1; break;
			case 3: frame = prev == 1 ? 7 : prev == 2 ? 8 : prev != -1 ? 1 : -1; break;
			case 1: frame = prev == 0 ? 8 : prev == 3 ? 10 : prev != -1 ? 2 : -1; break;
			case 2: frame = prev == 0 ? 7 : prev == 3 ? 9 : prev != -1 ? 2 : -1; break;
			default: break;
			}
			if (frame >= 0)
				add(out, x, y, f + frame);
			walk(code, x, y);
			prev = code;
		}
		static const int kTail[4] = { 13, 12, 11, 14 };
		if (s.dirs[0] <= 3)
			add(out, s.startX, s.startY, f + kTail[s.dirs[0]]);
		const byte last = s.dirs[s.len - 1];
		switch (last) {
		case 0: add(out, s.x, s.y + 5, f + 3); break;
		case 1: add(out, s.x + 5, s.y, f + 5); break;
		case 2: add(out, s.x - 5, s.y, f + 6); break;
		case 3: add(out, s.x, s.y - 5, f + 4); break;
		default: break;
		}
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

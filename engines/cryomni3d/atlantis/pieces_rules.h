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

#ifndef CRYOMNI3D_ATLANTIS_PIECES_RULES_H
#define CRYOMNI3D_ATLANTIS_PIECES_RULES_H

#include "common/array.h"
#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The rules of puzzle 3 (StartPuzzle=3, CHAPI029, atlantis.exe FUN_004459c0):
// six pieces in a ring, each swapped once with a neighbour, back to their
// places.  The click map is PUZZLES\PUZSRP2.BMP read raw (FUN_004472cc).
class RotatingPiecesRules {
public:
	enum { kNumPieces = 6 };
	enum Click { kClickNone, kClickLeave, kClickReset, kClickMove, kClickBoard };
	enum Cursor { kCursorAnim, kCursorExit, kCursorNormal, kCursorPiece };

	int16 pieces[kNumPieces];    // 0x00496eba
	int16 counters[kNumPieces];  // 0x00496ec6: moves of each piece
	// The shared puzzle-cursor animation (0x00496ea8 mode, 0x00496eaa frame,
	// 0x00496eac step), all 0 in the exe's data.
	int16 cursorMode, cursorFrame, cursorStep;

	RotatingPiecesRules() : cursorMode(0), cursorFrame(0), cursorStep(0) { reset(); }

	// FUN_00445ba0: the fixed start order at 0x00496eae, counters cleared.
	void reset() {
		static const int16 kStart[kNumPieces] = { 3, 1, 0, 4, 5, 2 };
		for (int i = 0; i < kNumPieces; i++) {
			counters[i] = 0;
			pieces[i] = kStart[i];
		}
	}

	// A byte of the raw BMP at [x - y * 640 + base] (0x4b438 for clicks,
	// 0x4b1b8 for the cursor), 0 off the file.
	static byte mapValue(const Common::Array<byte> &map, int x, int y, int base) {
		const int idx = x - y * 640 + base;
		return (idx >= 0 && idx < (int)map.size()) ? map[idx] : 0;
	}

	static bool onBoard(int x, int y) {
		return x >= 0x32 && x <= 0x24e && y >= 0x1e && y <= 0x1c2;
	}

	// FUN_004460c8.  kClickMove and kClickReset play a sound (serp_2_1,
	// serp_2_2), kClickBoard serp_2_3 (a click on the board off the pieces,
	// or on a piece that has already moved); kClickLeave ends the puzzle (2).
	// Values above 13 index past the exe's arrays; the shipped map has none.
	Click click(const Common::Array<byte> &map, int x, int y) {
		const byte v = mapValue(map, x, y, 0x4b438);
		if (v == 0) {
			if (!onBoard(x, y))
				return kClickLeave;
			return startCursorAnim();
		}
		if (v == 13) {
			reset();
			return kClickReset;
		}
		const int code = v - 1;
		if (code >= 2 * kNumPieces)
			return kClickNone;
		const int i = code >> 1;
		// A piece that has already moved: the cursor animation and its
		// sound (0x0044619c -> 0x00446239).
		if (counters[pieces[i]] != 0)
			return startCursorAnim();
		counters[pieces[i]]++;
		const int j = (code % 2 == 0) ? (i + 5) % kNumPieces : (i + 1) % kNumPieces;
		const int16 t = pieces[i];
		pieces[i] = pieces[j];
		pieces[j] = t;
		return kClickMove;
	}

	// FUN_004462b0: start the cursor animation (and play serp_2_3) unless
	// it is at its first frame.
	Click startCursorAnim() {
		if (cursorFrame == 2)
			return kClickNone;
		cursorFrame = 2;
		cursorMode = 2;
		cursorStep = 1;
		return kClickBoard;
	}

	// The win test of FUN_00445c00.
	bool solved() const {
		for (int i = 0; i < kNumPieces; i++)
			if (pieces[i] != i)
				return false;
		return true;
	}

	// FUN_00445e7c: which cursor to draw at (x, y) and its frame.  The
	// animation (FUN_00445d70) draws CURSEURS.SPR frame cursorFrame, then
	// steps 2 -> 6 -> 0 and ends; kCursorExit is CURSEURS.SPR frame 11,
	// kCursorNormal frame 6 (FUN_0042d558), kCursorPiece a puzsrp2c.spr frame
	// (13 over a piece that has moved).
	Cursor cursor(const Common::Array<byte> &map, int x, int y, int &frame) {
		if (cursorMode == 2) {
			frame = cursorFrame;
			cursorFrame += cursorStep;
			if (cursorFrame == 6)
				cursorStep = -1;
			if (cursorFrame < 0)
				cursorMode = 0;
			return kCursorAnim;
		}
		const byte v = mapValue(map, x, y, 0x4b1b8);
		if (v == 0) {
			frame = onBoard(x, y) ? 6 : 11;
			return onBoard(x, y) ? kCursorNormal : kCursorExit;
		}
		frame = v - 1;
		if (frame < 2 * kNumPieces && counters[pieces[frame >> 1]] != 0)
			frame = 13;
		return kCursorPiece;
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

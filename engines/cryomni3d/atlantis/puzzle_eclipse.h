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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_ECLIPSE_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_ECLIPSE_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/puzzle_assets.h"

namespace Graphics {
struct Surface;
class ManagedSurface;
}

namespace CryOmni3D {
namespace Atlantis {

// Initial record table: .data 0x0049745e, 7 records of 14 bytes (dir, x, y,
// min, max, curr, pad).  Records 0 and 1 have dir 1 and cycle on their own.
struct EclipseRecordInit {
	int dir, x, y, min, max;
};
static const EclipseRecordInit kEclipseRecordInit[7] = {
	{ 1, 215, 308,   0,   7 },   // 0  PZ2L 0..7    -- auto-cycling
	{ 1, 298, 308,   8,  15 },   // 1  PZ2L 8..15   -- auto-cycling
	{ 0, 227, 384,  16,  86 },   // 2  PZ2L 16..86  -- lever 1 mechanism
	{ 0, 443, 373,  87, 157 },   // 3  PZ2L 87..157 -- lever 2 mechanism
	{ 0, 319, 204,   0,  70 },   // 4  PZ1L 0..70   -- SUN sphere    (lever 2)
	{ 0, 236, 222,  71, 141 },   // 5  PZ1L 71..141 -- MOON sphere   (lever 1)
	{ 0, 271, 198, 142, 212 },   // 6  PZ1L 142..212 -- EARTH sphere (lever 1)
};

class CryOmni3DEngine_Atlantis;

// StartPuzzle=0: celestial-alignment minigame at CHAPI012 vue 221.
// Reverse-engineered from atlantis.exe (puzzle dispatch case 0,
// FUN_0044154c, called from FUN_0043a1e0).
class EclipsePuzzle {
public:
	explicit EclipsePuzzle(CryOmni3DEngine_Atlantis *engine);
	~EclipsePuzzle();

	// Returns 255 on win, 1 on abort (mirrors DAT_00496d14 / 00496d0c
	// at the exe's two return-code sites in FUN_0044154c).
	int run();

private:
	CryOmni3DEngine_Atlantis *_engine;

	struct Record {
		int dir;        // 0 idle / 1 step-up / 2 step-down (transient on click)
		int x, y;
		int min, max;
		int curr;
	};
	enum { kNumRecords = 7 };
	Record _records[kNumRecords];

	// Frame counter -- _DAT_007bebf0 in the exe; drives the alternate-
	// frame test on the win highlight (`(_frameCounter & 2) != 0`).
	uint32 _frameCounter;

	Common::Array<PuzzleSpr> _pz1lFrames;  // _DAT_007beae0
	Common::Array<PuzzleSpr> _pz2lFrames;  // _DAT_007beadc
	Common::Array<uint16>    _bgPixels;    // PZLION.TGA
	bool _bgLoaded;

	void tick(int recIdx);
	void renderFrame(Graphics::ManagedSurface &out);
	bool isWin() const;

	// Play one of the eclipse-puzzle APC sounds (synchronous setup,
	// asynchronous playback).  `name` is "LION1" / "LION2" / "LION3".
	// `wait` blocks until the sound finishes (used for the win sound).
	void playSound(const char *name, bool wait);
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif

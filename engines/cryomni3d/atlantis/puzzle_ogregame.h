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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_OGREGAME_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_OGREGAME_H

#include "common/array.h"
#include "common/random.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/puzzle_assets.h"

namespace Graphics {
struct Surface;
class ManagedSurface;
}

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// StartPuzzle=2: the rat-hunter's "ogre & hero" board pursuit game at
// place 290 (CHAPI018).  Reverse-engineered from atlantis.exe puzzle
// dispatch case 2 (FUN_0044203c and its helpers, see the .cpp).
//
// The player drags the blue hero piece 1-2 squares per turn across the
// 7x7 map grid, hunting an INVISIBLE wandering queen while two ogres
// each step one square toward him per turn.  Moving along a path that
// crosses the queen's cell catches her (win); an ogre reaching the
// hero's cell loses; the queen blinks visible briefly every fourth
// move so she can be tracked.
class OgreGamePuzzle {
public:
	explicit OgreGamePuzzle(CryOmni3DEngine_Atlantis *engine);
	~OgreGamePuzzle();

	// Returns 255 on win, 2 on loss, 1 on abort (DAT_00496d14 /
	// DAT_00496d10 / DAT_00496d0c, read from the exe's data section).
	int run();

private:
	CryOmni3DEngine_Atlantis *_engine;

	// Entity records, mirroring the 3-short [activeFlag, x, y] table at
	// DAT_00495e4c: 0 = hero, 1 = queen, 2..3 = ogres.
	struct Piece {
		int flag;
		int x, y;
	};
	enum { kNumPieces = 4 };
	Piece _pieces[kNumPieces];

	// Per-cell occupancy (0x7738b8): the ogres mark/clear their cells so
	// neither steps onto the other.  The hero and the (intangible) queen
	// are not in it -- an ogre stepping onto the hero IS the loss.
	byte _occupied[7 * 7];

	// Legal-destination mask (0x773938) and the three path cells
	// (0x773870/78/80: origin, elbow, destination) of the hovered or
	// last-executed move.
	byte _moveMask[7 * 7];
	int  _pathX[3], _pathY[3];

	int  _moveCounter;          // _DAT_007beaa8: starts at 2, blink at 4
	bool _holding;              // _DAT_007bebf8: hero piece on the cursor

	Common::Array<PuzzleSpr> _pieceFrames;  // PUZCYCLO.SPR
	Common::Array<uint16>    _bgPixels;     // PUZCYCLO.TGA
	bool _bgLoaded;

	Common::RandomSource _rng;

	void placeQueen();
	void buildMoveMask(int mouseX, int mouseY);
	void moveOgres(Graphics::ManagedSurface &working, int &result);
	void walkQueen();
	void blinkQueen(Graphics::ManagedSurface &working);
	void renderFrame(Graphics::ManagedSurface &out, int mouseX, int mouseY);
	void presentFrame(Graphics::ManagedSurface &working);
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif

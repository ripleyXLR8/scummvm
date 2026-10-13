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

#include "cryomni3d/atlantis/ogre_draw.h"
#include "cryomni3d/atlantis/ogre_rules.h"
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

	// Pieces, occupancy, path mask and path cells (the exe's globals).
	OgreRules _rules;

	int  _moveCounter;          // _DAT_007beaa8: starts at 2, blink at 4
	bool _holding;              // _DAT_007bebf8: hero piece on the cursor

	Common::Array<PuzzleSpr> _pieceFrames;  // PUZCYCLO.SPR
	Common::Array<uint16>    _bgPixels;     // PUZCYCLO.TGA
	bool _bgLoaded;

	Common::RandomSource _rng;

	void placeQueen();
	void moveOgres(Graphics::ManagedSurface &working, int &result);
	void walkQueen();
	void play(Graphics::ManagedSurface &working, const OgreDraw::Events &ev);
	void renderBoard(Graphics::ManagedSurface &working);
	void presentFrame(Graphics::ManagedSurface &working);
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif

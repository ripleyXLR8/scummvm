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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_RAT_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_RAT_H

#include "common/array.h"
#include "common/random.h"
#include "common/scummsys.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/atlantis/puzzle_assets.h"
#include "cryomni3d/atlantis/rat_game.h"

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// StartPuzzle=4 at CHAPI041: the rat's run (atlantis.exe FUN_00438bf0).
// RatGame runs the original's code; this class gives it the screen, the
// mouse, the sounds and the 200 Hz timer.
class RatPuzzle : public RatGame {
public:
	explicit RatPuzzle(CryOmni3DEngine_Atlantis *engine);

	// 255 when the rat jumps, 2 when the rats or the clock run out, 1 when
	// the player leaves or an asset is missing.
	int play();

protected:
	void readInput(int &mx, int &my, bool &click, bool &held) override;
	void drawBackground(int which) override;
	void drawSprite(char bank, int x, int y, int frame) override;
	void present() override;
	void playSound(int slot) override;
	int randomValue() override;
	uint32 ticks() override;
	void waitUntil(uint32 t) override;
	bool aborted() const override;

private:
	CryOmni3DEngine_Atlantis *_engine;
	Common::Array<uint16>    _bgPixels;   // PZFBACK.TGA
	Common::Array<PuzzleSpr> _pieces;     // PZFPIECE.SPR (0x007beae0)
	Common::Array<PuzzleSpr> _rat;        // PZFRAT.SPR (0x007bead4)
	Graphics::ManagedSurface _working;
	bool _loaded;
	bool _prevButton;
	int _mouseX, _mouseY;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

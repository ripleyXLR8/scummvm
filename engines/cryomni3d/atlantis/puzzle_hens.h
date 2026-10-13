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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_HENS_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_HENS_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/hens_rules.h"
#include "cryomni3d/atlantis/puzzle_assets.h"

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// StartPuzzle=1 at CHAPI034: the hens on the levers (atlantis.exe FUN_004441e8).
class HensPuzzle : private HensRules::Animator {
public:
	explicit HensPuzzle(CryOmni3DEngine_Atlantis *engine);
	~HensPuzzle() override;

	// 255 when solved, 2 when the player leaves (DAT_00496d14 / 00496d10),
	// 1 when an asset is missing (DAT_00496d0c).
	int run();

private:
	CryOmni3DEngine_Atlantis *_engine;
	HensRules _rules;
	Common::Array<uint16>    _bgPixels;   // PUZPOUL1.TGA
	Common::Array<PuzzleSpr> _spr0;       // PUZPOUL0.SPR (0x007beae0): hens at home, marks
	Common::Array<PuzzleSpr> _spr1;       // PUZPOUL1.SPR (0x007beadc): hens on the levers
	Common::Array<PuzzleSpr> _spr2;       // PUZPOUL2.SPR (0x007bead8): balance, counter
	Graphics::ManagedSurface *_working;
	bool _loaded;

	void drawFrame(int a, int b, int c);
	void drawCursor(int mouseX, int mouseY);
	void present();
	void frame(int a, int b, int c) override;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

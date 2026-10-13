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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_SNAKES_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_SNAKES_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/puzzle_assets.h"
#include "cryomni3d/atlantis/snakes_rules.h"

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// StartPuzzle=8 at CHAPI034: four snakes in a maze (atlantis.exe FUN_0043b0f4).
class SnakesPuzzle {
public:
	explicit SnakesPuzzle(CryOmni3DEngine_Atlantis *engine);

	// 255 when solved, 1 when the player leaves (DAT_00496d14 / 00496d0c),
	// 1 when an asset is missing.
	int run();

private:
	CryOmni3DEngine_Atlantis *_engine;
	SnakesRules _rules;
	Common::Array<uint16>    _bgPixels;   // LABBASE4.TGA
	Common::Array<PuzzleSpr> _frames;     // SERPNEW.SPR (0x007beae0)
	bool _loaded;

	void render(Graphics::ManagedSurface &out);
	void drawCursor(Graphics::ManagedSurface &out, int mouseX, int mouseY);
	void winAnimation(Graphics::ManagedSurface &out);
	void playSound(SnakesRules::Sound s);
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_MURIA_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_MURIA_H

#include "common/array.h"
#include "common/scummsys.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/atlantis/muria_game.h"
#include "cryomni3d/atlantis/puzzle_assets.h"

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// StartPuzzle=16 at CHAPI069 and 070: Muria (atlantis.exe FUN_0043ee54).
class MuriaPuzzle : public MuriaGame {
public:
	explicit MuriaPuzzle(CryOmni3DEngine_Atlantis *engine);

	// 255 won, 2 left, 1 when an asset is missing.
	int play();

protected:
	void startInput(int &mx, int &my, bool &clicked) override;
	void boundary(int &mx, int &my, bool &clicked) override;
	void loadPicture(const char *name) override;
	void drawBackground() override;
	void drawSprite(char bank, int x, int y, int frame) override;
	void present() override;
	void playSound(int slot) override;
	void scriptHook() override;
	bool aborted() const override;

private:
	CryOmni3DEngine_Atlantis *_engine;
	Common::Array<uint16>    _bgPixels;   // PUZMURIA.TGA
	Common::Array<PuzzleSpr> _frames;     // PUZMURIA.SPR
	Graphics::ManagedSurface _working;
	bool _loaded;
	bool _prevButton;
	bool _bgDone;   // the first background call of a pass restores it

	void readInput(int &mx, int &my, bool &clicked);
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

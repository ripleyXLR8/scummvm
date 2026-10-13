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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_META_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_META_H

#include "common/array.h"
#include "common/scummsys.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/atlantis/puzzle_assets.h"
#include "cryomni3d/atlantis/meta_game.h"

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// StartPuzzle=9..12 at CHAPI046: the metal pieces (atlantis.exe FUN_0044763c
// with variant 1..4).
class MetaPuzzle : public MetaGame {
public:
	MetaPuzzle(CryOmni3DEngine_Atlantis *engine, int stage);
	~MetaPuzzle() override;

	// 255 when solved, 2 when the player leaves, 1 when an asset is missing.
	int play();

protected:
	void startInput(int &mx, int &my, bool &click, bool &right) override;
	void boundary(int &mx, int &my, bool &click, bool &right) override;
	void loadPicture(int n) override;
	void reloadMap() override;
	int currentPlace() override;
	void drawStandardCursor(int mx, int my) override;
	void drawBackground() override;
	void drawSprite(char bank, int x, int y, int frame) override;
	void present() override;
	void playSound(int slot) override;
	bool aborted() const override;

private:
	CryOmni3DEngine_Atlantis *_engine;
	Common::Array<uint16>    _bgPixels;   // PZMETAn.TGA
	Common::Array<PuzzleSpr> _frames;     // PZMETAP1.SPR (0x007beae0)
	Graphics::ManagedSurface _working;
	bool _loaded;
	bool _prevButton;
	bool _bgDone;   // the first background call of a pass restores it
	int _variant;

	Common::Array<byte> _mapFile;   // PZMETAM.BMP from +0x436
	bool _prevRight;

	void readInput(int &mx, int &my, bool &click, bool &right);
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

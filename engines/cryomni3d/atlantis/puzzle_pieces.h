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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_PIECES_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_PIECES_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/pieces_rules.h"
#include "cryomni3d/atlantis/puzzle_assets.h"

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// StartPuzzle=3 at CHAPI029: six rotating pieces (atlantis.exe FUN_004459c0).
class RotatingPiecesPuzzle {
public:
	explicit RotatingPiecesPuzzle(CryOmni3DEngine_Atlantis *engine);

	// 255 when solved, 2 when the player leaves (DAT_00496d14 / 00496d10),
	// 1 when an asset is missing (DAT_00496d0c).
	int run();

private:
	CryOmni3DEngine_Atlantis *_engine;
	RotatingPiecesRules _rules;
	Common::Array<uint16>    _bgPixels;      // PUZSRP2.TGA
	Common::Array<PuzzleSpr> _pieceFrames;   // PUZSRP2.SPR
	Common::Array<PuzzleSpr> _cursorFrames;  // PUZSRP2C.SPR
	Common::Array<byte>      _hitMap;        // PUZSRP2.BMP, raw
	bool _loaded;

	void render(Graphics::ManagedSurface &out, int mouseX, int mouseY);
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

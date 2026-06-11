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

#ifndef CRYOMNI3D_ATLANTIS_PUZZLE_ASSETS_H
#define CRYOMNI3D_ATLANTIS_PUZZLE_ASSETS_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Graphics {
struct Surface;
class ManagedSurface;
}

namespace CryOmni3D {
namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// Decoded single SPR frame -- RGB565 pixels + per-pixel blend factor.
struct PuzzleSpr {
	uint16 w, h;
	int16  xoff, yoff;
	Common::Array<uint16> pixels;
	Common::Array<uint8>  blend;
	PuzzleSpr() : w(0), h(0), xoff(0), yoff(0) {}
};

// Shared PUZZLES\ asset loaders used by the minigames (eclipse, ogre-game …).
//
// loadPuzzleSpr  — decode an RLE SPR frame set (flags / RGB555 colour table /
//                  per-row btype 0=skip/1=raw/2=blend) into PuzzleSpr frames.
// loadPuzzleTGA  — decode a 640x480 15-bit uncompressed TGA into an RGB565
//                  pixel array (bottom-up, as stored).
// blitFrame      — composite one PuzzleSpr onto an RGB565 surface, centred on
//                  (cx,cy) minus the frame's stored offset, honouring blend.
bool loadPuzzleSpr(CryOmni3DEngine_Atlantis *engine, const char *fname,
                   Common::Array<PuzzleSpr> &out);
bool loadPuzzleTGA(CryOmni3DEngine_Atlantis *engine, const char *fname,
                   Common::Array<uint16> &outPixels);
void blitFrame(Graphics::ManagedSurface &out, const PuzzleSpr &spr, int cx, int cy);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif

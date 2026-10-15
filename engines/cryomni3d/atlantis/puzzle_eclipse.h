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

#include "audio/mixer.h"

#include "cryomni3d/atlantis/eclipse_rules.h"
#include "cryomni3d/atlantis/puzzle_assets.h"

namespace Graphics {
struct Surface;
class ManagedSurface;
}

namespace CryOmni3D {
namespace Atlantis {

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

	EclipseRules _rules;

	// 0x007bebf0: 0 while playing, counted only by the win animation
	// FUN_00441724; drives the win highlight (`(_frameCounter & 2) != 0`).
	uint32 _frameCounter;

	Common::Array<PuzzleSpr> _pz1lFrames;  // _DAT_007beae0
	Common::Array<PuzzleSpr> _pz2lFrames;  // _DAT_007beadc
	Common::Array<uint16>    _bgPixels;    // PZLION.TGA
	bool _bgLoaded;

	void renderFrame(Graphics::ManagedSurface &out);

	// Play one of the eclipse-puzzle APC sounds (synchronous setup,
	// asynchronous playback).  `name` is "LION1" / "LION2" / "LION3".
	// `wait` blocks until the sound finishes (used for the win sound).
	void playSound(const char *name, bool wait);
	void stopSounds();                        // FUN_0043d0e0
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif

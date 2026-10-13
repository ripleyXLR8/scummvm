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

#include "common/ptr.h"
#include "common/stream.h"
#include "common/system.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_snakes.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

// FUN_0043b0f4 does not clear the snakes' places and piece indexes: they
// carry over from the last run in the process, as in the exe's memory.
static SnakesRules::Snake g_lastSnakes[SnakesRules::kNumSnakes];

SnakesPuzzle::SnakesPuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _loaded(false) {
	bool ok = loadPuzzleTGA(engine, "LABBASE4.TGA", _bgPixels);
	ok = loadPuzzleSpr(engine, "SERPNEW.SPR", _frames) && ok;
	// FUN_0043a53c opens <root>puzzles\chemin00.PCX with cryo.dll's
	// _GL_CreateFile, which serves it from the BIG files.
	Common::ScopedPtr<Common::SeekableReadStream> pcx(
	    engine->openBigFileStream(kFileTypePuzzles, "CHEMIN00.PCX"));
	if (pcx) {
		Common::Array<byte> bytes((uint)pcx->size());
		pcx->read(bytes.data(), bytes.size());
		ok = _rules.decodeMap(bytes.data(), bytes.size()) && ok;
	} else {
		ok = false;
	}
	memcpy(_rules.snakes, g_lastSnakes, sizeof(g_lastSnakes));
	_rules.setup();
	_loaded = ok;
}

// FUN_0043bebc's drawing: the background, then the sprites.
void SnakesPuzzle::render(Graphics::ManagedSurface &out) {
	if (_bgPixels.size() == (uint)kScreenW * kScreenH && out.format.bytesPerPixel == 2) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(out.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	}
	Common::Array<SnakesRules::Draw> draws;
	_rules.draws(draws);
	for (uint i = 0; i < draws.size(); i++)
		if (draws[i].frame >= 0 && (uint)draws[i].frame < _frames.size())
			blitFrame(out, _frames[draws[i].frame], draws[i].x, draws[i].y);
}

// The samples the entry loads into 0x007739b8.. (PzS1_1..5.wav); PzS1_4 is
// loaded but not played.
void SnakesPuzzle::playSound(SnakesRules::Sound s) {
	static const char *const kNames[] = { nullptr, "PZS1_1", "PZS1_2", "PZS1_3", "PZS1_5" };
	if (kNames[s])
		_engine->playPuzzleSound(kNames[s]);
}

// FUN_0043b0f4: phase 1 (FUN_0043c35c) until the four pieces are placed,
// then phase 2 (FUN_0043c6bc); each frame FUN_0043bebc.
int SnakesPuzzle::run() {
	if (!_loaded)
		return 1;
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface working(kScreenW, kScreenH, fmt);

	g_system->lockMouse(false);
	// [UNKNOWN] the exe draws no cursor of its own here (FUN_0043bebc has no
	// cursor call); the engine's cursor is left shown.
	_engine->showMouse(true);

	bool prevButton = (_engine->getCurrentMouseButton() != 0);
	while (_rules.result == 0 && !_engine->shouldAbort()) {
		_engine->pollEvents();
		const Common::Point m = _engine->getMousePos();
		const bool nowButton = (_engine->getCurrentMouseButton() == 1);
		if (nowButton && !prevButton) {
			if (!_rules.allPlaced())
				playSound(_rules.place(m.x, m.y));
			else
				playSound(_rules.steer(m.x, m.y));
		}
		prevButton = nowButton;
		if (_rules.result != 0)
			break;   // phase 2 leaves before the frame
		if (_rules.frame(m.x, m.y))
			playSound(SnakesRules::kSoundWin);
		// [UNKNOWN] FUN_0043c114, the win animation, is not ported.
		render(working);
		g_system->copyRectToScreen(working.getPixels(), working.pitch, 0, 0, kScreenW, kScreenH);
		g_system->updateScreen();
		g_system->delayMillis(15);
	}
	memcpy(g_lastSnakes, _rules.snakes, sizeof(g_lastSnakes));
	return _rules.result ? _rules.result : 1;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

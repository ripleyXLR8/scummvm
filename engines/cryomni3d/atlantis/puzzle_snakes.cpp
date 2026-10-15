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
#include "cryomni3d/atlantis/sprite_decode.h"

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

// FUN_0043c7e8, each frame after FUN_0043bebc: CURSEURS.SPR frame 11 near
// the edges (where a click leaves), else the normal cursor (FUN_0042d558,
// frame 6; no object can be held here).
void SnakesPuzzle::drawCursor(Graphics::ManagedSurface &out, int mouseX, int mouseY) {
	const SprSprite *spr = _engine->cursorSprite(SnakesRules::atEdge(mouseX) ? 11 : 6);
	if (spr)
		drawSprSprite(*out.surfacePtr(), *spr, mouseX, mouseY);
}

// FUN_0043c114: 20 steps of 100 ticks of the 200 Hz timer 0x006ce118
// (500 ms), the pieces shown every other step (from the second), drawn
// before the snakes; no cursor.
void SnakesPuzzle::winAnimation(Graphics::ManagedSurface &out) {
	bool hidden = true;
	for (int step = 0x14; step != 0 && !_engine->shouldAbort(); step--) {
		if (_bgPixels.size() == (uint)kScreenW * kScreenH && out.format.bytesPerPixel == 2) {
			for (int y = 0; y < kScreenH; y++)
				memcpy(out.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
		}
		Common::Array<SnakesRules::Draw> draws;
		_rules.drawsWin(!hidden, draws);
		for (uint i = 0; i < draws.size(); i++)
			if (draws[i].frame >= 0 && (uint)draws[i].frame < _frames.size())
				blitFrame(out, _frames[draws[i].frame], draws[i].x, draws[i].y);
		hidden = !hidden;
		g_system->copyRectToScreen(out.getPixels(), out.pitch, 0, 0, kScreenW, kScreenH);
		g_system->updateScreen();
		// 100 increments of the tick counter (0x0043c341), counted from the
		// tick in progress after the present: 495..500 ms with the phase.
		const uint32 untilTick = g_system->getMillis() / 5 + 100;
		for (;;) {
			const uint32 now = g_system->getMillis();
			if (now / 5 >= untilTick || _engine->shouldAbort())
				break;
			_engine->pollEvents();
			g_system->delayMillis(MIN<uint32>(10, untilTick * 5 - now));
		}
	}
}

// The samples the entry loads into 0x007739b8.. (PzS1_1..5.wav); PzS1_4 is
// loaded but not played.
void SnakesPuzzle::playSound(SnakesRules::Sound s) {
	static const char *const kNames[] = { nullptr, "PZS1_1", "PZS1_2", "PZS1_3", "PZS1_5" };
	if (kNames[s])
		_engine->playPuzzleSound(kNames[s]);
}

// FUN_0043b0f4: phase 1 (FUN_0043c35c) until the four pieces are placed,
// then phase 2 (FUN_0043c6bc); each frame FUN_0043bebc then the cursor
// FUN_0043c7e8.  A leave click ends phase 2 before the frame (the result
// test at 0x0043b392 follows FUN_0043c6bc), phase 1 after it: its loop
// draws, presents and polls the input before testing the result at
// 0x0043b34e.
int SnakesPuzzle::run() {
	if (!_loaded)
		return 1;
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface working(kScreenW, kScreenH, fmt);

	g_system->lockMouse(false);
	_engine->showMouse(false);   // the cursor is drawn into the frame

	bool prevButton = (_engine->getCurrentMouseButton() != 0);
	while (_rules.result == 0 && !_engine->shouldAbort()) {
		_engine->pollEvents();
		const Common::Point m = _engine->getMousePos();
		const bool nowButton = (_engine->getCurrentMouseButton() == 1);
		const bool phase1 = !_rules.allPlaced();
		if (nowButton && !prevButton) {
			if (phase1)
				playSound(_rules.place(m.x, m.y));
			else
				playSound(_rules.steer(m.x, m.y));
		}
		prevButton = nowButton;
		if (_rules.result != 0 && !phase1)
			break;   // phase 2 leaves before the frame
		if (_rules.frame(m.x, m.y)) {
			playSound(SnakesRules::kSoundWin);
			winAnimation(working);
			break;
		}
		render(working);
		drawCursor(working, m.x, m.y);
		g_system->copyRectToScreen(working.getPixels(), working.pitch, 0, 0, kScreenW, kScreenH);
		g_system->updateScreen();
		if (_rules.result != 0)
			break;   // phase 1 leaves after the frame
		g_system->delayMillis(15);
	}
	_engine->showMouse(true);
	memcpy(g_lastSnakes, _rules.snakes, sizeof(g_lastSnakes));
	return _rules.result ? _rules.result : 1;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

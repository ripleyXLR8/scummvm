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

#include "common/system.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_throne.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

ThronePuzzle::ThronePuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _loaded(false), _prevButton(false), _bgDone(false) {
	bool ok = loadPuzzleTGA(engine, "PUZTRONE.TGA", _bgPixels);
	ok = loadPuzzleSpr(engine, "PUZTRONE.SPR", _frames) && ok;
	_loaded = ok;
}

int ThronePuzzle::play() {
	if (!_loaded)
		return 1;
	_working.create(kScreenW, kScreenH, g_system->getScreenFormat());
	g_system->lockMouse(false);
	_engine->showMouse(false);   // FUN_004400ec draws the cursor into the frame
	_prevButton = (_engine->getCurrentMouseButton() != 0);
	// the shared cursor animation (engine.h PuzzleCursorState)
	cursorMode = _engine->_puzzleCursor.mode;
	cursorFrame = _engine->_puzzleCursor.frame;
	cursorStep = _engine->_puzzleCursor.step;
	const int r = run();
	_engine->_puzzleCursor.mode = cursorMode;
	_engine->_puzzleCursor.frame = cursorFrame;
	_engine->_puzzleCursor.step = cursorStep;
	_engine->showMouse(true);
	_working.free();
	return _engine->shouldAbort() ? 1 : r;
}

void ThronePuzzle::readInput(int &mx, int &my, bool &click) {
	_engine->pollEvents();
	const Common::Point m = _engine->getMousePos();
	const bool now = (_engine->getCurrentMouseButton() == 1);
	mx = m.x;
	my = m.y;
	click = now && !_prevButton;
	_prevButton = now;
}

void ThronePuzzle::drawBackground() {
	// The first copy of a pass restores the background; the second is the
	// frame copy, done by present().
	if (_bgDone)
		return;
	_bgDone = true;
	if (_bgPixels.size() == (uint)kScreenW * kScreenH && _working.format.bytesPerPixel == 2) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(_working.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	}
}

void ThronePuzzle::drawSprite(char bank, int x, int y, int frame) {
	if (bank == 'C') {
		// What FUN_00410430 does with the cursor animation's frame -1 is
		// unknown; nothing is drawn.
		if (frame < 0)
			return;
		if (const SprSprite *spr = _engine->cursorSprite((uint)frame))
			drawSprSprite(*_working.surfacePtr(), *spr, x, y);
		return;
	}
	if (frame >= 0 && (uint)frame < _frames.size())
		blitFrame(_working, _frames[frame], x, y);
}

void ThronePuzzle::present() {
	_bgDone = false;
	g_system->copyRectToScreen(_working.getPixels(), _working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
	// The original loop has no wait; its pace on the machines of the time
	// is unknown.
	g_system->delayMillis(15);
}

void ThronePuzzle::playSound(int slot) {
	static const char *const kNames[] = { "TRONE1", "TRONE2", "TRONE3", "TRONE4" };
	if (slot >= 0 && slot < 4)
		_engine->playPuzzleSound(kNames[slot]);
}

void ThronePuzzle::waitTicks(uint32 n) {
	const uint32 until = g_system->getMillis() + n * 5;
	while (g_system->getMillis() < until && !_engine->shouldAbort()) {
		_engine->pollEvents();
		g_system->delayMillis(10);
	}
}

bool ThronePuzzle::aborted() const {
	return _engine->shouldAbort();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

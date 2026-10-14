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
#include "common/textconsole.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_muria.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

MuriaPuzzle::MuriaPuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _loaded(false), _prevButton(false), _bgDone(false) {
	bool ok = loadPuzzleSpr(engine, "PUZMURIA.SPR", _frames);
	ok = loadPuzzleTGA(engine, "PUZMURIA.TGA", _bgPixels) && ok;
	_loaded = ok;
	if (!ok)
		warning("MuriaPuzzle: missing assets (sprites %u, picture %u)", _frames.size(), _bgPixels.size());
}

int MuriaPuzzle::play() {
	if (!_loaded)
		return 1;
	_working.create(kScreenW, kScreenH, g_system->getScreenFormat());
	g_system->lockMouse(false);
	_engine->showMouse(false);   // FUN_0043f4d4 draws the cursor into the frame
	_prevButton = (_engine->getCurrentMouseButton() != 0);
	// the shared cursor animation (engine.h PuzzleCursorState)
	cursorMode = _engine->_puzzleCursor.mode;
	cursorFrame = _engine->_puzzleCursor.frame;
	cursorStep = _engine->_puzzleCursor.step;
	const int r = run();
	_engine->_puzzleCursor.mode = cursorMode;
	_engine->_puzzleCursor.frame = cursorFrame;
	_engine->_puzzleCursor.step = cursorStep;
	// FUN_0043ee54 zeroes FLAGTEMP3 after the loop.
	_engine->setGameVar("flagtemp3", 0);
	_engine->showMouse(true);
	_working.free();
	return _engine->shouldAbort() ? 1 : r;
}

void MuriaPuzzle::readInput(int &mx, int &my, bool &clicked) {
	_engine->pollEvents();
	const Common::Point m = _engine->getMousePos();
	const bool now = (_engine->getCurrentMouseButton() == 1);
	mx = m.x;
	my = m.y;
	clicked = now && !_prevButton;
	_prevButton = now;
}

void MuriaPuzzle::startInput(int &mx, int &my, bool &clicked) {
	readInput(mx, my, clicked);
}

void MuriaPuzzle::boundary(int &mx, int &my, bool &clicked) {
	readInput(mx, my, clicked);
}

void MuriaPuzzle::loadPicture(const char *) {
	// The one picture, loaded by the constructor.
}

void MuriaPuzzle::drawBackground() {
	// The first copy of a pass restores the picture; the second is the
	// frame copy, done by present().
	if (_bgDone)
		return;
	_bgDone = true;
	if (_bgPixels.size() == (uint)kScreenW * kScreenH && _working.format.bytesPerPixel == 2)
		for (int y = 0; y < kScreenH; y++)
			memcpy(_working.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
}

void MuriaPuzzle::drawSprite(char bank, int x, int y, int frame) {
	if (bank == 'C') {
		if (frame < 0)
			return;
		if (const SprSprite *spr = _engine->cursorSprite((uint)frame))
			drawSprSprite(*_working.surfacePtr(), *spr, x, y);
		return;
	}
	if (frame >= 0 && (uint)frame < _frames.size())
		blitFrame(_working, _frames[frame], x, y);
}

void MuriaPuzzle::present() {
	_bgDone = false;
	g_system->copyRectToScreen(_working.getPixels(), _working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
	// The original loop has no wait; its pace on the machines of the time
	// is unknown.
	g_system->delayMillis(15);
}

void MuriaPuzzle::playSound(int slot) {
	static const char *const kNames[] = { "MURIA1", "MURIA2", "MURIA3", "MURIA4", "MURIA5" };
	if (slot >= 0 && slot < 5)
		_engine->playPuzzleSound(kNames[slot]);
}

void MuriaPuzzle::scriptHook() {
	// FUN_0043faf0: FLAGTEMP3 = 1021 + slot, then FUN_004277c8 runs the
	// first /sel entry that holds (CHAPI069 and 070 react to 1021..1024).
	_engine->setGameVar("flagtemp3", flagTemp3);
	_engine->runSelScan();
}

bool MuriaPuzzle::aborted() const {
	return _engine->shouldAbort();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

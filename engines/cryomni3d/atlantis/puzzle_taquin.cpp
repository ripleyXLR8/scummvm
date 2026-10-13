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
#include "cryomni3d/atlantis/puzzle_taquin.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

TaquinPuzzle::TaquinPuzzle(CryOmni3DEngine_Atlantis *engine, int variant)
	: TaquinGame(variant), _engine(engine), _loaded(false), _prevButton(false), _bgDone(false),
	  _variant(variant) {
	_loaded = loadPuzzleSpr(engine, spriteName(), _frames);
}

const char *TaquinPuzzle::spriteName() const {
	return _variant == 7 ? "PUZTAQ2.SPR" : _variant == 14 ? "PUZTAQ3.SPR" : "PUZTAQ1.SPR";
}

int TaquinPuzzle::play() {
	if (!_loaded)
		return 1;
	// The entry reads OBJETENMAIN (the port's held object) and picks the
	// picture: the "b" one with object 322 (the missing tile) in hand.
	const Object *sel = _engine->heldObject();
	objectInHand = (sel && sel->idOBJ() != uint(-1)) ? (int)sel->idOBJ() : 0;
	static const char *const kPics[3][2] = {
		{ "PUZTAQ1.TGA", "PUZTAQ1B.TGA" }, { "PUZTAQ2.TGA", "PUZTAQ2B.TGA" }, { "PUZTAQ3.TGA", "PUZTAQ3B.TGA" }
	};
	const int v = _variant == 7 ? 1 : _variant == 14 ? 2 : 0;
	if (!loadPuzzleTGA(_engine, kPics[v][objectInHand == 0x142 ? 1 : 0], _bgPixels))
		return 1;
	_working.create(kScreenW, kScreenH, g_system->getScreenFormat());
	g_system->lockMouse(false);
	_engine->showMouse(false);   // FUN_00446f28 draws the cursor into the frame
	_prevButton = (_engine->getCurrentMouseButton() != 0);
	const int r = run();
	// FUN_00446bbc emptied the hand.
	_engine->clearHeldObject();
	_engine->showMouse(true);
	_working.free();
	return _engine->shouldAbort() ? 1 : r;
}

void TaquinPuzzle::startInput(int &mx, int &my, bool &click) {
	readInput(mx, my, click);
}

void TaquinPuzzle::boundary(int &mx, int &my, bool &click) {
	readInput(mx, my, click);
}

void TaquinPuzzle::drawStandardCursor(int mx, int my) {
	// FUN_0042d558 with no object in hand: CURSEURS frame 6.
	if (const SprSprite *spr = _engine->cursorSprite(6))
		drawSprSprite(*_working.surfacePtr(), *spr, mx, my);
}

void TaquinPuzzle::readInput(int &mx, int &my, bool &click) {
	_engine->pollEvents();
	const Common::Point m = _engine->getMousePos();
	const bool now = (_engine->getCurrentMouseButton() == 1);
	mx = m.x;
	my = m.y;
	click = now && !_prevButton;
	_prevButton = now;
}

void TaquinPuzzle::drawBackground() {
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

void TaquinPuzzle::drawSprite(char bank, int x, int y, int frame) {
	if (bank == 'C') {
		// What FUN_00410430 does with frame -1 (the animation's last step)
		// is unknown; nothing is drawn.
		if (frame < 0)
			return;
		if (const SprSprite *spr = _engine->cursorSprite((uint)frame))
			drawSprSprite(*_working.surfacePtr(), *spr, x, y);
		return;
	}
	if (frame >= 0 && (uint)frame < _frames.size())
		blitFrame(_working, _frames[frame], x, y);
}

void TaquinPuzzle::present() {
	_bgDone = false;
	g_system->copyRectToScreen(_working.getPixels(), _working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
	// The original loop has no wait; its pace on the machines of the time
	// is unknown.
	g_system->delayMillis(15);
}

void TaquinPuzzle::playSound(int slot) {
	static const char *const kNames[] = { "TAK1", "TAK2", "TAK3", "TAK4" };
	if (slot >= 0 && slot < 4)
		_engine->playPuzzleSound(kNames[slot]);
}

bool TaquinPuzzle::aborted() const {
	return _engine->shouldAbort();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

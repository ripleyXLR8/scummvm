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

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_rat.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

RatPuzzle::RatPuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _loaded(false), _prevButton(false),
	  _mouseX(0), _mouseY(0) {
	bool ok = loadPuzzleTGA(engine, "PZFBACK.TGA", _bgPixels);
	ok = loadPuzzleSpr(engine, "PZFPIECE.SPR", _pieces) && ok;
	ok = loadPuzzleSpr(engine, "PZFRAT.SPR", _rat) && ok;
	// FUN_00434dfc: the 30 trajectories, served from the BIG files.
	for (int k = 0; k < kNumSlots && ok; k++) {
		Common::ScopedPtr<Common::SeekableReadStream> s(
		    engine->openBigFileStream(kFileTypePuzzles, kSlotFiles[k]));
		if (!s) {
			ok = false;
			break;
		}
		Common::Array<byte> d((uint)s->size());
		s->read(d.data(), d.size());
		ok = loadSlot(k, d.data(), d.size());
	}
	_loaded = ok;
}

int RatPuzzle::play() {
	if (!_loaded)
		return 1;
	_working.create(kScreenW, kScreenH, g_system->getScreenFormat());
	g_system->lockMouse(false);
	_engine->showMouse(false);   // FUN_00438ee4 draws the cursor into the frame
	_prevButton = (_engine->getCurrentMouseButton() != 0);
	int result = run();
	_engine->showMouse(true);
	_working.free();
	if (_engine->shouldAbort() || (result != 255 && result != 2))
		result = (result == 0xa) ? 2 : 1;
	return result;
}

// FUN_0043318c then FUN_0041a298's sampling: the mouse, the left button
// held, and a click on its press edge.
void RatPuzzle::readInput(int &mx, int &my, bool &click, bool &held) {
	_engine->pollEvents();
	const Common::Point m = _engine->getMousePos();
	const bool now = (_engine->getCurrentMouseButton() == 1);
	mx = _mouseX = m.x;
	my = _mouseY = m.y;
	held = now;
	click = now && !_prevButton;
	_prevButton = now;
}

void RatPuzzle::drawBackground(int which) {
	// Copy 0 restores the background; copy 1 (to the exe's second buffer)
	// and the frame copy 2 have no separate surface here.
	if (which != 0)
		return;
	if (_bgPixels.size() == (uint)kScreenW * kScreenH && _working.format.bytesPerPixel == 2) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(_working.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	}
}

void RatPuzzle::drawSprite(char bank, int x, int y, int frame) {
	const Common::Array<PuzzleSpr> &set = (bank == 'R') ? _rat : _pieces;
	if (frame >= 0 && (uint)frame < set.size())
		blitFrame(_working, set[frame], x, y);
}

// FUN_00438ee4 (CURSEURS frame 11 outside x 10..600, where a click leaves;
// else the normal cursor, frame 6), then the frame to the screen.
void RatPuzzle::present() {
	const bool edge = _mouseX < 10 || _mouseX > 600;
	if (const SprSprite *spr = _engine->cursorSprite(edge ? 11 : 6))
		drawSprSprite(*_working.surfacePtr(), *spr, _mouseX, _mouseY);
	g_system->copyRectToScreen(_working.getPixels(), _working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
}

// The samples FUN_00438bf0 loads into 0x007739b8 + 8k (flip15 is not one).
void RatPuzzle::playSound(int slot) {
	static const char *const kNames[] = {
		"FLIP1", "FLIP2", "FLIP3", "FLIP4", "FLIP5", "FLIP6", "FLIP7", "FLIP8", "FLIP9",
		"FLIP10", "FLIP11", "FLIP12", "FLIP13", "FLIP14", "FLIP16", "FLIP17", "FLIP18",
		"FLIP19", "FLIP20", "FLIP21", "FLIP22", "FLIP23", "FLIP24", "FLIP25", "FLIP26"
	};
	if (slot >= 0 && slot < (int)ARRAYSIZE(kNames))
		_engine->playPuzzleSound(kNames[slot], CryOmni3DEngine_Atlantis::kPlayCd60);   // FUN_0043cd60
}

int RatPuzzle::randomValue() {
	return _engine->crtRand();   // FUN_00438fd4 @ 0x00439418
}

uint32 RatPuzzle::ticks() {
	return g_system->getMillis() / 5;   // the 200 Hz timer 0x006ce118
}

void RatPuzzle::waitUntil(uint32 t) {
	while (ticks() < t && !_engine->shouldAbort()) {
		_engine->pollEvents();
		g_system->delayMillis(1);
	}
}

bool RatPuzzle::aborted() const {
	return _engine->shouldAbort();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

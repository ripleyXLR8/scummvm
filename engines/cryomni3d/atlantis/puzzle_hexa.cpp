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
#include "cryomni3d/atlantis/puzzle_hexa.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

HexaPuzzle::HexaPuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _loaded(false), _prevButton(false), _bgDone(false), _prevRight(false) {
	bool ok = loadPuzzleSpr(engine, "PUZHEXA.SPR", _frames);
	ok = loadPuzzleTGA(engine, "PUZHEXA.TGA", _bgPixels) && ok;
	Common::ScopedPtr<Common::SeekableReadStream> spr(engine->openBigFileStream(kFileTypePuzzles, "PUZHEXA.SPR"));
	if (spr) {
		Common::Array<byte> d((uint)spr->size());
		spr->read(d.data(), d.size());
		ok = loadCoverage(d.data(), d.size()) && ok;
	} else {
		ok = false;
	}
	Common::ScopedPtr<Common::SeekableReadStream> bmp(engine->openBigFileStream(kFileTypePuzzles, "PUZHEXAM.BMP"));
	if (bmp && bmp->size() >= 0x436 + (int64)kMapSize) {
		bmp->seek(0x436);
		_mapFile.resize(kMapSize);
		bmp->read(_mapFile.data(), _mapFile.size());
	} else {
		ok = false;
	}
	_loaded = ok;
}

int HexaPuzzle::play() {
	if (!_loaded)
		return 1;
	_working.create(kScreenW, kScreenH, g_system->getScreenFormat());
	g_system->lockMouse(false);
	_engine->showMouse(false);   // FUN_00440cb8 draws the cursor into the frame
	_prevButton = (_engine->getCurrentMouseButton() != 0);
	_prevRight = false;
	const int r = run();
	// FUN_004406fc set OBJETENMAIN to 0 and FlagTemp6 to 1.
	_engine->clearHeldObject();
	_engine->setGameVar("flagtemp6", flagTemp6);
	_engine->showMouse(true);
	_working.free();
	return _engine->shouldAbort() ? 1 : r;
}

void HexaPuzzle::readInput(int &mx, int &my, bool &click, bool &right) {
	_engine->pollEvents();
	const Common::Point m = _engine->getMousePos();
	const uint btn = _engine->getCurrentMouseButton();
	const bool now = (btn == 1), nowRight = (btn == 2);
	mx = m.x;
	my = m.y;
	click = now && !_prevButton;
	// FUN_0041a298 reports the right button while it is down; FUN_00448084
	// keeps its own repeat counter.
	right = nowRight;
	_prevButton = now;
	_prevRight = nowRight;
}

void HexaPuzzle::startInput(int &mx, int &my, bool &click, bool &right) {
	readInput(mx, my, click, right);
}

void HexaPuzzle::boundary(int &mx, int &my, bool &click, bool &right) {
	readInput(mx, my, click, right);
}

void HexaPuzzle::reloadMap() {
	map = _mapFile;
}

void HexaPuzzle::drawStandardCursor(int mx, int my) {
	// FUN_0042d558 with no object in hand: CURSEURS frame 6.
	if (const SprSprite *spr = _engine->cursorSprite(6))
		drawSprSprite(*_working.surfacePtr(), *spr, mx, my);
}

void HexaPuzzle::drawBackground() {
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

void HexaPuzzle::drawSprite(char bank, int x, int y, int frame) {
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

void HexaPuzzle::present() {
	_bgDone = false;
	g_system->copyRectToScreen(_working.getPixels(), _working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
	// The original loop has no wait; its pace on the machines of the time
	// is unknown.
	g_system->delayMillis(15);
}

void HexaPuzzle::playSound(int slot) {
	static const char *const kNames[] = { "HEXA1", "HEXA2", "HEXA3", "HEXA4", "HEXA5" };
	if (slot >= 0 && slot < 5)
		_engine->playPuzzleSound(kNames[slot]);
}

bool HexaPuzzle::aborted() const {
	return _engine->shouldAbort();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
